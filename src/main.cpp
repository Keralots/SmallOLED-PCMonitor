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


// ========== WiFi Portal Configuration ==========
#ifndef AP_NAME
#define AP_NAME "PCMonitor-Setup"
#endif
#ifndef AP_PASSWORD
#define AP_PASSWORD "monitor123"
#endif

// ========== Optional Hardcoded WiFi (for modules with faulty AP) ==========
const char *HARDCODED_SSID = "";
const char *HARDCODED_PASSWORD = "";

// ========== UDP Configuration ==========
extern WiFiUDP udp; // Defined in network.cpp

// ========== Web Server ==========
extern WebServer server;        // Defined in web.cpp
extern Preferences preferences; // Defined in settings.cpp

// ========== Display Configuration ==========
#define SDA_PIN 8
#define SCL_PIN 9
#if DISPLAY_TYPE == 1
Adafruit_SH1106G display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
#define DISPLAY_WHITE SH110X_WHITE
#define DISPLAY_BLACK SH110X_BLACK
#else
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
#define DISPLAY_WHITE SSD1306_WHITE
#define DISPLAY_BLACK SSD1306_BLACK
#endif

// ========== NTP Configuration ==========
const char *ntpServer = "pool.ntp.org";

// ========== Global State ==========
Settings settings;
MetricData metricData;
bool displayAvailable = false;
bool ntpSynced = false;
unsigned long lastNtpSyncTime = 0;
unsigned long lastReceived = 0;
unsigned long wifiDisconnectTime = 0;
unsigned long nextDisplayUpdate = 0;

// ========== Digit X positions for time display ==========
// Standard digit positions for Mario, Standard, Large, Space, Pong clocks
// (evenly spaced 18px)
extern const int DIGIT_X[5] = {19, 37, 55, 73, 91};

// Progressive fragmentation: spawn 25%, then 50%, then 25%
extern const float FRAGMENT_SPAWN_PERCENT[3] = {0.25, 0.50, 0.25};

// ========== Mario Clock Globals ==========
MarioState mario_state = MARIO_IDLE;
float mario_x = -15;
float mario_jump_y = 0.0;
float jump_velocity = 0.0;
int mario_base_y = 62;
bool mario_facing_right = true;
int mario_walk_frame = 0;
unsigned long last_mario_update = 0;
int displayed_hour = 0;
int displayed_min = 0;
bool time_overridden = false;
int last_minute = -1;
bool animation_triggered = false;
bool digit_bounce_triggered = false;
int num_targets = 0;
int target_x_positions[4] = {0};
int target_digit_index[4] = {0};
int target_digit_values[4] = {0};
int current_target_index = 0;
float digit_offset_y[5] = {0};
float digit_velocity[5] = {0};

// ========== Space Clock Globals ==========
SpaceState space_state = SPACE_PATROL;
float space_x = 64.0;
const float space_y = 56; // Fixed Y position at bottom
int space_anim_frame = 0;
int space_patrol_direction = 1;
unsigned long last_space_update = 0;
unsigned long last_space_sprite_toggle = 0;
Laser space_laser = {0, 0, 0, false, -1};
SpaceFragment space_fragments[MAX_SPACE_FRAGMENTS];
int space_explosion_timer = 0;

// ========== Pong Clock Globals ==========
PongBall pong_balls[MAX_PONG_BALLS];
SpaceFragment pong_fragments[MAX_PONG_FRAGMENTS];
FragmentTarget fragment_targets[MAX_PONG_FRAGMENTS];
DigitTransition digit_transitions[5];
BreakoutPaddle breakout_paddle = {64, 20, 64, 3}; // x, width, target_x, speed
unsigned long last_pong_update = 0;
bool ball_stuck_to_paddle[MAX_PONG_BALLS] = {false};
unsigned long ball_stick_release_time[MAX_PONG_BALLS] = {0};
int ball_stuck_x_offset[MAX_PONG_BALLS] = {0};
int paddle_last_x = 64;

// ========== Pac-Man Clock Globals ==========
PacmanState pacman_state = PACMAN_PATROL;
float pacman_x = 30.0;
float pacman_y = 56.0; // Bottom patrol line
int pacman_direction = 1;
int pacman_mouth_frame = 0;
unsigned long last_pacman_update = 0;
unsigned long last_pacman_mouth_toggle = 0;
int last_minute_pacman = -1;
bool pacman_animation_triggered = false;
bool digit_being_eaten[5] = {false};
int digit_eaten_rows_left[5] = {0};
int digit_eaten_rows_right[5] = {0};
PatrolPellet patrol_pellets[MAX_PATROL_PELLETS];
int num_pellets = 0;
uint8_t digitEatenPellets[5][5] = {{0}};
uint8_t current_eating_digit_index = 0;
uint8_t current_eating_digit_value = 0;
uint8_t current_path_step = 0;
float pellet_eat_distance = 0.0;
uint8_t target_digit_queue[4] = {0};
uint8_t target_digit_new_values[4] = {0};
uint8_t target_queue_length = 0;
uint8_t target_queue_index = 0;
uint8_t pending_digit_index = 255;
uint8_t pending_digit_value = 0;

