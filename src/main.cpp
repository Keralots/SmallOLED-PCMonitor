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
#include <Wire.h>

#if DISPLAY_TYPE == 1
#include <Adafruit_SH110X.h> // For 1.3" SH1106
#else
#include <Adafruit_SSD1306.h> // For 0.96" SSD1306
#endif
#include <ArduinoJson.h>
#include <Update.h>
#include <esp_task_wdt.h>
#include <time.h>


#include "config/config.h"
#include "utils/utils.h"

// ========== External Objects ==========
extern WiFiUDP udp;              // Defined in network.cpp
extern WebServer server;         // Defined in web.cpp
extern Preferences preferences;  // Defined in settings.cpp

// ========== Display Object ==========
#if DISPLAY_TYPE == 1
Adafruit_SH1106G display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
#define DISPLAY_WHITE SH110X_WHITE
#define DISPLAY_BLACK SH110X_BLACK
#else
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
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
bool allSpaceFragmentsInactive(); // Required if not in clocks.h

// Mario clock helpers
void triggerDigitBounce(int digitIndex);
void updateDigitBounce();
void calculateTargetDigits(int current_hour, int current_min);
void updateMarioAnimation(struct tm *timeinfo);
void drawMario(int x, int y, bool facingRight, int frame, bool jumping);

// Space clock helpers
void updateSpaceAnimation(struct tm *timeinfo);
void drawSpaceInvader();
void drawSpaceship();
void triggerSpaceExplosion(int digitIndex);
void handleSpacePatrolState();
void handleSpaceSlidingState();
void handleSpaceShootingState();
void handleSpaceExplodingState();
void handleSpaceMovingNextState();
void handleSpaceReturningState();

// Pong clock helpers
void initPongAnimation();
void updatePongAnimation(struct tm *timeinfo);
void drawPongBall();
void drawPongDigits();
void drawBreakoutPaddle();
void drawPongFragments();
void updatePongBall(int ballIndex);
void checkPongCollisions(int ballIndex);
void updateBreakoutPaddle();
void updatePongFragments();
void updateAssemblyFragments();
void updateDigitTransitions();
void updateDigitBouncePong();
void spawnPongBall(int ballIndex);
void triggerDigitTransition(int digitIndex, char oldChar, char newChar);

// Pac-Man clock helpers
void updatePacmanAnimation(struct tm *timeinfo);
void updatePacmanPatrol();
void updatePacmanEating();
void startEatingDigit(uint8_t digitIndex, uint8_t digitValue);
void finishEatingDigit();
void generatePellets();
void updatePellets();
void drawPellets();
void drawPacman(int x, int y, int direction, int mouthFrame);
void updateSpecificDigit(uint8_t digitIndex, uint8_t newValue);

// ========== Module Includes ==========
#include "display/display.h"
#include "clocks/clocks.h"
#include "clocks/clock_globals.h"
#include "metrics/metrics.h"
#include "network/network.h"
#include "web/web.h"


// ========== Helper Functions ==========

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

// Returns optimal refresh rate in Hz based on current display mode
int getOptimalRefreshRate() {
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
         settings.clockStyle == 6)) {
      return 40; // Instant boost for smooth manual clock mode
    }
#endif

    // Check for animation boost (smooth animations during active motion)
    if (settings.boostAnimationRefresh && isAnimationActive()) {
      // Animation is happening - boost to 40 Hz for silky smooth motion!
      return 40;
    }

    if (settings.clockStyle == 0 || settings.clockStyle == 3 ||
        settings.clockStyle == 4 || settings.clockStyle == 5 ||
        settings.clockStyle == 6) {
      // Animated clocks (Mario, Space Invaders, Space Ship, Pong, Pac-Man)
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

// ========== setup() ==========
void setup() {
  Serial.begin(115200);
  delay(1000);

  // Load settings from flash
  loadSettings();

  // Initialize display
  displayAvailable = initDisplay();

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

  if (!useManualWiFi) {
    initNetwork();
  }

  // Initialize NTP
  initNTP();

  // Configure hardware watchdog timer
  esp_task_wdt_init(15, true);
  esp_task_wdt_add(NULL);

  // Initialize metricData
  metricData.count = 0;
  metricData.online = false;
  Serial.println("Waiting for PC stats data...");

  // Setup web server
  setupWebServer();

#if TOUCH_BUTTON_ENABLED
  // Initialize touch button
  initTouchButton();
#endif

  // Show IP address for 5 seconds
  if (displayAvailable) {
    displayConnected();
    delay(5000);
  }
}

// ========== loop() ==========
void loop() {
  // Feed watchdog
  esp_task_wdt_reset();

  // Handle web server requests
  server.handleClient();

  // Handle UDP packets - always process to track PC online status accurately
  handleUDP();

#if TOUCH_BUTTON_ENABLED
  // Handle touch button press
  if (checkTouchButtonPressed()) {
    if (manualClockMode) {
      // Check if PC is currently online (UDP is always processed, so status is accurate)
      if (metricData.online) {
        // PC is online - exit manual clock mode to show PC metrics
        manualClockMode = false;
        Serial.println("Touch button: Exiting manual clock mode (PC is online)");
      } else {
        // PC is offline (timeout triggered) - cycle through clock styles
        settings.clockStyle = (settings.clockStyle + 1) % 7;
        Serial.print("Touch button: PC offline, cycling clock style -> ");
        Serial.println(settings.clockStyle);
      }
    } else if (metricData.online) {
      // PC is online - enter manual clock mode
      manualClockMode = true;
      Serial.println("Touch button: Entering manual clock mode (PC is online)");
    } else {
      // PC is offline - cycle through clock styles
      settings.clockStyle = (settings.clockStyle + 1) % 7;
      Serial.print("Touch button: Clock style -> ");
      Serial.println(settings.clockStyle);
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
    }
    lastNtpSyncTime = millis();
  }

  // Display update with adaptive refresh rate
  int targetHz = getOptimalRefreshRate();
  unsigned long frameInterval = 1000 / targetHz;

  if (millis() >= nextDisplayUpdate && displayAvailable) {
    nextDisplayUpdate = millis() + frameInterval;

    display.clearDisplay();

#if TOUCH_BUTTON_ENABLED
    if (metricData.online && !manualClockMode) {
#else
    if (metricData.online) {
#endif
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
      default:
        displayStandardClock();
        break;
      }
    }

    display.display();
  }

  // WiFi reconnection handling
  handleWiFiReconnection();
}
