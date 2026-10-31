/*
 * SmallOLED-PCMonitor - Weather Clock (style 14)
 *
 * Port of AnimatedPixelClock's weather clock: same 128x64 geometry, layouts
 * and animation timing. That panel is colour; here everything is one colour,
 * so dimmed lines become dotted, dimmed fills become a checkerboard, the lit
 * cloud of a lightning flash grows by a pixel, and a black rim separates a
 * cloud from the sun behind it instead of a second colour.
 *
 * Time on top, animated condition icon + big temperature in the middle,
 * details row (high/low + humidity alternating with sunrise/sunset) at the
 * bottom. Forecast layouts move all of that into the left half and show the
 * forecast on the right. Icon animation runs off millis(), so there is no
 * state to reset between style switches.
 */

#include "../config/config.h"
#include "../display/display.h"
#include "../weather/weather.h"
#include "clocks.h"
#include "clock_globals.h"
#include <math.h>
#include <string.h>

// Layout
#define WTIME_Y 2
#define WICON_X 10
#define WICON_Y 24
#define WICON_SIZE 24
#define WDETAIL_Y 55
#define WDETAIL_SWAP_MS 5000

// Above this the rain and snow slant, clouds drift faster and streaks blow past
#define WWIND_KMH 30.0f

struct IconScene {
  int code;          // raw WMO code, for precipitation intensity
  bool night;
  bool windy;
  float t;           // seconds
  unsigned long ms;
};

static uint32_t hash32(uint32_t x) {
  x ^= x >> 16; x *= 0x7feb352dU;
  x ^= x >> 15; x *= 0x846ca68bU;
  x ^= x >> 16;
  return x;
}

static int parseHHMM(const char* s) {
  if (!s || strlen(s) < 5 || s[2] != ':') return -1;
  return ((s[0] - '0') * 10 + (s[1] - '0')) * 60 + (s[3] - '0') * 10 + (s[4] - '0');
}

// ---------- One-colour stand-ins for dimmed colours ----------

static void dottedHLine(int x, int y, int w) {
  for (int i = 0; i < w; i += 2) display.drawPixel(x + i, y, DISPLAY_WHITE);
}

static void dottedVLine(int x, int y, int h) {
  for (int i = 0; i < h; i += 2) display.drawPixel(x, y + i, DISPLAY_WHITE);
}

static void checkerRect(int x, int y, int w, int h) {
  for (int yy = y; yy < y + h; yy++)
    for (int xx = x + ((x + yy) & 1); xx < x + w; xx += 2) display.drawPixel(xx, yy, DISPLAY_WHITE);
}

// ---------- Icons ----------

// Puffy cloud built from three discs on a base bar, ~20px wide. `grow` adds a
// pixel all round: the rim that parts it from the sun, or a lightning glow.
static void drawCloudShape(int cx, int cy, uint16_t color, int grow = 0) {
  display.fillCircle(cx - 6, cy + 2, 4 + grow, color);
  display.fillCircle(cx, cy - 1, 5 + grow, color);
  display.fillCircle(cx + 6, cy + 2, 4 + grow, color);
  display.fillRect(cx - 6 - grow, cy + 2, 13 + 2 * grow, 4 + grow, color);
}

static void drawRimmedCloud(int cx, int cy) {
  drawCloudShape(cx, cy, DISPLAY_BLACK, 1);
  drawCloudShape(cx, cy, DISPLAY_WHITE);
}

// Disc that breathes by one pixel, with rays turning slowly around it.
// Alternate rays are shorter so the rotation reads.
static void drawSun(int cx, int cy, int r, float t) {
  int disc = r + ((sinf(t * 1.6f) > 0.6f) ? 1 : 0);
  display.fillCircle(cx, cy, disc, DISPLAY_WHITE);
  float spin = t * 0.5f;
  for (int i = 0; i < 8; i++) {
    float a = spin + i * (PI / 4.0f);
    int len = r + ((i % 2) ? 4 : 5);
    display.drawLine(cx + (int)roundf(cosf(a) * (r + 2)), cy + (int)roundf(sinf(a) * (r + 2)),
                     cx + (int)roundf(cosf(a) * len), cy + (int)roundf(sinf(a) * len), DISPLAY_WHITE);
  }
}