// ========== Forward Declarations ==========
// Redundant forward declarations removed (covered by headers)
void displayStats();
void displayStatsCompactGrid();
void displayMetricCompact(Metric *m);
void drawProgressBar(int x, int y, int width, Metric *m);
bool isAnimationActive();
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
#include "clocks/clocks.h"
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

// Detects if any animation is currently active for refresh rate boosting
bool isAnimationActive() {
  // Only check for animations in clock mode (when offline)
  if (metricData.online) {
    return false; // No animations in metrics mode
  }

  // Check Mario clock animations (clockStyle == 0)
  if (settings.clockStyle == 0) {
    // Check if Mario is jumping
    if (mario_state == MARIO_JUMPING) {
      return true;
    }
    // Check if any digit is bouncing (digit_offset_y != 0)
    for (int i = 0; i < 5; i++) {
      if (digit_offset_y[i] != 0.0) {
        return true;
      }
    }
  }

  // Check Space clock animations (clockStyle == 3 or 4)
  if (settings.clockStyle == 3 || settings.clockStyle == 4) {
    // Check if space character is in action state (not just patrolling)
    if (space_state != SPACE_PATROL) {
      return true;
    }
    // Check if laser is active
    if (space_laser.active) {
      return true;
    }
    // Check if explosion fragments are active
    if (!allSpaceFragmentsInactive()) {
      return true;
    }
  }

  // Check Pong clock animations (clockStyle == 5)
  if (settings.clockStyle == 5) {
    // Pong is always active (ball moving, paddles tracking)
    return true;
  }

  // Check Pac-Man clock animations (clockStyle == 6)
  if (settings.clockStyle == 6) {
    // Always active - Pac-Man is constantly moving and eating pellets
    return true;
  }

  // Standard and Large clocks (clockStyle 1 & 2) have no animations
  return false;
}

// Returns optimal refresh rate in Hz based on current display mode
int getOptimalRefreshRate() {
  if (settings.refreshRateMode == 1) {
    // Manual mode - use user-specified rate
    return settings.refreshRateHz;
  }

  // Auto mode - adaptive based on content
  if (!metricData.online) {
    // Clock mode (offline)

    // Check for animation boost (smooth animations during active motion)
    if (settings.boostAnimationRefresh && isAnimationActive()) {
      // Animation is happening - boost to 40 Hz for silky smooth motion!
      return 40;
    }

    if (settings.clockStyle == 0 || settings.clockStyle == 3 ||
        settings.clockStyle == 4 || settings.clockStyle == 5) {
      // Animated clocks (Mario, Space Invaders, Space Ship, Pong)
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

  Wire.begin(SDA_PIN, SCL_PIN);

  // Attempt display initialization with retries
  for (int attempt = 0; attempt < 3; attempt++) {
#if DISPLAY_TYPE == 1
    byte addrToTry = (attempt == 0) ? 0x3C : 0x3D;
    display.begin(addrToTry);
    display.setContrast(255);
    displayAvailable = true;
    break;
#else
    if (display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
      displayAvailable = true;
      break;
    }
#endif
    delay(500);
  }

  if (!displayAvailable) {
    Serial.println(
        "WARNING: Display not available, continuing without display");
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
  bool useManualWiFi = (strlen(HARDCODED_SSID) > 0);

  if (useManualWiFi) {
    Serial.println("\n*** USING HARDCODED WIFI CREDENTIALS ***");
    if (!connectManualWiFi(HARDCODED_SSID, HARDCODED_PASSWORD)) {
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

  // Handle UDP packets
  int packetSize = udp.parsePacket();
  if (packetSize) {
    static char buffer[2048];
    int len = udp.read(buffer, sizeof(buffer) - 1);
    if (len > 0) {
      buffer[len] = '\0';
      Serial.print("UDP packet: ");
      Serial.print(packetSize);
      Serial.print(" bytes, read: ");
      Serial.print(len);
      Serial.println(" bytes");

      if (packetSize > (int)sizeof(buffer) - 1) {
        Serial.printf(
            "ERROR: Packet %d bytes exceeds buffer %d bytes! Data truncated.\n",
            packetSize, (int)sizeof(buffer));
        udp.flush();
        lastReceived = millis();
      } else {
        parseStats(buffer);
        lastReceived = millis();
      }
    }
  }

  // Check timeout
  if (millis() - lastReceived > TIMEOUT && metricData.online) {
    metricData.online = false;
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

    if (metricData.online) {
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
  if (WiFi.status() != WL_CONNECTED) {
    if (wifiDisconnectTime == 0) {
      wifiDisconnectTime = millis();
      Serial.println("WiFi disconnected, attempting reconnection...");
      WiFi.reconnect();
    }

    if (millis() - wifiDisconnectTime > 60000) {
      Serial.println("WiFi reconnection failed, restarting...");
      ESP.restart();
    }

    if (displayAvailable && (millis() / 500) % 2 == 0) {
      display.clearDisplay();
      display.setTextSize(1);
      display.setCursor(10, 20);
      display.println("WiFi Lost");
      display.setCursor(10, 35);
      display.println("Reconnecting...");
      display.display();
    }
  } else {
    if (wifiDisconnectTime != 0) {
      Serial.println("WiFi reconnected successfully!");
      wifiDisconnectTime = 0;
      ntpSynced = false;
    }
  }
}
