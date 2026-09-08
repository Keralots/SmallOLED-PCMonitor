/*
 * SmallOLED-PCMonitor - Main Entry Point
 *
 * ESP32-C3 with SSD1306/SH1106 OLED display
 * Dual-mode: PC monitoring metrics OR animated clock displays
 */

// ========== User Configuration ==========
// Edit src/config/user_config.h to configure display type, WiFi, and I2C pins
#include "config/user_config.h"

// Use DEFAULT_DISPLAY_TYPE from user_config.h if DISPLAY_TYPE not already
// defined
#ifndef DISPLAY_TYPE
#define DISPLAY_TYPE DEFAULT_DISPLAY_TYPE
#endif

#include <Adafruit_GFX.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiManager.h>
#if DISPLAY_INTERFACE == 1
#include <SPI.h>
#else
#include <Wire.h>
#endif

#if DISPLAY_TYPE == 2
#include "display/ch1116.h"  // For 1.54" CH1116 (SH1106-compatible, 1-col offset)
#elif DISPLAY_TYPE == 1
#include <Adafruit_SH110X.h> // For 1.3" SH1106
#else
#include <Adafruit_SSD1306.h> // For 0.96" SSD1306
#endif
#include <ArduinoJson.h>
#include <Update.h>
#include <esp_system.h>
#include <esp_task_wdt.h>
#include <time.h>


#include "config/config.h"
#include "utils/utils.h"
#include "timezones.h"
#include "viz/visualizer.h"

// ========== External Objects ==========
extern WiFiUDP udp;              // Defined in network.cpp
extern WebServer server;         // Defined in web.cpp
extern Preferences preferences;  // Defined in settings.cpp

// ========== Display Object ==========
#if DISPLAY_TYPE == 2
  // CH1116 display (1.54", SH1106-compatible with corrected column offset)
  #if DISPLAY_INTERFACE == 1
    Adafruit_CH1116 display(SCREEN_WIDTH, SCREEN_HEIGHT, &SPI, SPI_DC_PIN, SPI_RST_PIN, SPI_CS_PIN);
  #else
    Adafruit_CH1116 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1, DISPLAY_I2C_CLOCK);
  #endif
  #define DISPLAY_WHITE SH110X_WHITE
  #define DISPLAY_BLACK SH110X_BLACK
#elif DISPLAY_TYPE == 1
  // SH1106 display
  #if DISPLAY_INTERFACE == 1
    Adafruit_SH1106G display(SCREEN_WIDTH, SCREEN_HEIGHT, &SPI, SPI_DC_PIN, SPI_RST_PIN, SPI_CS_PIN);
  #else
    Adafruit_SH1106G display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1, DISPLAY_I2C_CLOCK);
  #endif
  #define DISPLAY_WHITE SH110X_WHITE
  #define DISPLAY_BLACK SH110X_BLACK
#else
  // SSD1306 display (also drives 2.42" SSD1309 panels via the same driver)
  #if DISPLAY_INTERFACE == 1
    Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &SPI, SPI_DC_PIN, SPI_RST_PIN, SPI_CS_PIN);
  #else
    Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1, DISPLAY_I2C_CLOCK);
  #endif
  #define DISPLAY_WHITE SSD1306_WHITE
  #define DISPLAY_BLACK SSD1306_BLACK
#endif

// ========== Global State ==========
Settings settings;
MetricData metricData;
bool displayAvailable = false;
bool ntpSynced = false;
unsigned long lastNtpSyncTime = 0;
unsigned long lastReceived = 0;
unsigned long wifiDisconnectTime = 0;
unsigned long nextDisplayUpdate = 0;
bool wifiConnected = false;  // WiFi connection status for icon display
bool httpForceClock = false;  // HTTP override to force clock mode (via /api/mode/clock)
bool httpForceViz = false;    // HTTP override to force the audio visualizer (via /api/mode/viz)
#if VIZ_DEBUG_FB
float measuredFps = 0.0f;     // Frames/s actually pushed to the panel (debug builds only)
#endif