static void drawMoon(int cx, int cy, int r) {
  display.fillCircle(cx, cy, r, DISPLAY_WHITE);
  display.fillCircle(cx + r / 2 + 1, cy - r / 2, r - 1, DISPLAY_BLACK);
}

// Fixed star field around the moon, each star twinkling on its own beat
static void drawStars(int x, int y, float t, int count) {
  static const int8_t STARS[][2] = {{2, 3}, {20, 1}, {24, 12}, {1, 18}, {22, 22}, {12, 0}};
  for (int i = 0; i < count && i < 6; i++) {
    float s = sinf(t * (0.9f + i * 0.23f) + i * 1.7f);
    if (s < 0.2f) continue;
    int sx = x + STARS[i][0], sy = y + STARS[i][1];
    display.drawPixel(sx, sy, DISPLAY_WHITE);
    if (s > 0.9f) {
      display.drawPixel(sx - 1, sy, DISPLAY_WHITE);
      display.drawPixel(sx + 1, sy, DISPLAY_WHITE);
      display.drawPixel(sx, sy - 1, DISPLAY_WHITE);
      display.drawPixel(sx, sy + 1, DISPLAY_WHITE);
    }
  }
}

// Precipitation strength from the WMO code: 0 light, 1 moderate, 2 heavy
static int precipLevel(int code) {
  switch (code) {
    case 51: case 56: case 61: case 66: case 71: case 77: case 80: case 85:
      return 0;
    case 55: case 57: case 65: case 67: case 75: case 82: case 86: case 99:
      return 2;
    default:
      return 1;
  }
}

static bool isDrizzle(int code) { return code >= 51 && code <= 57; }

static void drawRain(int cx, int top, int bottom, const IconScene& s, int level) {
  static const int COUNT[] = {3, 5, 7};
  static const int STEP_MS[] = {60, 45, 32};
  int n = COUNT[level];
  int len = isDrizzle(s.code) ? 1 : (level == 2 ? 3 : 2);
  int fallH = bottom - top;
  for (int i = 0; i < n; i++) {
    int x0 = cx - 8 + (n > 1 ? i * 16 / (n - 1) : 8);
    int fy = (int)((s.ms / STEP_MS[level] + (hash32(i + 7) % fallH)) % fallH);
    int slant = s.windy ? fy / 3 : 0;
    int x = x0 - slant;
    if (s.windy) display.drawLine(x, top + fy, x + len / 2 + 1, top + fy - len + 1, DISPLAY_WHITE);
    else display.drawFastVLine(x, top + fy, len, DISPLAY_WHITE);
    if (!isDrizzle(s.code) && fy >= fallH - 2) {
      display.drawPixel(x - 1, bottom + 1, DISPLAY_WHITE);
      display.drawPixel(x + 1, bottom + 1, DISPLAY_WHITE);
    }
  }
}

static void drawSnow(int cx, int top, int bottom, const IconScene& s, int level) {
  static const int COUNT[] = {4, 6, 8};
  int n = COUNT[level];
  int fallH = bottom - top;
  for (int i = 0; i < n; i++) {
    int x0 = cx - 9 + (n > 1 ? i * 18 / (n - 1) : 9);
    int fy = (int)((s.ms / 110 + (hash32(i + 3) % fallH)) % fallH);
    int x = x0 + (int)roundf(1.5f * sinf(s.t * 2.0f + i * 1.3f)) - (s.windy ? fy / 2 : 0);
    int y = top + fy;
    display.drawPixel(x, y, DISPLAY_WHITE);
    if (i % 3 == 0) {
      display.drawPixel(x - 1, y, DISPLAY_WHITE);
      display.drawPixel(x + 1, y, DISPLAY_WHITE);
      display.drawPixel(x, y - 1, DISPLAY_WHITE);
      display.drawPixel(x, y + 1, DISPLAY_WHITE);
    }
  }
}

