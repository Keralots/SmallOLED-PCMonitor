/*
 * SmallOLED-PCMonitor - Weather Module (Open-Meteo)
 *
 * Ported from AnimatedPixelClock. A short-lived FreeRTOS task fetches current
 * conditions, today's range, the next three days and the next twelve hours
 * every 10 minutes into a small struct the render loop reads.
 */

#ifndef WEATHER_H
#define WEATHER_H

#include <Arduino.h>

// Condition groups the icon renderer understands (mapped from WMO codes)
enum WeatherIconKind : uint8_t {
  WICON_SUN = 0,
  WICON_PARTCLOUD,
  WICON_CLOUD,
  WICON_FOG,
  WICON_RAIN,
  WICON_SNOW,
  WICON_STORM,
};

#define WEATHER_FORECAST_DAYS 3
#define WEATHER_FORECAST_HOURS 12
#define WEATHER_STYLE_ID 14

struct WeatherDay {
  int8_t wday;           // 0 = Sunday; -1 when the day is missing
  int weatherCode;       // raw WMO code
  float tempMaxC;
  float tempMinC;
  int precipChance;      // max precipitation probability %, -1 if unknown
};

struct WeatherData {
  bool valid;            // true once at least one fetch succeeded
  float tempC;
  float tempMinC;        // today
  float tempMaxC;
  int humidity;          // %
  float windKmh;
  int weatherCode;       // raw WMO code
  char sunrise[6];       // "HH:MM", location time
  char sunset[6];
  WeatherDay days[WEATHER_FORECAST_DAYS];  // tomorrow onwards
  int8_t hourStart;      // local hour of hourTempC[0]; -1 when hourly data is missing
  float hourTempC[WEATHER_FORECAST_HOURS];
  int8_t hourPrecip[WEATHER_FORECAST_HOURS];  // %, -1 if unknown
  unsigned long fetchedAt;  // millis() of the last successful fetch
};

// Call from loop(). Starts a one-shot fetch task when a fetch is due; returns
// at once while weather is unconfigured or not on screen.
void weatherLoop();

// Thread-safe snapshot of the latest data
WeatherData getWeather();

// True once a location is set
bool weatherConfigured();

// True after a fetch attempt failed and nothing has been fetched yet
bool weatherFailed();

// Fetch on the next weatherLoop() (call after weather settings change)
void weatherSettingsChanged();

WeatherIconKind weatherIconFromCode(int wmoCode);

#endif // WEATHER_H