#if TOUCH_BUTTON_ENABLED
bool manualClockMode = false;  // Manual override to force clock mode when PC is online
#endif

// ========== Forward Declarations ==========
// Redundant forward declarations removed (covered by headers)
void displayStats();
void displayStatsCompactGrid();
void displayMetricCompact(Metric *m);
void drawProgressBar(int x, int y, int width, Metric *m);
int getOptimalRefreshRate();
// ========== Module Includes ==========
#include "display/display.h"
#include "clocks/clocks.h"
#include "clocks/clock_globals.h"
#include "metrics/metrics.h"
#include "network/network.h"
#include "web/web.h"


// ========== Helper Functions ==========

const char* getResetReasonName() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:  return "POWERON";
    case ESP_RST_EXT:      return "EXT";
    case ESP_RST_SW:       return "SW";
    case ESP_RST_PANIC:    return "PANIC";
    case ESP_RST_INT_WDT:  return "INT_WDT";
    case ESP_RST_TASK_WDT: return "TASK_WDT";
    case ESP_RST_WDT:      return "WDT";
    case ESP_RST_DEEPSLEEP:return "DEEPSLEEP";
    case ESP_RST_BROWNOUT: return "BROWNOUT";
    case ESP_RST_SDIO:     return "SDIO";
    default:               return "UNKNOWN";
  }
}

// One non-blocking read of the system clock. getLocalTime(info, 0) cannot be
// used for this: it stamps millis(), then loops while elapsed <= budget, so a
// millisecond tick landing in between skips every attempt and reports failure.
// In a render loop that shows up as content vanishing for single frames.
bool peekLocalTime(struct tm *info) {
  time_t now;
  time(&now);
  localtime_r(&now, info);
  return info->tm_year > 120;
}

// Helper function to get time with short timeout
bool getTimeWithTimeout(struct tm *timeinfo, unsigned long timeout_ms) {
  if (!ntpSynced) {
    if (getLocalTime(timeinfo, timeout_ms)) {
      // Verify time is reasonable (year > 2020) before accepting
      if (timeinfo->tm_year > 120) { // tm_year is years since 1900
        ntpSynced = true;
        lastNtpSyncTime = millis();
        Serial.println("NTP successfully synchronized");
        return true;
      }
    }
    return false;
  }
  return getLocalTime(timeinfo, timeout_ms);
}

// Selectable clock styles, in the order the touch button walks them. Ids match
// AnimatedPixelClock so a style ported later keeps the same number on both
// devices; 4 is a legacy alias for 3 and 12-15 are that project's styles we
// have not ported, so neither appears here.
const uint8_t CLOCK_STYLES[] = {0, 1, 2, 3, 5, 6, 7, 8, 9, 10, 11, 16};
const uint8_t CLOCK_STYLE_COUNT = sizeof(CLOCK_STYLES) / sizeof(CLOCK_STYLES[0]);

bool isValidClockStyle(uint8_t id) {
  if (id == 4) return true;  // legacy alias for Space Invaders
  for (uint8_t i = 0; i < CLOCK_STYLE_COUNT; i++) if (CLOCK_STYLES[i] == id) return true;
  return false;
}

uint8_t nextClockStyle(uint8_t current) {
  for (uint8_t i = 0; i < CLOCK_STYLE_COUNT; i++)
    if (CLOCK_STYLES[i] == current) return CLOCK_STYLES[(i + 1) % CLOCK_STYLE_COUNT];
  return CLOCK_STYLES[0];
}

// Single source of truth for mode precedence. The visualizer outranks both
// stats and clock: it is only ever on because something explicitly asked for
// it, and it stops asking on its own once the stream dies.
DisplayMode currentDisplayMode() {
  if (httpForceViz && vizShouldDisplay()) return MODE_VIZ;
#if TOUCH_BUTTON_ENABLED
  if (metricData.online && !manualClockMode && !httpForceClock) return MODE_METRICS;
#else
  if (metricData.online && !httpForceClock) return MODE_METRICS;
#endif
  return MODE_CLOCK;
}