// Short dotted gust lines blowing right to left across the icon area
static void drawWindStreaks(int x, int y, const IconScene& s) {
  static const int ROWS[] = {2, 13, 22};
  for (int i = 0; i < 3; i++) {
    int span = 70;
    int pos = (int)((s.ms / 18 + i * 23) % span);
    int sx = x + 44 - pos;
    int len = 4 + i % 2 * 2;
    for (int k = 0; k < len; k += 2) {
      int px = sx + k;
      if (px >= 0 && px < x + 46) display.drawPixel(px, y + ROWS[i], DISPLAY_WHITE);
    }
  }
}

// Lightning comes in random 500ms slots, about one in eight, as a double flash
static bool lightningOn(unsigned long ms, int* boltShift) {
  uint32_t slot = ms / 500;
  uint32_t h = hash32(slot * 2654435761U);
  if (h % 8 != 0) return false;
  unsigned long in = ms % 500;
  if (boltShift) *boltShift = (int)(h / 8 % 3) * 4 - 4;
  return in < 80 || (in >= 150 && in < 260);
}

static void drawWeatherIcon(int x, int y, WeatherIconKind kind, const IconScene& s) {
  int cx = x + WICON_SIZE / 2;
  int cy = y + WICON_SIZE / 2;
  float drift = s.windy ? 1.1f : 0.5f;

  switch (kind) {
    case WICON_SUN: {
      if (s.night) {
        drawStars(x - 2, y - 2, s.t, 6);
        drawMoon(cx, cy, 7);
      } else {
        drawSun(cx, cy, 6, s.t);
      }
      break;
    }
    case WICON_PARTCLOUD: {
      if (s.night) {
        drawStars(x - 2, y - 2, s.t, 3);
        drawMoon(cx - 4, cy - 4, 5);
      } else {
        drawSun(cx - 4, cy - 4, 4, s.t);
      }
      int off = (int)roundf(3.0f * sinf(s.t * drift));
      drawRimmedCloud(cx + 3 + off, cy + 4);
      break;
    }
    case WICON_CLOUD: {
      int back = (int)roundf(2.0f * sinf(s.t * drift * 0.7f));
      int front = (int)roundf(3.0f * sinf(s.t * drift + 1.0f));
      drawCloudShape(cx - 4 + back, cy - 5, DISPLAY_WHITE);
      drawRimmedCloud(cx + 3 + front, cy + 2);
      break;
    }
    case WICON_FOG: {
      // Four haze lines swaying in alternating directions, every other one dotted
      for (int i = 0; i < 4; i++) {
        int yy = y + 5 + i * 5;
        int shift = (int)roundf(3.0f * sinf(s.t * drift * 1.6f + i * 0.9f));
        if (i % 2) shift = -shift;
        if (i % 2) dottedHLine(x + 3 + shift, yy, 16);
        else display.drawFastHLine(x + 3 + shift, yy, 16, DISPLAY_WHITE);
      }
      break;
    }
    case WICON_RAIN: {
      drawRain(cx, cy + 1, y + WICON_SIZE + 2, s, precipLevel(s.code));
      drawCloudShape(cx + (int)roundf(sinf(s.t * drift)), cy - 6, DISPLAY_WHITE);
      break;
    }
    case WICON_SNOW: {
      drawSnow(cx, cy + 1, y + WICON_SIZE + 2, s, precipLevel(s.code));
      drawCloudShape(cx + (int)roundf(sinf(s.t * drift)), cy - 6, DISPLAY_WHITE);
      break;
    }
    case WICON_STORM: {
      int shift = 0;
      bool flash = lightningOn(s.ms, &shift);
      drawRain(cx, cy + 1, y + WICON_SIZE + 2, s, 1);
      drawCloudShape(cx, cy - 6, DISPLAY_WHITE, flash ? 1 : 0);
      if (flash) {
        int bx = cx + shift;
        display.drawLine(bx + 1, cy - 1, bx - 2, cy + 5, DISPLAY_WHITE);
        display.drawLine(bx - 2, cy + 5, bx + 2, cy + 5, DISPLAY_WHITE);
        display.drawLine(bx + 2, cy + 5, bx - 1, cy + 12, DISPLAY_WHITE);
      }
      break;
    }
  }

  if (s.windy) drawWindStreaks(x - 8, y - 4, s);
}

