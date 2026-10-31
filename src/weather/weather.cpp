/*
 * SmallOLED-PCMonitor - Weather Module (Open-Meteo)
 *
 * The fetch (DNS + HTTP transfer) can block for seconds, so it runs in its own
 * task instead of loop(). The task lives for one fetch and deletes itself, so
 * its stack is not held between fetches. Plain HTTP on a bare WiFiClient: the
 * data is public, a TLS handshake would cost ~40 KB of heap and stall the single
 * C3 core, and HTTPClient alone links in ~165 KB of mbedTLS.
 */

#include "weather.h"

#include <ArduinoJson.h>
#include <WiFi.h>

#include "../config/config.h"
#include "../clocks/cycle_config.h"

#define WEATHER_FETCH_INTERVAL_MS (10UL * 60UL * 1000UL)
#define WEATHER_RETRY_INTERVAL_MS (60UL * 1000UL)
#define WEATHER_IDLE_POLL_MS 5000UL
#define WEATHER_TASK_STACK 8192

static WeatherData published = {};
static portMUX_TYPE weatherMux = portMUX_INITIALIZER_UNLOCKED;

// fetchBusy is set by loop() before the task starts and cleared by the task
// last, after it has written waitFromMs and waitMs.
static volatile bool fetchBusy = false;
static volatile bool fetchKick = false;
static volatile bool lastFetchFailed = false;
static volatile unsigned long waitFromMs = 0;
static volatile unsigned long waitMs = 0;  // 0: check now
static float fetchedLat = 0, fetchedLon = 0;

bool weatherConfigured() {
  // 0,0 (middle of the Atlantic) doubles as the "unset" marker
  return !(settings.weatherLat == 0 && settings.weatherLon == 0);
}

bool weatherFailed() {
  return lastFetchFailed && !getWeather().valid;
}

static bool weatherOnScreen() {
  if (settings.clockStyle == WEATHER_STYLE_ID) return true;
  if (settings.clockStyle != 9) return false;
  CycleEntry entries[CYCLE_COUNT];
  if (!parseCycleConfig(settings.cycleConfig, entries)) return true;
  for (unsigned i = 0; i < CYCLE_COUNT; ++i)
    if (entries[i].style == WEATHER_STYLE_ID && entries[i].seconds) return true;
  return false;
}

WeatherData getWeather() {
  portENTER_CRITICAL(&weatherMux);
  WeatherData copy = published;
  portEXIT_CRITICAL(&weatherMux);
  return copy;
}

WeatherIconKind weatherIconFromCode(int code) {
  if (code == 0) return WICON_SUN;
  if (code <= 2) return WICON_PARTCLOUD;
  if (code == 3) return WICON_CLOUD;
  if (code == 45 || code == 48) return WICON_FOG;
  if (code >= 71 && code <= 77) return WICON_SNOW;
  if (code == 85 || code == 86) return WICON_SNOW;
  if (code >= 95) return WICON_STORM;
  // Everything else in the 5x/6x/8x ranges is some form of rain/drizzle
  return WICON_RAIN;
}

// Copy "HH:MM" out of an ISO-8601 timestamp ("2026-07-03T04:30")
static void extractClockTime(const char* iso, char out[6]) {
  out[0] = '\0';
  if (!iso) return;
  const char* t = strchr(iso, 'T');
  if (t && strlen(t) >= 6) {
    memcpy(out, t + 1, 5);
    out[5] = '\0';
  }
}