// Returns optimal refresh rate in Hz based on current display mode
int getOptimalRefreshRate() {
  // Checked before the manual-rate override: the visualizer is a transient,
  // explicitly requested mode, and a manual 2 Hz parked for burn-in reasons
  // should not freeze the bars. Capped rather than free-running - these
  // controllers have no double buffering, so pushing frames faster than the
  // panel scans them out shows up as tearing.
  if (currentDisplayMode() == MODE_VIZ) {
    return settings.vizRefreshHz;
  }

  if (settings.refreshRateMode == 1) {
    // Manual mode - use user-specified rate
    return settings.refreshRateHz;
  }

  // Auto mode - adaptive based on content
#if TOUCH_BUTTON_ENABLED
  if (!metricData.online || manualClockMode) {
#else
  if (!metricData.online) {
#endif
    // Clock mode (offline OR manual clock mode)

#if TOUCH_BUTTON_ENABLED
    // Immediately boost to 40 Hz when in manual clock mode for animated clocks
    if (manualClockMode && settings.boostAnimationRefresh &&
        (settings.clockStyle == 0 || settings.clockStyle == 3 ||
         settings.clockStyle == 4 || settings.clockStyle == 5 ||
         settings.clockStyle == 6 || settings.clockStyle == 7 ||
         settings.clockStyle == 8 || settings.clockStyle == 9 ||
         settings.clockStyle == 10 || settings.clockStyle == 11 ||
         settings.clockStyle == 16)) {
      return 60; // Instant boost for smooth manual clock mode
    }
#endif

    // Check for animation boost (smooth animations during active motion)
    if (settings.boostAnimationRefresh && isAnimationActive()) {
      // Animation is happening - boost to 40 Hz for silky smooth motion!
      return 60;
    }

    if (settings.clockStyle == 0 || settings.clockStyle == 3 ||
        settings.clockStyle == 4 || settings.clockStyle == 5 ||
        settings.clockStyle == 6 || settings.clockStyle == 7 ||
        settings.clockStyle == 8 || settings.clockStyle == 9 ||
        settings.clockStyle == 10 || settings.clockStyle == 11 ||
        settings.clockStyle == 16) {
      // Animated clocks (Mario, Space Invaders, Space Ship, Pong, Pac-Man, Snake, Tetris, Cycle, Asteroids, Dino)
      return 20; // 20 Hz keeps character movement smooth
    } else {
      // Static clocks (Standard, Large)
      return 2; // 2 Hz is plenty for clock that updates once/second
    }
  } else {
    // Metrics mode (online)
    return 10; // 10 Hz for PC stats (updates every 500ms from Python)
  }
}

// --- Clock screen cycle state ---
int lastMinuteBlock = -1;
int currentScreen = 0;
bool firstTimeSynced = false;

// Animated clocks fire their minute-change animation at :56 and it runs on into
// the next minute, so switching screens the instant the 5-minute block rolls
// over clipped the animation every single time. Hold the switch back a few
// seconds and let it land first.
const int CYCLE_MIN_SEC = 10;  // grace for the outgoing animation to finish
const int CYCLE_MAX_SEC = 30;  // cap so a stuck override can never wedge the cycle

void cycleClockScreens() {
    struct tm timeinfo;

    // Advance the cycle only when we have valid time. When time is not yet
    // available the per-screen draw functions below render their own
    // "Syncing time..." message, so the display is never left blank.
    if (getTimeWithTimeout(&timeinfo)) {
        // Determine which 5-minute block we are in
        int minuteBlock = timeinfo.tm_min / 5;

        // First valid time -> initialize block WITHOUT advancing screen
        if (!firstTimeSynced) {
            lastMinuteBlock = minuteBlock;
            firstTimeSynced = true;
        }

        // After that, normal cycling. The block change is not consumed until
        // the switch actually happens, so the condition simply stays true until
        // the grace window opens. time_overridden covers every animated style
        // except Pong, whose transition is long finished by CYCLE_MIN_SEC.
        if (minuteBlock != lastMinuteBlock &&
            timeinfo.tm_sec >= CYCLE_MIN_SEC &&
            (!time_overridden || timeinfo.tm_sec >= CYCLE_MAX_SEC)) {
            lastMinuteBlock = minuteBlock;
            currentScreen = (currentScreen + 1) % 11; // Cycle through all 11 clock styles
            resetClockAnimationState(); // Reset animation state when changing screens
        }
    }

    // Draw the current screen (each draw function handles the no-time case)
    switch (currentScreen) {
        case 0: displayStandardClock(); break;
        case 1: displayClockWithMario(); break;
        case 2: displayClockWithSpaceInvader(); break;
        case 3: displayLargeClock(); break;
        case 4: displayClockWithPong(); break;
        case 5: displayClockWithPacman(); break;
        case 6: displayClockWithSnake(); break;
        case 7: displayClockWithTetris(); break;
        case 8: displayClockWithAsteroids(); break;
        case 9: displayClockWithDino(); break;
        case 10: displayClockWithTron(); break;
    }
}

// ========== setup() ==========
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.printf("Boot reason: %s\n", getResetReasonName());

  // Load settings from flash
  loadSettings();

  // Initialize display
  displayAvailable = initDisplay();

  // Apply saved brightness setting
  if (displayAvailable) {
    applyDisplayBrightness();
  }

#if LED_PWM_ENABLED
  // Initialize LED PWM night light
  initLEDPWM();
  setLEDBrightness(settings.ledBrightness);
#endif

  if (!displayAvailable) {
    Serial.println("WARNING: Display not available, continuing without display");
  } else {
    display.clearDisplay();
    display.setTextColor(DISPLAY_WHITE);
    display.setTextSize(1);
    display.setCursor(10, 20);
    display.println("PC Monitor");
    display.setCursor(10, 35);
    display.println("Starting...");
    display.display();
  }

  // Check if hardcoded WiFi credentials are provided
  bool useManualWiFi = (strlen(HARDCODED_WIFI_SSID) > 0);

  if (useManualWiFi) {
    Serial.println("\n*** USING HARDCODED WIFI CREDENTIALS ***");
    if (!connectManualWiFi(HARDCODED_WIFI_SSID, HARDCODED_WIFI_PASSWORD)) {
      Serial.println("Manual WiFi connection failed!");
      Serial.println("Falling back to WiFiManager portal...");
      useManualWiFi = false;
    }
  }

#if BLE_SETUP_ENABLED
  // BLE provisioning path: fast-connect with saved creds, or BLE, or AP mode fallback
  bool bleHandled = false;
  if (!useManualWiFi) {
    if (tryConnectSavedWiFi()) {
      // Saved credentials worked — set up UDP + mDNS directly (skip WiFiManager)
      WiFi.setTxPower(WIFI_STA_TX_POWER);
      udp.begin(UDP_PORT);
      initMDNS();
      bleHandled = true;
    } else if (runBleProvisioning()) {
      // BLE provisioning succeeded — WiFi already connected inside runBleProvisioning()
      WiFi.setTxPower(WIFI_STA_TX_POWER);
      udp.begin(UDP_PORT);
      initMDNS();
      bleHandled = true;
    }
    // If neither worked: bleHandled = false → initNetwork() below (AP mode fallback)
  }
  if (!bleHandled && !useManualWiFi) {
    initNetwork();
  }
#else
  if (!useManualWiFi) {
    initNetwork();
  }
#endif

  // Keep the radio awake: WiFi modem sleep delays inbound ACKs to the beacon
  // interval, which stalls large web-page transfers for seconds. Power draw
  // is irrelevant next to the LED matrix.
  WiFi.setSleep(false);

  // Initialize NTP
  initNTP();

  // Apply the dimming schedule as soon as there is a clock to evaluate it
  // against. Boot brightness above is applied before WiFi/NTP, so without this
  // a restart inside a scheduled-off window lit the panel until the first
  // periodic check a minute later.
  refreshDisplayBrightnessNow();

  // Initialize WiFi connection status flag
  wifiConnected = (WiFi.status() == WL_CONNECTED);

  // Configure hardware watchdog timer
  esp_task_wdt_init(15, true);
  esp_task_wdt_add(NULL);

  // Initialize metricData
  metricData.count = 0;
  metricData.online = false;
  metricData.status = 0;  // No status received yet
  Serial.println("Waiting for PC stats data...");

  // Setup web server
  setupWebServer();

#if TOUCH_BUTTON_ENABLED
  // Initialize touch button
  initTouchButton();
#endif

  // Show IP address for 5 seconds (configurable via web interface). Skipped
  // when the schedule wants the panel dark, so a restart at night does not
  // light the room for five seconds.
  if (displayAvailable && settings.showIPAtBoot && !scheduledDisplayIsOff()) {
    displayConnected();
    delay(5000);
  }
}