static int toUnit(float tempC) {
  return (int)roundf(settings.weatherUseFahrenheit ? tempC * 9.0f / 5.0f + 32.0f : tempC);
}

// Big temperature: size-3 digits plus a small degree circle and unit letter
static void drawTemperature(int x, int y, float tempC) {
  char buf[8];
  snprintf(buf, sizeof(buf), "%d", toUnit(tempC));

  display.setTextSize(3);
  display.setCursor(x, y);
  display.print(buf);

  int endX = x + strlen(buf) * 18;
  display.drawCircle(endX + 2, y + 1, 2, DISPLAY_WHITE);
  display.setTextSize(1);
  display.setCursor(endX + 7, y);
  display.print(settings.weatherUseFahrenheit ? "F" : "C");
}

static void printAt(int x, int y, const char* s, int size = 1) {
  display.setTextSize(size);
  display.setCursor(x, y);
  display.print(s);
}

// 13x13 forecast icon with one-pixel motion. `phase` keeps neighbouring icons
// out of step with each other.
static void drawMiniIcon(int x, int y, int code, int phase) {
  int cx = x + 6, cy = y + 6;
  unsigned long ms = millis() + phase * 1777UL;
  float t = ms / 1000.0f;
  int drift = (int)roundf(sinf(t * 0.7f));
  auto cloud = [](int ox, int oy, uint16_t c, int grow) {
    display.fillCircle(ox - 3, oy + 1, 2 + grow, c);
    display.fillCircle(ox + 1, oy - 1, 3 + grow, c);
    display.fillCircle(ox + 4, oy + 1, 2 + grow, c);
    display.fillRect(ox - 3 - grow, oy + 1, 8 + 2 * grow, 3 + grow, c);
  };
  auto drops = [&](int count, int stepMs, bool flakes) {
    for (int i = 0; i < count; i++) {
      int fy = (int)((ms / stepMs + i * 2) % 4);
      int dx = cx - 3 + i * 6 / max(1, count - 1);
      if (flakes) display.drawPixel(dx + ((ms / 400 + i) % 2), cy + 3 + fy, DISPLAY_WHITE);
      else display.drawFastVLine(dx, cy + 3 + fy, 2, DISPLAY_WHITE);
    }
  };
  switch (weatherIconFromCode(code)) {
    case WICON_SUN: {
      display.fillCircle(cx, cy, 3, DISPLAY_WHITE);
      float spin = t * 0.6f;
      for (int i = 0; i < 8; i++) {
        float a = spin + i * PI / 4;
        display.drawPixel(cx + (int)roundf(cosf(a) * 5), cy + (int)roundf(sinf(a) * 5), DISPLAY_WHITE);
      }
      break;
    }
    case WICON_PARTCLOUD:
      display.fillCircle(cx - 2, cy - 2, 3, DISPLAY_WHITE);
      cloud(cx + 1 + drift, cy + 2, DISPLAY_BLACK, 1);
      cloud(cx + 1 + drift, cy + 2, DISPLAY_WHITE, 0);
      break;
    case WICON_CLOUD:
      cloud(cx + drift, cy, DISPLAY_WHITE, 0);
      break;
    case WICON_FOG:
      for (int i = 0; i < 3; i++) {
        int shift = (int)roundf(sinf(t * 1.1f + i * 1.2f));
        if (i % 2) dottedHLine(x + 1 + shift, y + 3 + i * 3, 10);
        else display.drawFastHLine(x + 1 + shift, y + 3 + i * 3, 10, DISPLAY_WHITE);
      }
      break;
    case WICON_RAIN:
      drops(3, 90, false);
      cloud(cx, cy - 2, DISPLAY_WHITE, 0);
      break;
    case WICON_SNOW:
      drops(3, 220, true);
      cloud(cx, cy - 2, DISPLAY_WHITE, 0);
      break;
    case WICON_STORM: {
      drops(2, 90, false);
      bool flash = lightningOn(ms, nullptr);
      cloud(cx, cy - 2, DISPLAY_WHITE, flash ? 1 : 0);
      if (flash) {
        display.drawLine(cx, cy + 2, cx - 2, cy + 5, DISPLAY_WHITE);
        display.drawLine(cx - 2, cy + 5, cx + 1, cy + 7, DISPLAY_WHITE);
      }
      break;
    }
  }
}