// Day of week (0 = Sunday) of an ISO date ("2026-10-07"), -1 if malformed
static int8_t weekdayOf(const char* iso) {
  int y, m, d;
  if (!iso || sscanf(iso, "%d-%d-%d", &y, &m, &d) != 3 || m < 1 || m > 12) return -1;
  static const int T[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  if (m < 3) y--;
  return (int8_t)((y + y / 4 - y / 100 + y / 400 + T[m - 1] + d) % 7);
}

static bool fetchWeather() {
  float lat = settings.weatherLat, lon = settings.weatherLon;
  char path[400];
  int len = snprintf(path, sizeof(path),
           "/v1/forecast?latitude=%.4f&longitude=%.4f"
           "&current=temperature_2m,relative_humidity_2m,weather_code,wind_speed_10m"
           "&daily=weather_code,temperature_2m_max,temperature_2m_min,"
           "precipitation_probability_max,sunrise,sunset"
           "&hourly=temperature_2m,precipitation_probability&forecast_hours=%d"
           "&timezone=auto&forecast_days=%d",
           lat, lon, WEATHER_FORECAST_HOURS, 1 + WEATHER_FORECAST_DAYS);
  if (len < 0 || len >= (int)sizeof(path)) return false;  // a cut URL loses timezone=auto

  // HTTP/1.0 with Connection: close, so the body is neither chunked nor kept alive
  WiFiClient client;
  client.setTimeout(10);
  if (!client.connect("api.open-meteo.com", 80)) {
    Serial.println("Weather fetch failed: no connection");
    return false;
  }
  client.printf("GET %s HTTP/1.0\r\nHost: api.open-meteo.com\r\nConnection: close\r\n\r\n", path);
  String status = client.readStringUntil('\n');
  if (!status.startsWith("HTTP/1.") || status.substring(9, 12) != "200" || !client.find("\r\n\r\n")) {
    Serial.printf("Weather fetch failed: %s\n", status.c_str());
    client.stop();
    return false;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, client);
  client.stop();
  if (err) {
    Serial.printf("Weather JSON error: %s\n", err.c_str());
    return false;
  }

  JsonObject current = doc["current"];
  JsonObject daily = doc["daily"];
  if (current.isNull() || daily.isNull()) return false;

  WeatherData fresh = {};
  fresh.valid = true;
  fresh.tempC = current["temperature_2m"] | 0.0f;
  fresh.humidity = current["relative_humidity_2m"] | 0;
  fresh.weatherCode = current["weather_code"] | 3;
  fresh.windKmh = current["wind_speed_10m"] | 0.0f;
  fresh.tempMaxC = daily["temperature_2m_max"][0] | 0.0f;
  fresh.tempMinC = daily["temperature_2m_min"][0] | 0.0f;
  extractClockTime(daily["sunrise"][0], fresh.sunrise);
  extractClockTime(daily["sunset"][0], fresh.sunset);
  for (int i = 0; i < WEATHER_FORECAST_DAYS; i++) {
    WeatherDay& day = fresh.days[i];
    day.wday = weekdayOf(daily["time"][i + 1]);
    day.weatherCode = daily["weather_code"][i + 1] | 3;
    day.tempMaxC = daily["temperature_2m_max"][i + 1] | 0.0f;
    day.tempMinC = daily["temperature_2m_min"][i + 1] | 0.0f;
    day.precipChance = daily["precipitation_probability_max"][i + 1] | -1;
  }
  JsonObject hourly = doc["hourly"];
  fresh.hourStart = -1;
  const char* firstHour = hourly["time"][0];
  const char* t = firstHour ? strchr(firstHour, 'T') : nullptr;
  if (t && isdigit((unsigned char)t[1]) && isdigit((unsigned char)t[2])) {
    fresh.hourStart = (int8_t)((t[1] - '0') * 10 + (t[2] - '0'));
  }
  for (int i = 0; i < WEATHER_FORECAST_HOURS; i++) {
    fresh.hourTempC[i] = hourly["temperature_2m"][i] | 0.0f;
    fresh.hourPrecip[i] = (int8_t)(hourly["precipitation_probability"][i] | -1);
  }
  if (hourly["temperature_2m"][WEATHER_FORECAST_HOURS - 1].isNull()) fresh.hourStart = -1;
  fresh.fetchedAt = millis();

  // The location changed while this fetch was in flight: drop the result
  if (lat != settings.weatherLat || lon != settings.weatherLon) return false;

  portENTER_CRITICAL(&weatherMux);
  published = fresh;
  portEXIT_CRITICAL(&weatherMux);
  fetchedLat = lat;
  fetchedLon = lon;

  Serial.printf("Weather: %.1fC code %d (RH %d%%)\n", fresh.tempC,
                fresh.weatherCode, fresh.humidity);
  return true;
}

static void weatherFetchTask(void*) {
  bool ok = fetchWeather();
  lastFetchFailed = !ok;
  waitFromMs = millis();
  waitMs = ok ? WEATHER_FETCH_INTERVAL_MS : WEATHER_RETRY_INTERVAL_MS;
  fetchBusy = false;
  vTaskDelete(nullptr);
}

void weatherLoop() {
  if (fetchBusy) return;
  const unsigned long now = millis();
  if (fetchKick) {
    fetchKick = false;
    waitMs = 0;
  }
  if (now - waitFromMs < waitMs) return;
  if (!weatherConfigured() || !weatherOnScreen() || WiFi.status() != WL_CONNECTED) {
    waitFromMs = now;
    waitMs = WEATHER_IDLE_POLL_MS;
    return;
  }
  fetchBusy = true;
  // Same priority as loop(): the arduino loop task never blocks, so a lower
  // priority task would starve on the single C3 core.
  if (xTaskCreate(weatherFetchTask, "weather", WEATHER_TASK_STACK, nullptr, 1, nullptr) != pdPASS) {
    Serial.println("Weather fetch task not started, retrying in a minute");
    fetchBusy = false;
    waitFromMs = now;
    waitMs = WEATHER_RETRY_INTERVAL_MS;
  }
}

void weatherSettingsChanged() {
  if (settings.weatherLat != fetchedLat || settings.weatherLon != fetchedLon) {
    portENTER_CRITICAL(&weatherMux);
    published.valid = false;
    portEXIT_CRITICAL(&weatherMux);
    lastFetchFailed = false;
  }
  fetchKick = true;
}