// ========== loop() ==========
void loop() {
  // Feed watchdog
  esp_task_wdt_reset();

#if TOUCH_BUTTON_ENABLED
  updateTemporaryDisplayWake();
#endif

  // Check and apply scheduled brightness (time-based dimming)
  checkScheduledBrightness();

  // Handle web server requests
  server.handleClient();

  // Handle UDP packets - always process to track PC online status accurately
  handleUDP();

#if TOUCH_BUTTON_ENABLED
  // Handle touch button gestures
#if LED_PWM_ENABLED
  handleTouchLED(); // Hold > 1s: ramp LED brightness up/down
#endif
  // Regular short press (mode toggle / clock style cycle)
  if (checkTouchButtonPressed()) {
    if (!handleTemporaryDisplayWake()) {
      // The visualizer joins the tap cycle only while audio is actually
      // arriving - otherwise a tap would land on a "No audio data" screen the
      // user cannot do anything about. One more tap always leaves it.
      if (httpForceViz) {
        httpForceViz = false;
        Serial.println("Touch button: Leaving visualizer");
      } else if (vizRecentEnough(10000)) {
        httpForceViz = true;
        vizNoteForced();
        Serial.println("Touch button: Entering visualizer (audio streaming)");
      } else if (manualClockMode) {
        // Check if PC is currently online (UDP is always processed, so status is accurate)
        if (metricData.online) {
          // PC is online - exit manual clock mode to show PC metrics
          manualClockMode = false;
          Serial.println("Touch button: Exiting manual clock mode (PC is online)");
        } else {
          // PC is offline (timeout triggered) - cycle through clock styles
          settings.clockStyle = nextClockStyle(settings.clockStyle);
          resetClockAnimationState();
          Serial.print("Touch button: PC offline, cycling clock style -> ");
          Serial.println(settings.clockStyle);
        }
      } else if (metricData.online) {
        // PC is online - enter manual clock mode
        manualClockMode = true;
        Serial.println("Touch button: Entering manual clock mode (PC is online)");
      } else {
        // PC is offline - cycle through clock styles
        settings.clockStyle = nextClockStyle(settings.clockStyle);
        resetClockAnimationState();
        Serial.print("Touch button: Clock style -> ");
        Serial.println(settings.clockStyle);
      }
    }
  }
#endif

  // Check timeout
  if (millis() - lastReceived > TIMEOUT && metricData.online) {
    metricData.online = false;
#if TOUCH_BUTTON_ENABLED
    // Reset manual clock mode so PC metrics auto-show when PC comes back online
    manualClockMode = false;
#endif
    Serial.println("PC stats timeout - switching to clock mode");
  }

  // Retry NTP sync periodically if not synced
  if (!ntpSynced && millis() - lastNtpSyncTime > 30000) {
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 100)) {
      if (timeinfo.tm_year > 120) {
        ntpSynced = true;
        lastNtpSyncTime = millis();
        Serial.println("NTP sync successful (retry)");
      }
    } else {
      applyTimezone();  // SNTP client might be dead - restart it
      Serial.println("NTP retry: restarted SNTP client");
    }
    lastNtpSyncTime = millis();
  }

  // Periodic NTP re-sync even when already synced (safety net). SNTP keeps the
  // system clock valid between refreshes, so refresh the timezone/SNTP client
  // without clearing ntpSynced - dropping it would needlessly re-anchor the
  // clocks (below) every hour and could flash "Syncing time..." for a frame.
  if (ntpSynced && millis() - lastNtpSyncTime > NTP_RESYNC_INTERVAL) {
    applyTimezone();
    lastNtpSyncTime = millis();
    Serial.println("Periodic NTP re-sync triggered");
  }

  // Re-anchor the animated clocks when NTP first becomes valid (or after a
  // reconnect resync). At boot the clocks seed displayed_hour/min with a 00:00
  // fallback; if the first sync lands inside a clock's minute-change window the
  // animation advances from 00:00 instead of jumping to the real time, leaving
  // the clock stuck near 00:00 until a reboot or clock-cycle. Resetting the
  // animation state clears the stale time override, and syncDisplayedTime()
  // forces displayed_hour/min to match reality.
  static bool prevNtpSynced = false;
  if (ntpSynced && !prevNtpSynced) {
    resetClockAnimationState();
    struct tm now_tm;
    if (getLocalTime(&now_tm, 10)) {
      syncDisplayedTime(&now_tm);
    }
    // Time just became usable - settle the dimming schedule now rather than
    // waiting for the next periodic check.
    refreshDisplayBrightnessNow();
  }
  prevNtpSynced = ntpSynced;

  // Display update with adaptive refresh rate
  int targetHz = getOptimalRefreshRate();
  unsigned long frameInterval = 1000 / targetHz;

  if (millis() >= nextDisplayUpdate && displayAvailable && !isDisplayForcedOff()) {
    nextDisplayUpdate = millis() + frameInterval;

    display.clearDisplay();

    DisplayMode mode = currentDisplayMode();
    bool showStats = mode == MODE_METRICS;

    // Show error status if PC is connected but LHM has issues
    if (mode == MODE_VIZ) {
      displayVisualizer();
    } else if (showStats && metricData.status != STATUS_OK && metricData.status != 0) {
      displayErrorStatus(metricData.status);
    } else if (showStats) {
      displayStats();
    } else {
      switch (settings.clockStyle) {
      case 0:
        displayClockWithMario();
        break;
      case 1:
        displayStandardClock();
        break;
      case 2:
        displayLargeClock();
        break;
      case 3:
      case 4:
        displayClockWithSpaceInvader();
        break;
      case 5:
        displayClockWithPong();
        break;
      case 6:
        displayClockWithPacman();
        break;
      case 7:
        displayClockWithSnake();
        break;
      case 8:
        displayClockWithTetris();
        break;
      case 9:
        cycleClockScreens();
        break;
      case 10:
        displayClockWithAsteroids();
        break;
      case 11:
        displayClockWithDino();
        break;
      case 16:
        displayClockWithTron();
        break;
      default:
        displayStandardClock();
        break;
      }
    }

    display.display();

#if VIZ_DEBUG_FB
    // Frames actually pushed to the panel over the last second. The frame loop
    // is bus-limited, so the configured refresh rate is an upper bound and not
    // a promise - this is the only honest way to know what the panel does.
    {
      static unsigned long fpsWindowStart = 0;
      static uint16_t fpsFrames = 0;
      fpsFrames++;
      unsigned long nowMs = millis();
      if (nowMs - fpsWindowStart >= 1000) {
        measuredFps = fpsFrames * 1000.0f / (nowMs - fpsWindowStart);
        fpsFrames = 0;
        fpsWindowStart = nowMs;
      }
    }
#endif
  }

  // WiFi reconnection handling
  handleWiFiReconnection();
}