static const char* const DAY_NAMES[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};

static void precipText(char* buf, size_t len, int chance, bool percentSign) {
  if (chance < 0) snprintf(buf, len, "--");
  else if (percentSign) snprintf(buf, len, "%d%%", chance);
  else snprintf(buf, len, "%d", chance);
}

// Current weather squeezed into the left half of the panel
static void drawForecastLeft(const WeatherData& wx, const IconScene& scene, bool haveTime,
                             const struct tm& timeinfo) {
  char buf[16];
  if (haveTime) {
    int displayHour, displayMin;
    bool isPM;
    formatTimeForDisplay(timeinfo.tm_hour, timeinfo.tm_min, displayHour, displayMin, isPM);
    snprintf(buf, sizeof(buf), "%02d%c%02d", displayHour, shouldShowColon() ? ':' : ' ', displayMin);
    printAt(2, 2, buf, 2);  // no AM/PM: the left half has no room for it
  } else {
    printAt(2, 6, !ntpSynced ? "Syncing.." : "Time Error");
  }

  drawWeatherIcon(2, 22, weatherIconFromCode(wx.weatherCode), scene);
  snprintf(buf, sizeof(buf), "%d", toUnit(wx.tempC));
  int tw = strlen(buf) * 12;
  if (tw <= 24) {
    printAt(30, 26, buf, 2);
    display.drawCircle(30 + tw + 2, 27, 1, DISPLAY_WHITE);
  } else {
    printAt(26, 26, buf, 2);
  }
  snprintf(buf, sizeof(buf), "\x18%d \x19%d", toUnit(wx.tempMaxC), toUnit(wx.tempMinC));
  printAt(4, 55, buf);
  dottedVLine(63, 2, 60);
}

// One row per day: name over rain chance, icon, high over low
static void drawForecastRows(const WeatherData& wx) {
  char buf[16];
  for (int i = 0; i < WEATHER_FORECAST_DAYS; i++) {
    const WeatherDay& d = wx.days[i];
    int y = 2 + i * 21;
    if (i < WEATHER_FORECAST_DAYS - 1) dottedHLine(66, y + 19, 60);
    if (d.wday < 0 || d.wday > 6) continue;
    printAt(67, y + 1, DAY_NAMES[d.wday]);
    precipText(buf, sizeof(buf), d.precipChance, true);
    printAt(67, y + 10, buf);
    drawMiniIcon(92, y + 2, d.weatherCode, i);
    snprintf(buf, sizeof(buf), "%d", toUnit(d.tempMaxC));
    printAt(127 - strlen(buf) * 6, y + 1, buf);
    snprintf(buf, sizeof(buf), "%d", toUnit(d.tempMinC));
    printAt(127 - strlen(buf) * 6, y + 10, buf);
  }
}

// One column per day: name, icon, high, low, rain chance
static void drawForecastColumns(const WeatherData& wx) {
  char buf[16];
  for (int i = 0; i < WEATHER_FORECAST_DAYS; i++) {
    const WeatherDay& d = wx.days[i];
    if (d.wday < 0 || d.wday > 6) continue;
    int x = 65 + i * 21;
    auto centred = [&](int y, const char* s) {
      printAt(x + (21 - (int)strlen(s) * 6) / 2, y, s);
    };
    centred(2, DAY_NAMES[d.wday]);
    drawMiniIcon(x + 4, 13, d.weatherCode, i);
    snprintf(buf, sizeof(buf), "%d", toUnit(d.tempMaxC));
    centred(30, buf);
    snprintf(buf, sizeof(buf), "%d", toUnit(d.tempMinC));
    centred(40, buf);
    precipText(buf, sizeof(buf), d.precipChance, d.precipChance < 100);
    centred(54, buf);
  }
}

// Temperature curve over checkerboard rain chance bars for the coming hours
static void drawForecastHours(const WeatherData& wx) {
  const int gx = 66, gy = 12, gh = 36, n = WEATHER_FORECAST_HOURS;
  printAt(66, 1, "NEXT 12H");
  float tmin = wx.hourTempC[0], tmax = wx.hourTempC[0];
  for (int i = 1; i < n; i++) {
    tmin = min(tmin, wx.hourTempC[i]);
    tmax = max(tmax, wx.hourTempC[i]);
  }
  if (tmax - tmin < 4.0f) {
    float mid = (tmax + tmin) / 2;
    tmin = mid - 2.0f;
    tmax = mid + 2.0f;
  }
  for (int i = 0; i < n; i++) {
    int bh = max(0, (int)wx.hourPrecip[i]) * gh / 100;
    if (bh) checkerRect(gx + i * 5, gy + gh - bh, 4, bh);
  }
  int px = 0, py = 0;
  for (int i = 0; i < n; i++) {
    int x = gx + i * 5 + 2;
    int y = gy + gh - 3 - (int)roundf((wx.hourTempC[i] - tmin) * (gh - 4) / (tmax - tmin));
    if (i) display.drawLine(px, py, x, y, DISPLAY_WHITE);
    px = x;
    py = y;
  }
  dottedHLine(gx, gy + gh, n * 5);
  char buf[4];
  const int marks[] = {0, 5, n - 1};
  const int markX[] = {gx, gx + 25, gx + 48};
  for (int k = 0; k < 3; k++) {
    int h = (wx.hourStart + marks[k]) % 24;
    if (!settings.use24Hour) h = h % 12 == 0 ? 12 : h % 12;
    snprintf(buf, sizeof(buf), "%d", h);
    printAt(markX[k], 54, buf);
  }
}

// Tomorrow with the full animated icon, then the two days after as lines
static void drawForecastTomorrow(const WeatherData& wx) {
  char buf[16];
  const WeatherDay& d = wx.days[0];
  printAt(66, 1, "TOMORROW");
  IconScene s;
  s.code = d.weatherCode;
  s.night = false;
  s.windy = false;
  s.ms = millis();
  s.t = s.ms / 1000.0f;
  drawWeatherIcon(65, 11, weatherIconFromCode(d.weatherCode), s);

  snprintf(buf, sizeof(buf), "%d", toUnit(d.tempMaxC));
  int tw = strlen(buf) * 12;
  if (tw <= 24) {
    printAt(94, 14, buf, 2);
    display.drawCircle(94 + tw + 2, 15, 1, DISPLAY_WHITE);
  } else {
    printAt(127 - tw, 14, buf, 2);
  }
  char lo[8], pop[8];
  snprintf(lo, sizeof(lo), "%d", toUnit(d.tempMinC));
  precipText(pop, sizeof(pop), d.precipChance, true);
  if (94 + (int)strlen(lo) * 6 + 3 > 127 - (int)strlen(pop) * 6)
    precipText(pop, sizeof(pop), d.precipChance, false);
  printAt(94, 32, lo);
  printAt(127 - strlen(pop) * 6, 32, pop);

  dottedHLine(66, 43, 60);
  for (int i = 1; i < WEATHER_FORECAST_DAYS; i++) {
    const WeatherDay& n = wx.days[i];
    if (n.wday < 0 || n.wday > 6) continue;
    int y = 46 + (i - 1) * 10;
    printAt(66, y, DAY_NAMES[n.wday]);
    snprintf(buf, sizeof(buf), "%d/%d", toUnit(n.tempMaxC), toUnit(n.tempMinC));
    printAt(127 - strlen(buf) * 6, y, buf);
  }
}

// Layouts 1-4: current weather on the left, a forecast on the right
static void drawForecastLayout(const WeatherData& wx, const IconScene& scene,
                               bool haveTime, const struct tm& timeinfo) {
  drawForecastLeft(wx, scene, haveTime, timeinfo);
  switch (settings.weatherLayout) {
    case 2: drawForecastColumns(wx); break;
    case 3:
      if (wx.hourStart >= 0) drawForecastHours(wx);
      else drawForecastRows(wx);
      break;
    case 4: drawForecastTomorrow(wx); break;
    default: drawForecastRows(wx); break;
  }

  if (!wifiConnected) drawNoWiFiIcon(0, 0);
}

void displayClockWithWeather() {
  display.setTextColor(DISPLAY_WHITE);
  struct tm timeinfo;
  bool haveTime = getTimeWithTimeout(&timeinfo);
  WeatherData wx = getWeather();
  bool ready = weatherConfigured() && wx.valid;

  IconScene scene;
  scene.code = wx.weatherCode;
  scene.ms = millis();
  scene.t = scene.ms / 1000.0f;
  scene.windy = wx.windKmh >= WWIND_KMH;
  scene.night = false;
  int rise = parseHHMM(wx.sunrise), set = parseHHMM(wx.sunset);
  if (haveTime && rise >= 0 && set > rise) {
    int nowMin = timeinfo.tm_hour * 60 + timeinfo.tm_min;
    scene.night = nowMin < rise || nowMin >= set;
  }

  if (ready && settings.weatherLayout >= 1) {
    drawForecastLayout(wx, scene, haveTime, timeinfo);
    return;
  }

  // --- Time row (size 2, centered) ---
  if (haveTime) {
    int displayHour, displayMin;
    bool isPM;
    formatTimeForDisplay(timeinfo.tm_hour, timeinfo.tm_min, displayHour,
                         displayMin, isPM);
    char timeStr[9];
    char separator = shouldShowColon() ? ':' : ' ';
    sprintf(timeStr, "%02d%c%02d", displayHour, separator, displayMin);

    display.setTextSize(2);
    display.setCursor((SCREEN_WIDTH - 5 * 12) / 2, WTIME_Y);
    display.print(timeStr);
    drawMeridiemIndicator(112, WTIME_Y + 4, isPM);
  } else {
    display.setTextSize(1);
    display.setCursor(20, WTIME_Y + 4);
    display.print(!ntpSynced ? "Syncing time..." : "Time Error");
  }

  // --- Weather block ---
  display.setTextSize(1);

  auto centred = [](int y, const char* s) {
    display.setCursor((SCREEN_WIDTH - (int)strlen(s) * 6) / 2, y);
    display.print(s);
  };
  if (!weatherConfigured()) {
    centred(34, "Weather not set up");
    centred(46, "Set it in the web UI");
    return;
  }
  if (!wx.valid) {
    centred(38, weatherFailed() ? "Weather unavailable" : "Fetching weather...");
    return;
  }

  drawWeatherIcon(WICON_X, WICON_Y, weatherIconFromCode(wx.weatherCode), scene);
  drawTemperature(52, WICON_Y + 3, wx.tempC);

  // --- Details row: high/low + humidity alternating with sun times ---
  char line[30];
  if ((millis() / WDETAIL_SWAP_MS) % 2 == 0) {
    snprintf(line, sizeof(line), "%d\x18 %d\x19  %d%% RH", toUnit(wx.tempMaxC),
             toUnit(wx.tempMinC), wx.humidity);
  } else {
    snprintf(line, sizeof(line), "\x18%s  \x19%s", wx.sunrise, wx.sunset);
  }
  int w = strlen(line) * 6;
  display.setCursor((SCREEN_WIDTH - w) / 2, WDETAIL_Y);
  display.print(line);

  if (!wifiConnected) drawNoWiFiIcon(0, 0);
}
