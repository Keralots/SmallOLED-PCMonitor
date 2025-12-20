/*
 * PC Stats Monitor Display - WiFi Portal Version with Web Config
 * ESP32-C3 Super Mini + SSD1306 128x64 OLED
 * 
 * Features:
 * - WiFi setup portal for easy configuration
 * - Web-based settings page (access via ESP32 IP address)
 * - PC stats display when online (CPU, RAM, GPU, Disk, Fan)
 * - Clock + date when offline (Mario / Standard / Large)
 * - Settings saved to flash memory
 * - UDP data reception on port 4210
 * - Configurable timezone and date format
 * 
 * Required Libraries:
 * - WiFiManager by tzapu (install from Library Manager)
 * - Adafruit SSD1306
 * - Adafruit GFX
 * - ArduinoJson
 * - Preferences (built-in)
 * - WebServer (built-in)
 */

#include <WiFi.h>
#include <WiFiManager.h>
#include <WebServer.h>
#include <Preferences.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ArduinoJson.h>
#include <time.h>
#include <esp_task_wdt.h>

// ========== WiFi Portal Configuration ==========
const char* AP_NAME = "PCMonitor-Setup";
const char* AP_PASSWORD = "monitor123";

// ========== Optional Hardcoded WiFi (for modules with faulty AP) ==========
// Leave blank ("") to use WiFiManager portal (default behavior)
// Set your WiFi credentials here to bypass WiFiManager:
const char* HARDCODED_SSID = "";        // Your WiFi network name
const char* HARDCODED_PASSWORD = "";    // Your WiFi password

// ========== UDP Configuration ==========
WiFiUDP udp;
const int UDP_PORT = 4210;

// ========== Web Server ==========
WebServer server(80);
Preferences preferences;

// ========== Display Configuration ==========
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define SDA_PIN 8
#define SCL_PIN 9
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// ========== NTP Time Configuration ==========
const char* ntpServer = "pool.ntp.org";

// ========== Dynamic Metrics System (v2.0) ==========
#define MAX_METRICS 20  // Increased from 12 to support more metrics with companions
#define METRIC_NAME_LEN 11  // 10 chars + null terminator
#define METRIC_UNIT_LEN 5   // 4 chars + null terminator

struct Metric {
  uint8_t id;                     // 1-20
  char name[METRIC_NAME_LEN];     // "CPU%", "FAN1", etc. (from Python)
  char label[METRIC_NAME_LEN];    // Custom label (user editable, preserves '^' for spaces)
  char unit[METRIC_UNIT_LEN];     // "%", "C", "RPM", etc.
  int value;                      // Sensor value
  uint8_t displayOrder;           // Display position (0-19, for sorting in web UI)
  uint8_t companionId;            // ID of companion metric (0 = none, 1-20 = metric ID)
  uint8_t position;               // Display position: 0-11 (0=R1-L, 1=R1-R, 2=R2-L, ..., 11=R6-R), 255=None/Hidden
  uint8_t barPosition;            // Position where progress bar should be displayed (0-11 or 255=None)
  int barMin;                     // Min value for progress bar (default 0)
  int barMax;                     // Max value for progress bar (default 100)
  uint8_t barWidth;               // Bar width in pixels (default 60 for left, 64 for right)
  uint8_t barOffsetX;             // Horizontal offset in pixels from left edge (default 0)
};

struct MetricData {
  Metric metrics[MAX_METRICS];
  uint8_t count;                  // Actual number of metrics received
  char timestamp[6];              // "HH:MM"
  bool online;                    // Connection status
};

MetricData metricData;

// ========== Settings (loaded from flash) ==========
struct Settings {
  int clockStyle;        // 0 = Mario, 1 = Standard, 2 = Large, 3 = Space Invaders (Jumping), 4 = Space Invaders (Sliding)
  int gmtOffset;         // GMT offset in hours (-12 to +14)
  bool daylightSaving;   // Daylight saving time
  bool use24Hour;        // 24-hour format
  int dateFormat;        // 0 = DD/MM/YYYY, 1 = MM/DD/YYYY, 2 = YYYY-MM-DD

  // Clock positioning
  int clockPosition;     // 0 = Center, 1 = Left, 2 = Right
  int clockOffset;       // Horizontal offset in pixels (can be negative or positive)

  // Custom metric labels (preserves '^' character for spacing)
  char metricLabels[MAX_METRICS][METRIC_NAME_LEN];  // Custom display names

  // Metric names (from Python - for validation)
  char metricNames[MAX_METRICS][METRIC_NAME_LEN];   // Original metric names (e.g., "CPU%", "PUMP")

  // Metric display order
  uint8_t metricOrder[MAX_METRICS];  // Display position for each metric

  // Companion metrics (pair metrics on same line)
  uint8_t metricCompanions[MAX_METRICS];  // Companion metric ID (0 = none)

  // Metric position assignment (slot-based: 0-11 for Row1-Left to Row6-Right, 255=None)
  uint8_t metricPositions[MAX_METRICS];  // Display slot position

  // Progress bar settings
  uint8_t metricBarPositions[MAX_METRICS];  // Position where bar should be displayed (0-11 or 255=None)
  int metricBarMin[MAX_METRICS];            // Min value for progress bars
  int metricBarMax[MAX_METRICS];            // Max value for progress bars
  uint8_t metricBarWidths[MAX_METRICS];     // Bar width in pixels
  uint8_t metricBarOffsets[MAX_METRICS];    // Bar horizontal offset in pixels

  // Clock toggle
  bool showClock;        // Show/hide timestamp in metrics display

  // Display layout mode
  int displayRowMode;    // 0 = 5 rows (12px spacing), 1 = 6 rows (10px compact)

  // RPM display format
  bool useRpmKFormat;    // true = "1.8K", false = "1800RPM"
};

Settings settings;

// ========== Legacy PC Stats Structure (for backward compatibility) ==========
struct PCStats {
  float cpu_percent;
  float ram_percent;
  float ram_used_gb;
  float ram_total_gb;
  float disk_percent;
  int cpu_temp;
  int gpu_temp;
  int fan_speed;
  char timestamp[6];
  bool online;
};

PCStats stats;  // Keep for backward compatibility with old Python script
unsigned long lastReceived = 0;
const unsigned long TIMEOUT = 6000;
bool ntpSynced = false;  // Track NTP sync status
unsigned long wifiDisconnectTime = 0;  // Track WiFi disconnect time
const unsigned long WIFI_RECONNECT_TIMEOUT = 30000;  // 30s before restart
bool displayAvailable = false;  // Track if display is working

// ========== Mario Animation Variables ==========
float mario_x = -15;
int mario_base_y = 62;
float mario_jump_y = 0;
bool mario_facing_right = true;
int mario_walk_frame = 0;
unsigned long last_mario_update = 0;
const int MARIO_ANIM_SPEED = 50;

enum MarioState {
  MARIO_IDLE,
  MARIO_WALKING,
  MARIO_JUMPING,
  MARIO_WALKING_OFF
};
MarioState mario_state = MARIO_IDLE;

// Digit targeting
int target_x_positions[4];
int target_digit_index[4];
int target_digit_values[4];  // Store the target value for each digit
int num_targets = 0;
int current_target_index = 0;
int last_minute = -1;
bool animation_triggered = false;

// Jump physics
float jump_velocity = 0;
const float GRAVITY = 0.6;
const float JUMP_POWER = -4.5;
bool digit_bounce_triggered = false;

const int MARIO_HEAD_OFFSET = 10;
const int DIGIT_BOTTOM = 47;

// Displayed time
int displayed_hour = -1;
int displayed_min = -1;
bool time_overridden = false;

// Digit bounce animation
float digit_offset_y[5] = {0, 0, 0, 0, 0};
float digit_velocity[5] = {0, 0, 0, 0, 0};
const float DIGIT_BOUNCE_POWER = -3.5;
const float DIGIT_GRAVITY = 0.6;

const int DIGIT_X[5] = {19, 37, 55, 73, 91};
const int TIME_Y = 26;  // Mario and standard clocks use this position

// ========== Space Invader Animation Variables (Clock Style 3 - Sliding) ==========
enum InvaderState {
  INVADER_PATROL,
  INVADER_SLIDING,
  INVADER_SHOOTING,
  INVADER_EXPLODING_DIGIT,
  INVADER_MOVING_NEXT,
  INVADER_RETURNING
};

// Invader fragment (same structure as ShipFragment)
struct InvaderFragment {
  float x, y;
  float vx, vy;
  bool active;
};

float invader_x = 64;                    // Horizontal position
const float invader_y = 56;              // Fixed Y position (always at bottom)
int invader_anim_frame = 0;              // Sprite animation frame
int invader_patrol_direction = 1;        // 1 = right, -1 = left
InvaderState invader_state = INVADER_PATROL;

// Timing
unsigned long last_invader_update = 0;
unsigned long last_invader_sprite_toggle = 0;

// Movement constants
const float INVADER_PATROL_SPEED = 0.5;       // Slow patrol drift
const float INVADER_SLIDE_SPEED = 2.5;        // Fast slide to target
const int INVADER_PATROL_LEFT = 15;           // Left boundary
const int INVADER_PATROL_RIGHT = 113;         // Right boundary

// Laser system
struct Laser {
  float x, y;
  float length;
  bool active;
  int target_digit_idx;
};

// Invader uses same laser and fragment systems as ship
Laser invader_laser = {0, 0, false, 0, -1};
const float LASER_EXTEND_SPEED = 4.0;
const float LASER_MAX_LENGTH = 30.0;

#define MAX_INVADER_FRAGMENTS 16
InvaderFragment invader_fragments[MAX_INVADER_FRAGMENTS] = {0};
const float INVADER_FRAG_GRAVITY = 0.5;
const float INVADER_FRAG_SPEED = 2.0;
int invader_explosion_timer = 0;
const int INVADER_EXPLOSION_DURATION = 25;

// ========== Space Ship Animation Variables (Clock Style 4) ==========
enum ShipState {
  SHIP_PATROL,
  SHIP_SLIDING,
  SHIP_SHOOTING,
  SHIP_EXPLODING_DIGIT,
  SHIP_MOVING_NEXT,
  SHIP_RETURNING
};

// Ship fragment (same structure as InvaderFragment)
struct ShipFragment {
  float x, y;
  float vx, vy;
  bool active;
};

float ship_x = 64;                    // Horizontal position
const float ship_y = 56;              // Fixed Y position (always at bottom)
int ship_anim_frame = 0;              // Sprite animation frame
int ship_patrol_direction = 1;        // 1 = right, -1 = left
ShipState ship_state = SHIP_PATROL;

// Timing
unsigned long last_ship_update = 0;
unsigned long last_ship_sprite_toggle = 0;
const int SHIP_ANIM_SPEED = 50;            // 50ms = 20 FPS
const int SHIP_SPRITE_TOGGLE_SPEED = 200;  // Slow retro animation

// Movement constants
const float SHIP_PATROL_SPEED = 0.5;       // Slow patrol drift
const float SHIP_SLIDE_SPEED = 2.5;        // Fast slide to target
const int SHIP_PATROL_LEFT = 15;           // Left boundary
const int SHIP_PATROL_RIGHT = 113;         // Right boundary

// Ship uses same laser and fragment systems as invader
Laser ship_laser = {0, 0, 0, false, -1};
ShipFragment ship_fragments[MAX_INVADER_FRAGMENTS] = {0};
int ship_explosion_timer = 0;

// ========== WiFiManager ==========
WiFiManager wifiManager;

// Forward declarations
void loadSettings();
void saveSettings();
void setupWebServer();
void handleRoot();
void handleSave();
void handleReset();
void handleMetricsAPI();
void handleExportConfig();
void handleImportConfig();
void trimTrailingSpaces(char* str);
void convertCaretToSpaces(char* str);
void displaySetupInstructions();
void displayConnecting();
void displayConnected();
void displayClockWithMario();
void displayStandardClock();
void displayLargeClock();
void updateMarioAnimation(struct tm* timeinfo);
void drawMario(int x, int y, bool facingRight, int frame, bool jumping);
void calculateTargetDigits(int hour, int min);
void advanceDisplayedTime();
void updateSpecificDigit(int digitIndex, int newValue);
void updateDigitBounce();
void triggerDigitBounce(int digitIndex);
void drawTimeWithBounce();
void applyTimezone();
void configModeCallback(WiFiManager *myWiFiManager);
void saveConfigCallback();
bool connectManualWiFi(const char* ssid, const char* password);
void parseStats(const char* json);
void displayStats();
void displayMetricCompact(Metric* m);
void drawProgressBar(int x, int y, int width, Metric* m);
void displayClockWithInvader();
void updateInvaderAnimation(struct tm* timeinfo);
void handleInvaderPatrolState();
void handleInvaderSlidingState();
void handleInvaderShootingState();
void handleInvaderExplodingState();
void handleInvaderMovingNextState();
void handleInvaderReturningState();
void drawInvader(int x, int y, int frame);
void drawInvaderLaser(Laser* laser);
void updateInvaderLaser();
void fireInvaderLaser(int target_digit_idx);
void spawnInvaderExplosion(int digitIndex);
void updateInvaderFragments();
void drawInvaderFragments();
bool allInvaderFragmentsInactive();
InvaderFragment* findFreeInvaderFragment();
void displayClockWithShip();
void updateShipAnimation(struct tm* timeinfo);
void handleShipPatrolState();
void handleShipSlidingState();
void handleShipShootingState();
void handleShipExplodingState();
void handleShipMovingNextState();
void handleShipReturningState();
void drawShip(int x, int y, int frame);
void drawShipLaser(Laser* laser);
void updateShipLaser();
void fireShipLaser(int target_digit_idx);
void spawnShipExplosion(int digitIndex);
void updateShipFragments();
void drawShipFragments();
bool allShipFragmentsInactive();
ShipFragment* findFreeShipFragment();

void setup() {
  Serial.begin(115200);
  delay(1000);

  // Serial.println("\n========================================");
  // Serial.println("PC Stats Monitor - Web Config Version");
  // Serial.println("========================================");

  // Load settings from flash
  loadSettings();

  Wire.begin(SDA_PIN, SCL_PIN);

  // Attempt display initialization with retries
  // Serial.println("Initializing display...");
  for (int attempt = 0; attempt < 3; attempt++) {
    if(display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
      displayAvailable = true;
      // Serial.println("Display initialized successfully");
      break;
    }
    // Serial.printf("Display init failed (attempt %d/3)\n", attempt + 1);
    delay(500);
  }

  if (!displayAvailable) {
    Serial.println("WARNING: Display not available, continuing without display");
  } else {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
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
    // Use manual WiFi connection (for modules with faulty AP)
    Serial.println("\n*** USING HARDCODED WIFI CREDENTIALS ***");

    if (!connectManualWiFi(HARDCODED_SSID, HARDCODED_PASSWORD)) {
      Serial.println("Manual WiFi connection failed!");
      Serial.println("Falling back to WiFiManager portal...");
      useManualWiFi = false;  // Fall back to WiFiManager
    }
  }

  if (!useManualWiFi) {
    // Use WiFiManager portal (default behavior)
    // Serial.println("\n\n========== WIFI MANAGER SETUP ==========");
    wifiManager.setConnectTimeout(30);
    // Serial.println("Connect timeout set to 30s");

    wifiManager.setConfigPortalTimeout(180);
    // Serial.println("Portal timeout set to 180s");

    wifiManager.setAPCallback(configModeCallback);
    wifiManager.setSaveConfigCallback(saveConfigCallback);
    wifiManager.setDebugOutput(false);  // Disable verbose WiFiManager debug output
    // Serial.println("Callbacks registered, debug enabled");

    // Serial.println("\nCalling autoConnect...");
    // Serial.flush();

    if (!wifiManager.autoConnect(AP_NAME, AP_PASSWORD)) {
      Serial.println("Failed to connect and hit timeout");
      if (displayAvailable) {
        display.clearDisplay();
        display.setCursor(10, 20);
        display.println("WiFi Timeout!");
        display.setCursor(10, 35);
        display.println("Restarting...");
        display.display();
      }
      delay(3000);
      ESP.restart();
    }
  }

  Serial.println("WiFi Connected!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  // Set WiFi TX power to maximum for better range
  WiFi.setTxPower(WIFI_POWER_19_5dBm);  // Maximum: 19.5 dBm (~89 mW)
  // Serial.println("WiFi TX Power set to maximum (19.5 dBm)");

  // Apply timezone and start NTP (async operation)
  applyTimezone();
  // Serial.println("NTP sync initiated...");
  ntpSynced = false;

  // Display sync status
  if (displayAvailable) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(10, 20);
    display.println("Syncing time...");
    display.display();
  }

  // Attempt initial sync with short retries (max 1 second total)
  struct tm timeinfo;
  for (int i = 0; i < 10; i++) {
    if (getLocalTime(&timeinfo, 100)) {
      ntpSynced = true;
      Serial.println("NTP time synchronized successfully");
      break;
    }
    delay(100);
  }

  if (!ntpSynced) {
    // Serial.println("NTP sync pending, will retry in background");
  }

  // Configure hardware watchdog timer (15 seconds)
  // NOTE: Enabled AFTER WiFi config portal to allow time for user setup
  esp_task_wdt_init(15, true);  // 15s timeout, panic on timeout
  esp_task_wdt_add(NULL);       // Add current task
  // Serial.println("Watchdog enabled (15s)");

  // Start UDP listener
  udp.begin(UDP_PORT);
  Serial.print("UDP listening on port ");
  Serial.println(UDP_PORT);

  // Initialize metricData
  metricData.count = 0;
  metricData.online = false;
  Serial.println("Waiting for PC stats data...");

  // Setup web server for configuration
  setupWebServer();
  // Serial.println("Web server started on port 80");

  // Show IP address for 5 seconds
  if (displayAvailable) {
    displayConnected();
    delay(5000);
  }

  // Serial.println("Setup complete!");
  // Serial.println("========================================");
}

void configModeCallback(WiFiManager *myWiFiManager) {
  Serial.println("Config mode entered");
  Serial.println(WiFi.softAPIP());

  if (displayAvailable) {
    displaySetupInstructions();
  }
}

void saveConfigCallback() {
  // Serial.println("Config saved");
  if (displayAvailable) {
    displayConnecting();
  }
}

// Connect to WiFi using hardcoded credentials (no WiFiManager)
bool connectManualWiFi(const char* ssid, const char* password) {
  // Serial.println("\n========== MANUAL WIFI CONNECTION ==========");
  // Serial.print("Connecting to: ");
  // Serial.println(ssid);

  if (displayAvailable) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(10, 20);
    display.println("Connecting to");
    display.setCursor(10, 35);
    display.println(ssid);
    display.display();
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  int attempts = 0;
  int maxAttempts = 30;  // 30 seconds timeout (30 x 1000ms)

  while (WiFi.status() != WL_CONNECTED && attempts < maxAttempts) {
    delay(1000);
    attempts++;
    // Serial.print(".");

    // Update display with progress
    if (displayAvailable && attempts % 5 == 0) {
      display.clearDisplay();
      display.setTextSize(1);
      display.setCursor(10, 20);
      display.println("Connecting...");
      display.setCursor(10, 35);
      display.print("Attempt: ");
      display.print(attempts);
      display.print("/");
      display.println(maxAttempts);
      display.display();
    }
  }

  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi Connected!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
    return true;
  } else {
    Serial.println("WiFi Connection Failed!");
    return false;
  }
}

void loadSettings() {
  // Try to open preferences namespace (create if doesn't exist)
  if (!preferences.begin("pcmonitor", false)) {  // Read-write mode to create if needed
    Serial.println("WARNING: Failed to open preferences, using defaults");
    // Initialize with defaults
    settings.clockStyle = 0;
    settings.gmtOffset = 1;
    settings.daylightSaving = true;
    settings.use24Hour = true;
    settings.dateFormat = 0;
    settings.clockPosition = 0;  // Center by default
    settings.clockOffset = 0;    // No offset by default
    settings.showClock = true;
    settings.displayRowMode = 0;  // Default: 5 rows with more spacing
    settings.useRpmKFormat = false;  // Default: Full RPM format (1800RPM)
    // Initialize all metrics with defaults
    for (int i = 0; i < MAX_METRICS; i++) {
      settings.metricLabels[i][0] = '\0';  // Empty = use Python name
      settings.metricNames[i][0] = '\0';   // Empty = no stored name
      settings.metricOrder[i] = i;  // Default order
      settings.metricCompanions[i] = 0;  // No companion by default
      settings.metricPositions[i] = 255;  // Default: None/Hidden (user must assign position)
      settings.metricBarPositions[i] = 255;  // Default: No progress bar
      settings.metricBarMin[i] = 0;
      settings.metricBarMax[i] = 100;
      settings.metricBarWidths[i] = 60;  // Default width (60px for left, 64px for right)
      settings.metricBarOffsets[i] = 0;  // Default: no offset
    }
    Serial.println("Settings initialized with defaults");
    return;
  }

  // Check if this is a fresh namespace (no settings saved yet)
  if (!preferences.isKey("clockStyle")) {
    Serial.println("Fresh preferences namespace detected, initializing with defaults...");
    // Write defaults to NVS
    preferences.putInt("clockStyle", 0);
    preferences.putInt("gmtOffset", 1);
    preferences.putBool("dst", true);
    preferences.putBool("use24Hour", true);
    preferences.putInt("dateFormat", 0);
    preferences.putInt("clockPos", 0);  // Center
    preferences.putInt("clockOffset", 0);  // No offset
    preferences.putBool("showClock", true);
    preferences.putInt("rowMode", 0);  // Default: 5 rows
    preferences.putBool("rpmKFormat", false);  // Default: Full RPM format

    // Initialize all metrics with default values
    uint8_t defaultOrder[MAX_METRICS];
    uint8_t defaultCompanions[MAX_METRICS];
    for (int i = 0; i < MAX_METRICS; i++) {
      defaultOrder[i] = i;
      defaultCompanions[i] = 0;  // No companion
    }
    preferences.putBytes("metricOrd", defaultOrder, MAX_METRICS);
    preferences.putBytes("metricComp", defaultCompanions, MAX_METRICS);

    Serial.println("Default settings written to NVS");
  }

  settings.clockStyle = preferences.getInt("clockStyle", 0);  // Default: Mario
  settings.gmtOffset = preferences.getInt("gmtOffset", 1);    // Default: GMT+1
  settings.daylightSaving = preferences.getBool("dst", true); // Default: true
  settings.use24Hour = preferences.getBool("use24Hour", true); // Default: 24h
  settings.dateFormat = preferences.getInt("dateFormat", 0);  // Default: DD/MM/YYYY
  settings.clockPosition = preferences.getInt("clockPos", 0);  // Default: Center
  settings.clockOffset = preferences.getInt("clockOffset", 0);  // Default: No offset
  settings.showClock = preferences.getBool("showClock", true);
  settings.displayRowMode = preferences.getInt("rowMode", 0);  // Default: 5 rows
  settings.useRpmKFormat = preferences.getBool("rpmKFormat", false);  // Default: Full RPM format

  // Note: Visibility is now determined by position (255 = hidden, 0-11 = visible)

  // Load metric display order
  size_t orderSize = preferences.getBytesLength("metricOrd");
  if (orderSize == MAX_METRICS) {
    preferences.getBytes("metricOrd", settings.metricOrder, MAX_METRICS);
    Serial.println("Loaded metric order from NVS");
  } else {
    // Default sequential order if not found
    Serial.println("Initializing metric order to default (0-11)");
    for (int i = 0; i < MAX_METRICS; i++) {
      settings.metricOrder[i] = i;
    }
    preferences.putBytes("metricOrd", settings.metricOrder, MAX_METRICS);
  }

  // Load companion metrics
  size_t companionSize = preferences.getBytesLength("metricComp");
  if (companionSize == MAX_METRICS) {
    preferences.getBytes("metricComp", settings.metricCompanions, MAX_METRICS);
    Serial.println("Loaded metric companions from NVS");
  } else {
    // Default no companions if not found
    Serial.println("Initializing companions to none (0)");
    for (int i = 0; i < MAX_METRICS; i++) {
      settings.metricCompanions[i] = 0;
    }
    preferences.putBytes("metricComp", settings.metricCompanions, MAX_METRICS);
  }

  // Load metric positions
  size_t posSize = preferences.getBytesLength("metricPos");
  if (posSize == MAX_METRICS) {
    preferences.getBytes("metricPos", settings.metricPositions, MAX_METRICS);
    Serial.println("Loaded metric positions from NVS");
  } else {
    // Default: all positions set to None (255)
    Serial.println("Initializing positions to None (255)");
    for (int i = 0; i < MAX_METRICS; i++) {
      settings.metricPositions[i] = 255;  // None/Hidden by default
    }
    preferences.putBytes("metricPos", settings.metricPositions, MAX_METRICS);
  }

  // Load progress bar settings
  size_t barPosSize = preferences.getBytesLength("metricBarPos");
  if (barPosSize == MAX_METRICS) {
    preferences.getBytes("metricBarPos", settings.metricBarPositions, MAX_METRICS);
    preferences.getBytes("barMin", settings.metricBarMin, MAX_METRICS * sizeof(int));
    preferences.getBytes("barMax", settings.metricBarMax, MAX_METRICS * sizeof(int));
    preferences.getBytes("barWidths", settings.metricBarWidths, MAX_METRICS);
    preferences.getBytes("barOffsets", settings.metricBarOffsets, MAX_METRICS);
    Serial.println("Loaded progress bar settings from NVS");
  } else {
    // Default: no progress bars
    for (int i = 0; i < MAX_METRICS; i++) {
      settings.metricBarPositions[i] = 255;  // None
      settings.metricBarMin[i] = 0;
      settings.metricBarMax[i] = 100;
      settings.metricBarWidths[i] = 60;  // Default width
      settings.metricBarOffsets[i] = 0;  // Default: no offset
    }
  }

  // Load custom metric labels
  for (int i = 0; i < MAX_METRICS; i++) {
    String key = "label" + String(i);
    String label = preferences.getString(key.c_str(), "");
    if (label.length() > 0) {
      strncpy(settings.metricLabels[i], label.c_str(), METRIC_NAME_LEN - 1);
      settings.metricLabels[i][METRIC_NAME_LEN - 1] = '\0';
    } else {
      settings.metricLabels[i][0] = '\0';  // Empty = use Python name
    }
  }

  // Load metric names (for validation)
  for (int i = 0; i < MAX_METRICS; i++) {
    String key = "name" + String(i);
    String name = preferences.getString(key.c_str(), "");
    if (name.length() > 0) {
      strncpy(settings.metricNames[i], name.c_str(), METRIC_NAME_LEN - 1);
      settings.metricNames[i][METRIC_NAME_LEN - 1] = '\0';
    } else {
      settings.metricNames[i][0] = '\0';  // Empty = no stored name
    }
  }

  preferences.end();

  Serial.println("Settings loaded (v2.0 - Compact Grid Layout)");
}

void saveSettings() {
  preferences.begin("pcmonitor", false);  // Read-write
  preferences.putInt("clockStyle", settings.clockStyle);
  preferences.putInt("gmtOffset", settings.gmtOffset);
  preferences.putBool("dst", settings.daylightSaving);
  preferences.putBool("use24Hour", settings.use24Hour);
  preferences.putInt("dateFormat", settings.dateFormat);
  preferences.putInt("clockPos", settings.clockPosition);
  preferences.putInt("clockOffset", settings.clockOffset);
  preferences.putBool("showClock", settings.showClock);
  preferences.putInt("rowMode", settings.displayRowMode);
  preferences.putBool("rpmKFormat", settings.useRpmKFormat);

  // Save metric display order
  preferences.putBytes("metricOrd", settings.metricOrder, MAX_METRICS);

  // Save metric companions
  preferences.putBytes("metricComp", settings.metricCompanions, MAX_METRICS);

  // Save metric positions (255 = hidden, 0-11 = visible at position)
  preferences.putBytes("metricPos", settings.metricPositions, MAX_METRICS);

  // Save progress bar settings
  preferences.putBytes("metricBarPos", settings.metricBarPositions, MAX_METRICS);
  preferences.putBytes("barMin", settings.metricBarMin, MAX_METRICS * sizeof(int));
  preferences.putBytes("barMax", settings.metricBarMax, MAX_METRICS * sizeof(int));
  preferences.putBytes("barWidths", settings.metricBarWidths, MAX_METRICS);
  preferences.putBytes("barOffsets", settings.metricBarOffsets, MAX_METRICS);

  // Save custom metric labels
  for (int i = 0; i < MAX_METRICS; i++) {
    String key = "label" + String(i);
    if (settings.metricLabels[i][0] != '\0') {
      preferences.putString(key.c_str(), settings.metricLabels[i]);
    } else {
      preferences.remove(key.c_str());  // Remove if empty
    }
  }

  // Save metric names (for validation)
  for (int i = 0; i < MAX_METRICS; i++) {
    String key = "name" + String(i);
    if (settings.metricNames[i][0] != '\0') {
      preferences.putString(key.c_str(), settings.metricNames[i]);
    } else {
      preferences.remove(key.c_str());  // Remove if empty
    }
  }

  preferences.end();

  Serial.println("Settings saved (v2.0)!");
}

void applyTimezone() {
  long gmtOffset_sec = settings.gmtOffset * 3600;
  int dstOffset_sec = settings.daylightSaving ? 3600 : 0;
  configTime(gmtOffset_sec, dstOffset_sec, ntpServer);
}

// Helper function to get time with short timeout
bool getTimeWithTimeout(struct tm* timeinfo, unsigned long timeout_ms = 100) {
  if (!ntpSynced) {
    if (getLocalTime(timeinfo, timeout_ms)) {
      ntpSynced = true;
      Serial.println("NTP successfully synchronized");
      return true;
    }
    return false;
  }
  return getLocalTime(timeinfo, timeout_ms);
}

void setupWebServer() {
  server.on("/", handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/reset", handleReset);
  server.on("/metrics", handleMetricsAPI);  // New API endpoint
  server.on("/api/export", HTTP_GET, handleExportConfig);
  server.on("/api/import", HTTP_POST, handleImportConfig);
  server.begin();
}

// API endpoint to return current metrics as JSON
void handleMetricsAPI() {
  String json = "{\"metrics\":[";

  for (int i = 0; i < metricData.count; i++) {
    Metric& m = metricData.metrics[i];

    if (i > 0) json += ",";
    json += "{\"id\":" + String(m.id) +
            ",\"name\":\"" + String(m.name) + "\"" +
            ",\"label\":\"" + String(m.label) + "\"" +
            ",\"unit\":\"" + String(m.unit) + "\"" +
            ",\"displayOrder\":" + String(m.displayOrder) +
            ",\"companionId\":" + String(m.companionId) +
            ",\"position\":" + String(m.position) +
            ",\"barPosition\":" + String(m.barPosition) +
            ",\"barMin\":" + String(m.barMin) +
            ",\"barMax\":" + String(m.barMax) +
            ",\"barWidth\":" + String(m.barWidth) +
            ",\"barOffsetX\":" + String(m.barOffsetX) + "}";
  }

  json += "]}";

  server.send(200, "application/json", json);
}

void handleRoot() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>PC Monitor Settings</title>
  <style>
    * { box-sizing: border-box; }
    body {
      font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Arial, sans-serif;
      margin: 0; padding: 20px;
      background: linear-gradient(135deg, #0f0c29 0%, #1a1a2e 50%, #24243e 100%);
      background-attachment: fixed;
      color: #e0e7ff;
      min-height: 100vh;
    }
    .container { max-width: 420px; margin: 0 auto; padding-bottom: 100px; }
    h1 {
      color: #fff;
      text-align: center;
      font-size: 28px;
      font-weight: 700;
      margin: 0 0 8px;
      text-shadow: 0 2px 10px rgba(0,212,255,0.3);
    }
    .card {
      background: rgba(22,33,62,0.6);
      backdrop-filter: blur(10px);
      padding: 20px;
      border-radius: 12px;
      margin-bottom: 15px;
      border: 1px solid rgba(0,212,255,0.15);
      box-shadow: 0 4px 15px rgba(0,0,0,0.2);
    }
    label {
      display: block;
      margin: 15px 0 8px;
      color: #00d4ff;
      font-size: 14px;
      font-weight: 500;
      letter-spacing: 0.3px;
    }
    select, input[type="number"], input[type="text"] {
      width: 100%;
      padding: 12px 14px;
      border: 2px solid rgba(0,212,255,0.2);
      border-radius: 8px;
      background: rgba(15,52,96,0.5);
      color: #fff;
      font-size: 15px;
      transition: all 0.3s ease;
      cursor: pointer;
    }
    select:hover, input[type="number"]:hover, input[type="text"]:hover {
      border-color: rgba(0,212,255,0.4);
      background: rgba(15,52,96,0.7);
    }
    select:focus, input:focus {
      outline: none;
      border-color: #00d4ff;
      background: rgba(15,52,96,0.8);
      box-shadow: 0 0 0 3px rgba(0,212,255,0.1);
    }
    input[type="checkbox"] {
      appearance: none;
      width: 20px;
      height: 20px;
      border: 2px solid rgba(0,212,255,0.4);
      border-radius: 5px;
      background: rgba(15,52,96,0.5);
      cursor: pointer;
      position: relative;
      transition: all 0.3s ease;
      flex-shrink: 0;
    }
    input[type="checkbox"]:hover {
      border-color: #00d4ff;
      transform: scale(1.05);
    }
    input[type="checkbox"]:checked {
      background: linear-gradient(135deg, #00d4ff 0%, #0096ff 100%);
      border-color: #00d4ff;
    }
    input[type="checkbox"]:checked::after {
      content: '✓';
      position: absolute;
      color: #0f0c29;
      font-size: 14px;
      font-weight: bold;
      top: 50%;
      left: 50%;
      transform: translate(-50%, -50%);
    }
    button {
      width: 100%;
      padding: 14px;
      margin-top: 20px;
      border: none;
      border-radius: 8px;
      font-size: 16px;
      font-weight: 600;
      cursor: pointer;
      transition: all 0.3s ease;
      text-transform: uppercase;
      letter-spacing: 0.5px;
    }
    .save-btn {
      background: linear-gradient(135deg, #00d4ff 0%, #0096ff 100%);
      color: #0f0c29;
      box-shadow: 0 4px 15px rgba(0,212,255,0.3);
    }
    .save-btn:hover {
      transform: translateY(-2px);
      box-shadow: 0 6px 20px rgba(0,212,255,0.4);
    }
    .save-btn:active { transform: translateY(0); }
    .reset-btn {
      background: linear-gradient(135deg, #ff6b6b 0%, #ee5a52 100%);
      color: #fff;
      box-shadow: 0 4px 15px rgba(255,107,107,0.2);
    }
    .reset-btn:hover {
      transform: translateY(-2px);
      box-shadow: 0 6px 20px rgba(255,107,107,0.3);
    }
    .reset-btn:active { transform: translateY(0); }
    .info { text-align: center; color: #94a3b8; font-size: 12px; margin-top: 20px; }
    .status {
      background: rgba(15,52,96,0.4);
      padding: 12px;
      border-radius: 10px;
      text-align: center;
      margin-bottom: 20px;
      border: 1px solid rgba(0,212,255,0.2);
      font-size: 14px;
    }

    /* Collapsible sections */
    .section-header {
      background: linear-gradient(135deg, rgba(15,52,96,0.6) 0%, rgba(26,77,122,0.4) 100%);
      padding: 16px 18px;
      border-radius: 10px;
      cursor: pointer;
      margin-bottom: 10px;
      user-select: none;
      display: flex;
      justify-content: space-between;
      align-items: center;
      border: 1px solid rgba(0,212,255,0.15);
      transition: all 0.3s ease;
    }
    .section-header:hover {
      background: linear-gradient(135deg, rgba(15,52,96,0.8) 0%, rgba(26,77,122,0.6) 100%);
      transform: translateX(4px);
      border-color: rgba(0,212,255,0.3);
    }
    .section-header h3 {
      margin: 0;
      color: #00d4ff;
      font-size: 16px;
      font-weight: 600;
    }
    .section-arrow {
      font-size: 14px;
      transition: transform 0.3s ease;
      color: #00d4ff;
    }
    .section-arrow.collapsed { transform: rotate(-90deg); }
    .section-content {
      max-height: 10000px;
      overflow: visible;
      transition: max-height 0.3s ease, opacity 0.3s ease;
      opacity: 1;
    }
    .section-content.collapsed {
      max-height: 0;
      overflow: hidden;
      opacity: 0;
    }

    /* Config management buttons */
    .config-buttons { display: grid; grid-template-columns: 1fr 1fr; gap: 10px; margin-bottom: 20px; }
    .export-btn {
      background: linear-gradient(135deg, #10b981 0%, #059669 100%);
      color: #fff;
      padding: 12px;
      font-size: 14px;
      margin-top: 0;
      border-radius: 8px;
      font-weight: 600;
      box-shadow: 0 4px 12px rgba(16,185,129,0.2);
      transition: all 0.3s ease;
    }
    .export-btn:hover {
      transform: translateY(-2px);
      box-shadow: 0 6px 16px rgba(16,185,129,0.3);
    }
    .import-btn {
      background: linear-gradient(135deg, #3b82f6 0%, #2563eb 100%);
      color: #fff;
      padding: 12px;
      font-size: 14px;
      margin-top: 0;
      border-radius: 8px;
      font-weight: 600;
      box-shadow: 0 4px 12px rgba(59,130,246,0.2);
      transition: all 0.3s ease;
    }
    .import-btn:hover {
      transform: translateY(-2px);
      box-shadow: 0 6px 16px rgba(59,130,246,0.3);
    }

    /* Sticky save button */
    .sticky-save {
      position: fixed;
      bottom: 0;
      left: 0;
      right: 0;
      background: linear-gradient(to top, rgba(15,12,41,0.98) 0%, rgba(15,12,41,0.95) 100%);
      backdrop-filter: blur(10px);
      padding: 12px 20px;
      box-shadow: 0 -4px 20px rgba(0,0,0,0.4);
      z-index: 1000;
      border-top: 1px solid rgba(0,212,255,0.2);
    }
    .sticky-save .container { max-width: 420px; margin: 0 auto; }
    .sticky-save button { margin-top: 0; }

    /* Hidden file input */
    #importFile { display: none; }

    /* Mobile responsiveness */
    @media (max-width: 480px) {
      body { padding: 12px; }
      .container { padding-bottom: 90px; }
      h1 { font-size: 24px; }
      .card { padding: 16px; }
      .section-header { padding: 14px 16px; }
      .section-header h3 { font-size: 15px; }
      select, input[type="number"], input[type="text"] { font-size: 16px; padding: 11px 12px; }
      button { padding: 13px; font-size: 15px; }
      .sticky-save { padding: 10px 12px; }
    }
    @media (max-width: 360px) {
      h1 { font-size: 22px; }
      .config-buttons { grid-template-columns: 1fr; gap: 8px; }
    }
  </style>
</head>
<body>
  <div class="container">
    <h1>&#128421; PC Monitor</h1>
    <div class="status">
      <strong>IP:</strong> )rawliteral" + WiFi.localIP().toString() + R"rawliteral( | <strong>UDP Port:</strong> 4210
    </div>

    <!-- Config Management -->
    <div class="config-buttons">
      <button type="button" class="export-btn" onclick="exportConfig()">&#128190; Export Config</button>
      <button type="button" class="import-btn" onclick="document.getElementById('importFile').click()">&#128229; Import Config</button>
    </div>
    <input type="file" id="importFile" accept=".json" onchange="importConfig(event)">

    <form action="/save" method="POST">
      <!-- Clock Settings Section -->
      <div class="section-header" onclick="toggleSection('clockSection')">
        <h3>&#128348; Clock Settings</h3>
        <span class="section-arrow">&#9660;</span>
      </div>
      <div id="clockSection" class="section-content">
        <div class="card">
        
        <label for="clockStyle">Idle Clock Style</label>
        <select name="clockStyle" id="clockStyle">
          <option value="0" )rawliteral" + String(settings.clockStyle == 0 ? "selected" : "") + R"rawliteral(>Mario Animation</option>
          <option value="1" )rawliteral" + String(settings.clockStyle == 1 ? "selected" : "") + R"rawliteral(>Standard Clock</option>
          <option value="2" )rawliteral" + String(settings.clockStyle == 2 ? "selected" : "") + R"rawliteral(>Large Clock</option>
          <option value="3" )rawliteral" + String(settings.clockStyle == 3 ? "selected" : "") + R"rawliteral(>Space Invader</option>
          <option value="4" )rawliteral" + String(settings.clockStyle == 4 ? "selected" : "") + R"rawliteral(>Space Ship</option>
        </select>
        
        <label for="use24Hour">Time Format</label>
        <select name="use24Hour" id="use24Hour">
          <option value="1" )rawliteral" + String(settings.use24Hour ? "selected" : "") + R"rawliteral(>24-Hour (14:30)</option>
          <option value="0" )rawliteral" + String(!settings.use24Hour ? "selected" : "") + R"rawliteral(>12-Hour (2:30 PM)</option>
        </select>
        
        <label for="dateFormat">Date Format</label>
        <select name="dateFormat" id="dateFormat">
          <option value="0" )rawliteral" + String(settings.dateFormat == 0 ? "selected" : "") + R"rawliteral(>DD/MM/YYYY</option>
          <option value="1" )rawliteral" + String(settings.dateFormat == 1 ? "selected" : "") + R"rawliteral(>MM/DD/YYYY</option>
          <option value="2" )rawliteral" + String(settings.dateFormat == 2 ? "selected" : "") + R"rawliteral(>YYYY-MM-DD</option>
        </select>
        </div>
      </div>

      <!-- Timezone Section -->
      <div class="section-header" onclick="toggleSection('timezoneSection')">
        <h3>&#127760; Timezone</h3>
        <span class="section-arrow">&#9660;</span>
      </div>
      <div id="timezoneSection" class="section-content">
        <div class="card">
        
        <label for="gmtOffset">GMT Offset (hours)</label>
        <select name="gmtOffset" id="gmtOffset">
)rawliteral";

  // Generate timezone options
  for (int i = -12; i <= 14; i++) {
    String selected = (settings.gmtOffset == i) ? "selected" : "";
    String label = "GMT" + String(i >= 0 ? "+" : "") + String(i);
    html += "<option value=\"" + String(i) + "\" " + selected + ">" + label + "</option>\n";
  }

  html += R"rawliteral(
        </select>

        <label for="dst">Daylight Saving Time</label>
        <select name="dst" id="dst">
          <option value="1" )rawliteral" + String(settings.daylightSaving ? "selected" : "") + R"rawliteral(>Enabled (+1 hour)</option>
          <option value="0" )rawliteral" + String(!settings.daylightSaving ? "selected" : "") + R"rawliteral(>Disabled</option>
        </select>
        </div>
      </div>

      <!-- Display Layout Section -->
      <div class="section-header" onclick="toggleSection('layoutSection')">
        <h3>&#128202; Display Layout</h3>
        <span class="section-arrow">&#9660;</span>
      </div>
      <div id="layoutSection" class="section-content">
        <div class="card">
        <label for="clockPosition">Clock Position</label>
        <select name="clockPosition" id="clockPosition">
          <option value="0" )rawliteral" + String(settings.clockPosition == 0 ? "selected" : "") + R"rawliteral(>Center (Top)</option>
          <option value="1" )rawliteral" + String(settings.clockPosition == 1 ? "selected" : "") + R"rawliteral(>Left Column (Row 1)</option>
          <option value="2" )rawliteral" + String(settings.clockPosition == 2 ? "selected" : "") + R"rawliteral(>Right Column (Row 1)</option>
        </select>

        <label for="clockOffset" style="margin-top: 15px; display: block;">Clock Offset (pixels)</label>
        <input type="number" name="clockOffset" id="clockOffset" value=")rawliteral" + String(settings.clockOffset) + R"rawliteral(" min="-20" max="20" style="width: 100%; padding: 8px; box-sizing: border-box;">

        <p style="color: #888; font-size: 12px; margin-top: 10px;">
          Position clock to optimize space for metrics. Use offset to fine-tune horizontal position (-20 to +20 pixels).
        </p>

        <hr style="margin: 20px 0; border: none; border-top: 1px solid #333;">

        <label for="rowMode">Display Row Mode</label>
        <select name="rowMode" id="rowMode" onchange="updateRowMode()">
          <option value="0" )rawliteral" + String(settings.displayRowMode == 0 ? "selected" : "") + R"rawliteral(>5 Rows (13px spacing - optimized)</option>
          <option value="1" )rawliteral" + String(settings.displayRowMode == 1 ? "selected" : "") + R"rawliteral(>6 Rows (10px spacing - compact)</option>
        </select>

        <p style="color: #888; font-size: 12px; margin-top: 10px;">
          5-row mode provides maximum readability with optimized 13px spacing (11px with centered clock). 6-row mode fits more metrics. Row 6 positions (10-11) are hidden in 5-row mode.
        </p>

        <div style="margin-top: 20px;">
          <label>
            <input type="checkbox" name="rpmKFormat" id="rpmKFormat" )rawliteral" + String(settings.useRpmKFormat ? "checked" : "") + R"rawliteral(>
            Use K-format for RPM values (e.g., 1.8K instead of 1800RPM)
          </label>
          <p style="color: #888; font-size: 12px; margin-top: 5px;">
            Applies to all fan and pump speed metrics with RPM unit.
          </p>
        </div>
        </div>
      </div>

      <!-- Visible Metrics Section -->
      <div class="section-header" onclick="toggleSection('metricsSection')">
        <h3>&#128195; Visible Metrics</h3>
        <span class="section-arrow">&#9660;</span>
      </div>
      <div id="metricsSection" class="section-content">
        <div class="card">
        <p style="color: #888; font-size: 14px; margin-top: 0; text-align: left;">
          Select which metrics to show on OLED
        </p>

        <p style="color: #888; font-size: 12px; margin-top: 10px; background: #0f172a; padding: 10px; border-radius: 5px; border-left: 3px solid #00d4ff;">
          <strong>&#128161; Tip:</strong> Use <code style="background: #1e293b; padding: 2px 6px; border-radius: 3px;">^</code> character for spacing.<br>
          Example: <code style="background: #1e293b; padding: 2px 6px; border-radius: 3px;">CPU^^</code> displays as <code style="background: #1e293b; padding: 2px 6px; border-radius: 3px;">CPU:  45C</code> (2 spaces after colon)
        </p>

        <div id="metricsContainer">
          <p style="color: #888;">Loading metrics...</p>
        </div>

        <p style="color: #888; font-size: 12px; margin-top: 15px;">
          <strong>Note:</strong> Metrics are configured in Python script.<br>
          Select up to 20 in pc_stats_monitor_v2.py (use companion metrics to fit more)
        </p>

        <hr style="margin: 20px 0; border: none; border-top: 1px solid #333;">

        <div style="display: flex; align-items: center;">
          <input type="checkbox" name="showClock" id="showClock" value="1" )rawliteral" + String(settings.showClock ? "checked" : "") + R"rawliteral( style="width: 20px; margin: 0;">
          <label for="showClock" style="margin: 0 0 0 10px; text-align: left; color: #00d4ff;">Show Clock/Time in metrics display</label>
        </div>

        <script>
          let metricsData = [];
          let MAX_ROWS = )rawliteral" + String((settings.displayRowMode == 0) ? 5 : 6) + R"rawliteral(;  // Dynamic based on row mode

          function saveFormState() {
            // Save all form values before re-rendering
            metricsData.forEach(metric => {
              // Save label value
              const labelInput = document.querySelector(`input[name="label_${metric.id}"]`);
              if (labelInput) {
                metric.label = labelInput.value;
              }

              // Save position value
              const posDropdown = document.getElementById('pos_' + metric.id);
              if (posDropdown) {
                metric.position = parseInt(posDropdown.value);
              }

              // Save companion value
              const compDropdown = document.getElementById('comp_' + metric.id);
              if (compDropdown) {
                metric.companionId = parseInt(compDropdown.value);
              }

              // Save progress bar settings
              const barPosDropdown = document.getElementById('barPos_' + metric.id);
              if (barPosDropdown) {
                metric.barPosition = parseInt(barPosDropdown.value);
              }

              const barMinInput = document.querySelector(`input[name="barMin_${metric.id}"]`);
              if (barMinInput) {
                metric.barMin = parseInt(barMinInput.value) || 0;
              }

              const barMaxInput = document.querySelector(`input[name="barMax_${metric.id}"]`);
              if (barMaxInput) {
                metric.barMax = parseInt(barMaxInput.value) || 100;
              }

              const barWidthInput = document.querySelector(`input[name="barWidth_${metric.id}"]`);
              if (barWidthInput) {
                metric.barWidth = parseInt(barWidthInput.value) || 60;
              }

              const barOffsetInput = document.querySelector(`input[name="barOffset_${metric.id}"]`);
              if (barOffsetInput) {
                metric.barOffsetX = parseInt(barOffsetInput.value) || 0;
              }
            });
          }

          function updatePosition(metricId) {
            saveFormState();  // Save all changes before re-rendering
            renderMetrics();
          }

          function updateCompanion(metricId) {
            saveFormState();  // Save all changes before re-rendering
            renderMetrics();
          }

          function updateRowMode() {
            const rowMode = parseInt(document.getElementById('rowMode').value);
            const oldMaxRows = MAX_ROWS;
            MAX_ROWS = (rowMode === 0) ? 5 : 6;

            // Warn if switching to 5-row mode with metrics on Row 6
            if (MAX_ROWS === 5 && oldMaxRows === 6) {
              const row6Metrics = metricsData.filter(m =>
                m.position === 10 || m.position === 11 ||
                m.barPosition === 10 || m.barPosition === 11
              );
              if (row6Metrics.length > 0) {
                const metricNames = row6Metrics.map(m => m.name).join(', ');
                if (!confirm(`Warning: ${row6Metrics.length} metric(s) on Row 6 (${metricNames}) will be hidden. Continue?`)) {
                  document.getElementById('rowMode').value = '1';
                  MAX_ROWS = 6;
                  return;
                }
              }
            }

            // Hide Row 6 metrics when switching to 5-row mode
            if (MAX_ROWS === 5) {
              metricsData.forEach(metric => {
                if (metric.position === 10 || metric.position === 11) {
                  metric.position = 255;
                }
                if (metric.barPosition === 10 || metric.barPosition === 11) {
                  metric.barPosition = 255;
                }
              });
            }

            renderMetrics();
          }

          function renderMetrics() {
            const container = document.getElementById('metricsContainer');
            container.innerHTML = '';

            // Sort metrics by displayOrder
            const sortedMetrics = [...metricsData].sort((a, b) => a.displayOrder - b.displayOrder);

            // Header explaining the layout
            const header = document.createElement('div');
            header.style.cssText = 'background: #1e293b; padding: 12px; border-radius: 6px; margin-bottom: 15px; border: 2px solid #00d4ff;';
            header.innerHTML = `
              <div style="color: #00d4ff; font-weight: bold; font-size: 14px; margin-bottom: 5px;">&#128247; OLED Display Preview (` + MAX_ROWS + ` Rows Max)</div>
              <div style="color: #888; font-size: 12px;">Assign each metric to a specific position using the dropdown</div>
            `;
            container.appendChild(header);

            // Build 6 rows
            for (let rowIndex = 0; rowIndex < MAX_ROWS; rowIndex++) {
              // Find metrics for this row
              const leftPos = rowIndex * 2;      // 0, 2, 4, 6, 8, 10
              const rightPos = rowIndex * 2 + 1; // 1, 3, 5, 7, 9, 11

              const leftMetric = sortedMetrics.find(m => m.position === leftPos) || null;
              const rightMetric = sortedMetrics.find(m => m.position === rightPos) || null;

              const rowDiv = document.createElement('div');
              rowDiv.style.cssText = 'background: #0f172a; border: 1px solid #334155; border-radius: 6px; margin-bottom: 10px; overflow: hidden;';

              // Row header
              const rowHeader = document.createElement('div');
              rowHeader.style.cssText = 'background: #1e293b; padding: 6px 10px; color: #00d4ff; font-weight: bold; font-size: 12px; border-bottom: 1px solid #334155;';
              rowHeader.textContent = `Row ${rowIndex + 1}`;
              rowDiv.appendChild(rowHeader);

              // Row content - two columns
              const rowContent = document.createElement('div');
              rowContent.style.cssText = 'display: grid; grid-template-columns: 1fr 1fr; gap: 1px; background: #334155;';

              // Left slot
              const leftSlot = createMetricSlot(leftMetric, 'left', leftPos);
              rowContent.appendChild(leftSlot);

              // Right slot
              const rightSlot = createMetricSlot(rightMetric, 'right', rightPos);
              rowContent.appendChild(rightSlot);

              rowDiv.appendChild(rowContent);
              container.appendChild(rowDiv);
            }

            // Metrics list (for configuration)
            const metricsListDiv = document.createElement('div');
            metricsListDiv.style.cssText = 'background: #1e293b; border: 1px solid #334155; border-radius: 6px; padding: 15px; margin-top: 20px;';
            metricsListDiv.innerHTML = '<div style="color: #00d4ff; font-weight: bold; font-size: 14px; margin-bottom: 10px;">&#9881; All Metrics Configuration</div>';

            sortedMetrics.forEach(metric => {
              const metricDiv = createMetricConfig(metric);
              metricsListDiv.appendChild(metricDiv);
            });

            container.appendChild(metricsListDiv);
          }

          function createMetricSlot(metric, side, position) {
            const slot = document.createElement('div');
            slot.style.cssText = 'background: #0f172a; padding: 15px; min-height: 60px;';

            if (!metric) {
              // Empty slot
              slot.innerHTML = `
                <div style="color: #555; font-size: 12px; text-align: center; padding: 10px;">
                  ${side === 'left' ? '&#8592;' : '&#8594;'} Empty<br>
                  <span style="font-size: 10px;">No metric assigned</span>
                </div>
              `;
              return slot;
            }

            const companionName = metric.companionId > 0 ?
              (metricsData.find(m => m.id === metric.companionId)?.name || 'Unknown') : 'None';

            slot.innerHTML = `
              <div style="margin-bottom: 4px;">
                <div style="color: #00d4ff; font-weight: bold; font-size: 13px; margin-bottom: 2px;">
                  ${metric.name}
                </div>
                <div style="color: #888; font-size: 10px;">
                  Label: ${metric.label || metric.name}
                </div>
                ${metric.companionId > 0 ?
                  `<div style="color: #888; font-size: 10px;">Paired with: ${companionName}</div>` :
                  ''}
              </div>
            `;

            return slot;
          }

          function createMetricConfig(metric) {
            const div = document.createElement('div');
            div.style.cssText = 'background: #0f172a; padding: 12px; border-radius: 6px; margin-bottom: 8px; border: 1px solid #334155;';

            // Build position dropdown options
            let positionOptions = '<option value="255">None (Hidden)</option>';
            for (let row = 0; row < MAX_ROWS; row++) {
              const leftPos = row * 2;
              const rightPos = row * 2 + 1;
              positionOptions += `<option value="${leftPos}" ${metric.position === leftPos ? 'selected' : ''}>Row ${row + 1} - &#8592; Left</option>`;
              positionOptions += `<option value="${rightPos}" ${metric.position === rightPos ? 'selected' : ''}>Row ${row + 1} - Right &#8594;</option>`;
            }

            // Build bar position dropdown options (same as position but for progress bar placement)
            let barPositionOptions = '<option value="255">None</option>';
            for (let row = 0; row < MAX_ROWS; row++) {
              const leftPos = row * 2;
              const rightPos = row * 2 + 1;
              barPositionOptions += `<option value="${leftPos}" ${metric.barPosition === leftPos ? 'selected' : ''}>Row ${row + 1} - &#8592; Left</option>`;
              barPositionOptions += `<option value="${rightPos}" ${metric.barPosition === rightPos ? 'selected' : ''}>Row ${row + 1} - Right &#8594;</option>`;
            }

            // Build companion dropdown options
            let companionOptions = '<option value="0">None</option>';
            metricsData.forEach(m => {
              if (m.id !== metric.id) {
                const selected = (metric.companionId === m.id) ? 'selected' : '';
                companionOptions += `<option value="${m.id}" ${selected}>${m.name} (${m.unit})</option>`;
              }
            });

            div.innerHTML = `
              <div style="margin-bottom: 8px;">
                <div style="color: #00d4ff; font-weight: bold; font-size: 13px;">
                  ${metric.name} (${metric.unit})
                </div>
              </div>
              <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 10px;">
                <div>
                  <label style="color: #888; font-size: 10px; display: block; margin-bottom: 3px;">Position:</label>
                  <select name="position_${metric.id}" id="pos_${metric.id}" onchange="updatePosition(${metric.id})"
                          style="width: 100%; padding: 6px; background: #16213e; border: 1px solid #334155;
                                 color: #eee; border-radius: 3px; font-size: 11px;">
                    ${positionOptions}
                  </select>
                </div>
                <div>
                  <label style="color: #888; font-size: 10px; display: block; margin-bottom: 3px;">Pair with:</label>
                  <select name="companion_${metric.id}" id="comp_${metric.id}" onchange="updateCompanion(${metric.id})"
                          style="width: 100%; padding: 6px; background: #16213e; border: 1px solid #334155;
                                 color: #eee; border-radius: 3px; font-size: 11px;">
                    ${companionOptions}
                  </select>
                </div>
              </div>
              <div style="margin-top: 8px;">
                <label style="color: #888; font-size: 10px; display: block; margin-bottom: 3px;">Custom Label (10 chars max):</label>
                <input type="text" name="label_${metric.id}"
                       value="${metric.label}" maxlength="10" placeholder="${metric.name}"
                       style="width: 100%; padding: 6px; background: #16213e; border: 1px solid #334155;
                              color: #eee; border-radius: 3px; font-size: 11px; box-sizing: border-box;">
              </div>
              <div style="margin-top: 10px; padding-top: 8px; border-top: 1px solid #334155;">
                <label style="color: #888; font-size: 10px; display: block; margin-bottom: 3px;">Progress Bar Position:</label>
                <select name="barPosition_${metric.id}" id="barPos_${metric.id}"
                        style="width: 100%; padding: 6px; background: #16213e; border: 1px solid #334155;
                               color: #eee; border-radius: 3px; font-size: 11px; margin-bottom: 8px;">
                  ${barPositionOptions}
                </select>
                <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 10px;">
                  <div>
                    <label style="color: #888; font-size: 9px; display: block; margin-bottom: 2px;">Min Value:</label>
                    <input type="number" name="barMin_${metric.id}" value="${metric.barMin || 0}"
                           style="width: 100%; padding: 4px; background: #16213e; border: 1px solid #334155;
                                  color: #eee; border-radius: 3px; font-size: 10px; box-sizing: border-box;">
                  </div>
                  <div>
                    <label style="color: #888; font-size: 9px; display: block; margin-bottom: 2px;">Max Value:</label>
                    <input type="number" name="barMax_${metric.id}" value="${metric.barMax || 100}"
                           style="width: 100%; padding: 4px; background: #16213e; border: 1px solid #334155;
                                  color: #eee; border-radius: 3px; font-size: 10px; box-sizing: border-box;">
                  </div>
                </div>
                <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 10px; margin-top: 8px;">
                  <div>
                    <label style="color: #888; font-size: 9px; display: block; margin-bottom: 2px;">Width (px):</label>
                    <input type="number" name="barWidth_${metric.id}" value="${metric.barWidth || 60}" min="10" max="64"
                           style="width: 100%; padding: 4px; background: #16213e; border: 1px solid #334155;
                                  color: #eee; border-radius: 3px; font-size: 10px; box-sizing: border-box;">
                  </div>
                  <div>
                    <label style="color: #888; font-size: 9px; display: block; margin-bottom: 2px;">Offset X (px):</label>
                    <input type="number" name="barOffset_${metric.id}" value="${metric.barOffsetX || 0}" min="0" max="54"
                           style="width: 100%; padding: 4px; background: #16213e; border: 1px solid #334155;
                                  color: #eee; border-radius: 3px; font-size: 10px; box-sizing: border-box;">
                  </div>
                </div>
              </div>
              <input type="hidden" name="order_${metric.id}" value="${metric.displayOrder}">
            `;

            return div;
          }

          // Load metrics on page load
          fetch('/metrics')
            .then(res => res.json())
            .then(data => {
              if (data.metrics && data.metrics.length > 0) {
                metricsData = data.metrics;
                renderMetrics();
              } else {
                document.getElementById('metricsContainer').innerHTML =
                  '<p style="color: #ff6666;">No metrics received yet. Start Python script.</p>';
              }
            })
            .catch(err => {
              document.getElementById('metricsContainer').innerHTML =
                '<p style="color: #ff6666;">Error loading metrics</p>';
            });
        </script>
        </div>
      </div>
    </form>

    <form action="/reset" method="GET" onsubmit="return confirm('Reset WiFi settings? Device will restart in AP mode.');">
      <button type="submit" class="reset-btn">&#128260; Reset WiFi Settings</button>
    </form>

    <div class="info">
      PC Stats Monitor v2.0<br>
      Configure Python script with IP shown above
    </div>
  </div>

  <!-- Sticky Save Button -->
  <div class="sticky-save">
    <div class="container">
      <button type="button" class="save-btn" onclick="document.querySelector('form[action=\'/save\']').submit()">&#128190; Save Settings</button>
    </div>
  </div>

  <script>
    // Collapsible section toggle
    function toggleSection(sectionId) {
      const content = document.getElementById(sectionId);
      const arrow = event.currentTarget.querySelector('.section-arrow');
      content.classList.toggle('collapsed');
      arrow.classList.toggle('collapsed');
    }

    // Export configuration
    function exportConfig() {
      fetch('/api/export')
        .then(response => response.json())
        .then(data => {
          const blob = new Blob([JSON.stringify(data, null, 2)], { type: 'application/json' });
          const url = URL.createObjectURL(blob);
          const a = document.createElement('a');
          a.href = url;
          a.download = 'pc-monitor-config.json';
          document.body.appendChild(a);
          a.click();
          document.body.removeChild(a);
          URL.revokeObjectURL(url);
          alert('Configuration exported successfully!');
        })
        .catch(err => alert('Error exporting configuration: ' + err));
    }

    // Import configuration
    function importConfig(event) {
      const file = event.target.files[0];
      if (!file) return;

      const reader = new FileReader();
      reader.onload = function(e) {
        try {
          const config = JSON.parse(e.target.result);

          // Send to server
          fetch('/api/import', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(config)
          })
          .then(response => response.json())
          .then(data => {
            if (data.success) {
              alert('Configuration imported successfully! Reloading page...');
              location.reload();
            } else {
              alert('Error importing configuration: ' + data.message);
            }
          })
          .catch(err => alert('Error importing configuration: ' + err));
        } catch (err) {
          alert('Invalid configuration file: ' + err);
        }
      };
      reader.readAsText(file);
    }
  </script>
</body>
</html>
)rawliteral";

  server.send(200, "text/html", html);
}

void handleSave() {
  if (server.hasArg("clockStyle")) {
    settings.clockStyle = server.arg("clockStyle").toInt();
  }
  if (server.hasArg("gmtOffset")) {
    settings.gmtOffset = server.arg("gmtOffset").toInt();
  }
  if (server.hasArg("dst")) {
    settings.daylightSaving = server.arg("dst").toInt() == 1;
  }
  if (server.hasArg("use24Hour")) {
    settings.use24Hour = server.arg("use24Hour").toInt() == 1;
  }
  if (server.hasArg("dateFormat")) {
    settings.dateFormat = server.arg("dateFormat").toInt();
  }

  // Save clock position
  if (server.hasArg("clockPosition")) {
    settings.clockPosition = server.arg("clockPosition").toInt();
  }

  // Save clock offset
  if (server.hasArg("clockOffset")) {
    settings.clockOffset = server.arg("clockOffset").toInt();
  }

  // Save clock visibility checkbox
  settings.showClock = server.hasArg("showClock");

  // Save row mode selection
  if (server.hasArg("rowMode")) {
    settings.displayRowMode = server.arg("rowMode").toInt();
  }

  // Save RPM format preference
  settings.useRpmKFormat = server.hasArg("rpmKFormat");

  // Save custom labels
  for (int i = 0; i < MAX_METRICS; i++) {
    String labelArg = "label_" + String(i + 1);
    if (server.hasArg(labelArg)) {
      String label = server.arg(labelArg);
      label.trim();
      if (label.length() > 0) {
        strncpy(settings.metricLabels[i], label.c_str(), METRIC_NAME_LEN - 1);
        settings.metricLabels[i][METRIC_NAME_LEN - 1] = '\0';
      } else {
        settings.metricLabels[i][0] = '\0';  // Empty = use Python name
      }
    }
  }

  // Save metric display order
  for (int i = 0; i < MAX_METRICS; i++) {
    String orderArg = "order_" + String(i + 1);
    if (server.hasArg(orderArg)) {
      settings.metricOrder[i] = server.arg(orderArg).toInt();
    }
  }

  // Save metric companions
  for (int i = 0; i < MAX_METRICS; i++) {
    String companionArg = "companion_" + String(i + 1);
    if (server.hasArg(companionArg)) {
      settings.metricCompanions[i] = server.arg(companionArg).toInt();
    } else {
      settings.metricCompanions[i] = 0;  // No companion
    }
  }

  // Save metric positions
  for (int i = 0; i < MAX_METRICS; i++) {
    String positionArg = "position_" + String(i + 1);
    if (server.hasArg(positionArg)) {
      settings.metricPositions[i] = server.arg(positionArg).toInt();
    } else {
      settings.metricPositions[i] = 255;  // Default: None/Hidden
    }
  }

  // Save progress bar settings
  for (int i = 0; i < MAX_METRICS; i++) {
    String barPosArg = "barPosition_" + String(i + 1);
    String minArg = "barMin_" + String(i + 1);
    String maxArg = "barMax_" + String(i + 1);
    String widthArg = "barWidth_" + String(i + 1);
    String offsetArg = "barOffset_" + String(i + 1);

    if (server.hasArg(barPosArg)) {
      settings.metricBarPositions[i] = server.arg(barPosArg).toInt();
    } else {
      settings.metricBarPositions[i] = 255;  // Default: No bar
    }

    if (server.hasArg(minArg)) {
      settings.metricBarMin[i] = server.arg(minArg).toInt();
    }
    if (server.hasArg(maxArg)) {
      settings.metricBarMax[i] = server.arg(maxArg).toInt();
    }
    if (server.hasArg(widthArg)) {
      settings.metricBarWidths[i] = server.arg(widthArg).toInt();
    } else {
      settings.metricBarWidths[i] = 60;  // Default width
    }
    if (server.hasArg(offsetArg)) {
      settings.metricBarOffsets[i] = server.arg(offsetArg).toInt();
    } else {
      settings.metricBarOffsets[i] = 0;  // Default: no offset
    }
  }

  // Hide Row 6 metrics when switching to 5-row mode
  if (settings.displayRowMode == 0) {  // 5-row mode
    for (int i = 0; i < MAX_METRICS; i++) {
      if (settings.metricPositions[i] == 10 || settings.metricPositions[i] == 11) {
        settings.metricPositions[i] = 255;  // Hide
      }
      if (settings.metricBarPositions[i] == 10 || settings.metricBarPositions[i] == 11) {
        settings.metricBarPositions[i] = 255;  // Hide bars
      }
    }
  }

  // Apply labels, order, companions, positions, and progress bars to current metrics in memory
  for (int i = 0; i < metricData.count; i++) {
    Metric& m = metricData.metrics[i];
    if (m.id > 0 && m.id <= MAX_METRICS) {
      // Apply custom label if set
      if (settings.metricLabels[m.id - 1][0] != '\0') {
        strncpy(m.label, settings.metricLabels[m.id - 1], METRIC_NAME_LEN - 1);
        m.label[METRIC_NAME_LEN - 1] = '\0';
      } else {
        strncpy(m.label, m.name, METRIC_NAME_LEN - 1);
        m.label[METRIC_NAME_LEN - 1] = '\0';
      }

      // Apply display order
      m.displayOrder = settings.metricOrder[m.id - 1];

      // Apply companion metric
      m.companionId = settings.metricCompanions[m.id - 1];

      // Apply position assignment
      m.position = settings.metricPositions[m.id - 1];

      // Apply progress bar settings
      m.barPosition = settings.metricBarPositions[m.id - 1];
      m.barMin = settings.metricBarMin[m.id - 1];
      m.barMax = settings.metricBarMax[m.id - 1];
      m.barWidth = settings.metricBarWidths[m.id - 1];
      m.barOffsetX = settings.metricBarOffsets[m.id - 1];

      // Store/update the metric name for future validation
      strncpy(settings.metricNames[m.id - 1], m.name, METRIC_NAME_LEN - 1);
      settings.metricNames[m.id - 1][METRIC_NAME_LEN - 1] = '\0';
    }
  }

  saveSettings();
  applyTimezone();

  // Reset Mario animation state when switching modes
  mario_state = MARIO_IDLE;
  mario_x = -15;
  animation_triggered = false;
  time_overridden = false;
  last_minute = -1;

  // Reset Space Invader animation state when switching modes
  invader_state = INVADER_PATROL;
  invader_x = 64;  // Center of screen (invader_y is const at 56)

  // Reset Space Ship animation state when switching modes
  ship_state = SHIP_PATROL;
  ship_x = 64;  // Center of screen
  
  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta http-equiv="refresh" content="2;url=/">
  <title>Settings Saved</title>
  <style>
    body { font-family: Arial; background: #1a1a2e; color: #00d4ff; display: flex; justify-content: center; align-items: center; height: 100vh; margin: 0; }
    .msg { text-align: center; }
    h1 { font-size: 48px; }
  </style>
</head>
<body>
  <div class="msg">
    <h1>&#9989;</h1>
    <p>Settings saved! Redirecting...</p>
  </div>
</body>
</html>
)rawliteral";
  
  server.send(200, "text/html", html);
}

void handleReset() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>Resetting...</title>
  <style>
    body { font-family: Arial; background: #1a1a2e; color: #e94560; display: flex; justify-content: center; align-items: center; height: 100vh; margin: 0; }
    .msg { text-align: center; }
  </style>
</head>
<body>
  <div class="msg">
    <h1>&#128260;</h1>
    <p>Resetting WiFi settings...<br>Connect to "PCMonitor-Setup" to reconfigure.</p>
  </div>
</body>
</html>
)rawliteral";
  
  server.send(200, "text/html", html);
  delay(1000);
  
  wifiManager.resetSettings();
  ESP.restart();
}

// Export configuration as JSON
void handleExportConfig() {
  String json = "{";

  // Clock settings
  json += "\"clockStyle\":" + String(settings.clockStyle) + ",";
  json += "\"gmtOffset\":" + String(settings.gmtOffset) + ",";
  json += "\"daylightSaving\":" + String(settings.daylightSaving ? "true" : "false") + ",";
  json += "\"use24Hour\":" + String(settings.use24Hour ? "true" : "false") + ",";
  json += "\"dateFormat\":" + String(settings.dateFormat) + ",";
  json += "\"clockPosition\":" + String(settings.clockPosition) + ",";
  json += "\"clockOffset\":" + String(settings.clockOffset) + ",";
  json += "\"showClock\":" + String(settings.showClock ? "true" : "false") + ",";
  json += "\"displayRowMode\":" + String(settings.displayRowMode) + ",";
  json += "\"useRpmKFormat\":" + String(settings.useRpmKFormat ? "true" : "false") + ",";

  // Metric labels
  json += "\"metricLabels\":[";
  for (int i = 0; i < MAX_METRICS; i++) {
    if (i > 0) json += ",";
    json += "\"" + String(settings.metricLabels[i]) + "\"";
  }
  json += "],";

  // Metric names
  json += "\"metricNames\":[";
  for (int i = 0; i < MAX_METRICS; i++) {
    if (i > 0) json += ",";
    json += "\"" + String(settings.metricNames[i]) + "\"";
  }
  json += "],";

  // Metric order
  json += "\"metricOrder\":[";
  for (int i = 0; i < MAX_METRICS; i++) {
    if (i > 0) json += ",";
    json += String(settings.metricOrder[i]);
  }
  json += "],";

  // Metric companions
  json += "\"metricCompanions\":[";
  for (int i = 0; i < MAX_METRICS; i++) {
    if (i > 0) json += ",";
    json += String(settings.metricCompanions[i]);
  }
  json += "],";

  // Metric positions
  json += "\"metricPositions\":[";
  for (int i = 0; i < MAX_METRICS; i++) {
    if (i > 0) json += ",";
    json += String(settings.metricPositions[i]);
  }
  json += "],";

  // Progress bar settings
  json += "\"metricBarPositions\":[";
  for (int i = 0; i < MAX_METRICS; i++) {
    if (i > 0) json += ",";
    json += String(settings.metricBarPositions[i]);
  }
  json += "],";

  json += "\"metricBarMin\":[";
  for (int i = 0; i < MAX_METRICS; i++) {
    if (i > 0) json += ",";
    json += String(settings.metricBarMin[i]);
  }
  json += "],";

  json += "\"metricBarMax\":[";
  for (int i = 0; i < MAX_METRICS; i++) {
    if (i > 0) json += ",";
    json += String(settings.metricBarMax[i]);
  }
  json += "],";

  json += "\"metricBarWidths\":[";
  for (int i = 0; i < MAX_METRICS; i++) {
    if (i > 0) json += ",";
    json += String(settings.metricBarWidths[i]);
  }
  json += "],";

  json += "\"metricBarOffsets\":[";
  for (int i = 0; i < MAX_METRICS; i++) {
    if (i > 0) json += ",";
    json += String(settings.metricBarOffsets[i]);
  }
  json += "]";

  json += "}";

  server.send(200, "application/json", json);
}

// Import configuration from JSON
void handleImportConfig() {
  if (server.hasArg("plain")) {
    String body = server.arg("plain");

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, body);

    if (error) {
      server.send(400, "application/json", "{\"success\":false,\"message\":\"Invalid JSON\"}");
      return;
    }

    // Import clock settings
    if (!doc["clockStyle"].isNull()) settings.clockStyle = doc["clockStyle"];
    if (!doc["gmtOffset"].isNull()) settings.gmtOffset = doc["gmtOffset"];
    if (!doc["daylightSaving"].isNull()) settings.daylightSaving = doc["daylightSaving"];
    if (!doc["use24Hour"].isNull()) settings.use24Hour = doc["use24Hour"];
    if (!doc["dateFormat"].isNull()) settings.dateFormat = doc["dateFormat"];
    if (!doc["clockPosition"].isNull()) settings.clockPosition = doc["clockPosition"];
    if (!doc["clockOffset"].isNull()) settings.clockOffset = doc["clockOffset"];
    if (!doc["showClock"].isNull()) settings.showClock = doc["showClock"];
    if (!doc["displayRowMode"].isNull()) settings.displayRowMode = doc["displayRowMode"];
    if (!doc["useRpmKFormat"].isNull()) settings.useRpmKFormat = doc["useRpmKFormat"];

    // Import metric labels
    if (!doc["metricLabels"].isNull()) {
      JsonArray labels = doc["metricLabels"];
      for (int i = 0; i < MAX_METRICS && i < labels.size(); i++) {
        const char* label = labels[i];
        if (label) {
          strncpy(settings.metricLabels[i], label, METRIC_NAME_LEN - 1);
          settings.metricLabels[i][METRIC_NAME_LEN - 1] = '\0';
        }
      }
    }

    // Import metric names
    if (!doc["metricNames"].isNull()) {
      JsonArray names = doc["metricNames"];
      for (int i = 0; i < MAX_METRICS && i < names.size(); i++) {
        const char* name = names[i];
        if (name) {
          strncpy(settings.metricNames[i], name, METRIC_NAME_LEN - 1);
          settings.metricNames[i][METRIC_NAME_LEN - 1] = '\0';
        }
      }
    }

    // Import metric order
    if (!doc["metricOrder"].isNull()) {
      JsonArray order = doc["metricOrder"];
      for (int i = 0; i < MAX_METRICS && i < order.size(); i++) {
        settings.metricOrder[i] = order[i];
      }
    }

    // Import metric companions
    if (!doc["metricCompanions"].isNull()) {
      JsonArray companions = doc["metricCompanions"];
      for (int i = 0; i < MAX_METRICS && i < companions.size(); i++) {
        settings.metricCompanions[i] = companions[i];
      }
    }

    // Import metric positions
    if (!doc["metricPositions"].isNull()) {
      JsonArray positions = doc["metricPositions"];
      for (int i = 0; i < MAX_METRICS && i < positions.size(); i++) {
        settings.metricPositions[i] = positions[i];
      }
    }

    // Import progress bar settings
    if (!doc["metricBarPositions"].isNull()) {
      JsonArray barPositions = doc["metricBarPositions"];
      for (int i = 0; i < MAX_METRICS && i < barPositions.size(); i++) {
        settings.metricBarPositions[i] = barPositions[i];
      }
    }

    if (!doc["metricBarMin"].isNull()) {
      JsonArray barMin = doc["metricBarMin"];
      for (int i = 0; i < MAX_METRICS && i < barMin.size(); i++) {
        settings.metricBarMin[i] = barMin[i];
      }
    }

    if (!doc["metricBarMax"].isNull()) {
      JsonArray barMax = doc["metricBarMax"];
      for (int i = 0; i < MAX_METRICS && i < barMax.size(); i++) {
        settings.metricBarMax[i] = barMax[i];
      }
    }

    if (!doc["metricBarWidths"].isNull()) {
      JsonArray barWidths = doc["metricBarWidths"];
      for (int i = 0; i < MAX_METRICS && i < barWidths.size(); i++) {
        settings.metricBarWidths[i] = barWidths[i];
      }
    }

    if (!doc["metricBarOffsets"].isNull()) {
      JsonArray barOffsets = doc["metricBarOffsets"];
      for (int i = 0; i < MAX_METRICS && i < barOffsets.size(); i++) {
        settings.metricBarOffsets[i] = barOffsets[i];
      }
    }

    // Hide Row 6 metrics when importing 5-row mode config
    if (settings.displayRowMode == 0) {
      for (int i = 0; i < MAX_METRICS; i++) {
        if (settings.metricPositions[i] == 10 || settings.metricPositions[i] == 11) {
          settings.metricPositions[i] = 255;
        }
        if (settings.metricBarPositions[i] == 10 || settings.metricBarPositions[i] == 11) {
          settings.metricBarPositions[i] = 255;
        }
      }
    }

    // Save imported settings
    saveSettings();
    applyTimezone();

    server.send(200, "application/json", "{\"success\":true,\"message\":\"Configuration imported successfully\"}");
  } else {
    server.send(400, "application/json", "{\"success\":false,\"message\":\"No data received\"}");
  }
}

void displaySetupInstructions() {
  display.clearDisplay();
  display.setTextSize(1);
  
  display.setCursor(20, 0);
  display.println("WiFi Setup");
  display.drawLine(0, 10, 128, 10, SSD1306_WHITE);
  
  display.setCursor(0, 14);
  display.println("1.Connect to WiFi:");
  
  display.setCursor(0, 26);
  display.print("  ");
  display.println(AP_NAME);
  
  display.setCursor(0, 38);
  display.print("  Pass: ");
  display.println(AP_PASSWORD);
  
  display.setCursor(0, 50);
  display.println("2.Open 192.168.4.1");
  
  display.display();
}

void displayConnecting() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(20, 25);
  display.println("Connecting to");
  display.setCursor(30, 40);
  display.println("WiFi...");
  display.display();
}

void displayConnected() {
  display.clearDisplay();
  display.setTextSize(1);
  
  display.setCursor(25, 4);
  display.println("Connected!");
  
  display.setCursor(8, 18);
  display.println("IP (for Python):");
  
  String ip = WiFi.localIP().toString();
  int ip_width = ip.length() * 6;
  int ip_x = (SCREEN_WIDTH - ip_width) / 2;
  display.setCursor(ip_x, 30);
  display.println(ip);
  
  display.drawLine(0, 42, 128, 42, SSD1306_WHITE);
  
  display.setCursor(4, 48);
  display.println("Open IP in browser");
  display.setCursor(12, 56);
  display.println("to change settings");
  
  display.display();
}

void loop() {
  // Feed watchdog to prevent reset
  esp_task_wdt_reset();

  // Handle web server requests
  server.handleClient();

  // WiFi connection management with reconnection logic
  if (WiFi.status() != WL_CONNECTED) {
    if (wifiDisconnectTime == 0) {
      wifiDisconnectTime = millis();
      Serial.println("WiFi disconnected, attempting reconnection...");
      WiFi.reconnect();
    }

    // Check if reconnection timeout exceeded
    if (millis() - wifiDisconnectTime > WIFI_RECONNECT_TIMEOUT) {
      Serial.println("WiFi reconnection failed, restarting...");
      ESP.restart();
    }

    // Show reconnection status (blink every 500ms)
    if (displayAvailable && (millis() / 500) % 2 == 0) {
      display.clearDisplay();
      display.setTextSize(1);
      display.setCursor(10, 20);
      display.println("WiFi Lost");
      display.setCursor(10, 35);
      display.println("Reconnecting...");
      display.display();
    }

    return;  // Skip normal processing while reconnecting
  } else {
    // WiFi connected - reset disconnect timer
    if (wifiDisconnectTime != 0) {
      Serial.println("WiFi reconnected successfully!");
      wifiDisconnectTime = 0;
    }
  }

  int packetSize = udp.parsePacket();
  if (packetSize) {
    char buffer[1024];  // Increased buffer for up to 12 metrics
    int len = udp.read(buffer, sizeof(buffer) - 1);
    if (len > 0) {
      buffer[len] = '\0';

      // Debug: Show packet info
      Serial.print("UDP packet: ");
      Serial.print(packetSize);
      Serial.print(" bytes, read: ");
      Serial.print(len);
      Serial.println(" bytes");

      if (packetSize > sizeof(buffer) - 1) {
        Serial.println("WARNING: Packet truncated! Increase buffer size.");
      }

      parseStats(buffer);
      lastReceived = millis();
    }
  }
  
  stats.online = (millis() - lastReceived) < TIMEOUT;
  metricData.online = stats.online;  // Sync both for backward compatibility

  if (displayAvailable) {
    display.clearDisplay();

    if (metricData.online) {
      displayStats();
    } else {
      if (settings.clockStyle == 0) {
        displayClockWithMario();
      } else if (settings.clockStyle == 1) {
        displayStandardClock();
      } else if (settings.clockStyle == 2) {
        displayLargeClock();
      } else if (settings.clockStyle == 3) {
        displayClockWithInvader();
      } else if (settings.clockStyle == 4) {
        displayClockWithShip();
      }
    }

    display.display();
  }

  delay(30);
}

// Helper function to trim trailing whitespace (only from Python names, not custom labels)
void trimTrailingSpaces(char* str) {
  int len = strlen(str);
  while (len > 0 && (str[len - 1] == ' ' || str[len - 1] == '\t')) {
    str[len - 1] = '\0';
    len--;
  }
}

// Helper function to convert '^' to spaces in labels for custom spacing
// Example: "CPU^^" becomes "CPU  " for display alignment
void convertCaretToSpaces(char* str) {
  int len = strlen(str);
  for (int i = 0; i < len; i++) {
    if (str[i] == '^') {
      str[i] = ' ';
    }
  }
}

// Helper function to add a legacy metric to the new system
void addLegacyMetric(uint8_t id, const char* name, int value, const char* unit) {
  if (metricData.count >= MAX_METRICS) return;

  Metric& m = metricData.metrics[metricData.count];
  m.id = id;
  strncpy(m.name, name, METRIC_NAME_LEN - 1);
  m.name[METRIC_NAME_LEN - 1] = '\0';
  trimTrailingSpaces(m.name);

  strncpy(m.unit, unit, METRIC_UNIT_LEN - 1);
  m.unit[METRIC_UNIT_LEN - 1] = '\0';
  m.value = value;

  // Load settings (ID-based indexing)
  if (id > 0 && id <= MAX_METRICS) {
    // Check if stored metric name matches current metric name
    // If no stored name exists OR name matches, apply settings
    // If name exists but doesn't match, this is a different sensor - use defaults
    bool nameMatches = (settings.metricNames[id - 1][0] == '\0' ||  // No stored name
                        strcmp(settings.metricNames[id - 1], m.name) == 0);  // Name matches

    if (nameMatches) {
      // Apply stored settings
      // Apply custom label if set (preserve '^' character - it will be converted during display)
      if (settings.metricLabels[id - 1][0] != '\0') {
        strncpy(m.label, settings.metricLabels[id - 1], METRIC_NAME_LEN - 1);
        m.label[METRIC_NAME_LEN - 1] = '\0';
      } else {
        strncpy(m.label, m.name, METRIC_NAME_LEN - 1);
        m.label[METRIC_NAME_LEN - 1] = '\0';
      }

      m.displayOrder = settings.metricOrder[id - 1];

      // Load companion metric
      m.companionId = settings.metricCompanions[id - 1];

      // Load position assignment (255 = hidden, 0-11 = visible)
      m.position = settings.metricPositions[id - 1];

      // Load progress bar settings
      m.barPosition = settings.metricBarPositions[id - 1];
      m.barMin = settings.metricBarMin[id - 1];
      m.barMax = settings.metricBarMax[id - 1];
      m.barWidth = settings.metricBarWidths[id - 1];
      m.barOffsetX = settings.metricBarOffsets[id - 1];

      // Store/update the metric name for future validation
      strncpy(settings.metricNames[id - 1], m.name, METRIC_NAME_LEN - 1);
      settings.metricNames[id - 1][METRIC_NAME_LEN - 1] = '\0';
    } else {
      // Name mismatch - this is a different sensor now, use defaults
      Serial.printf("Metric ID %d name changed: '%s' -> '%s', using defaults\n",
                    id, settings.metricNames[id - 1], m.name);

      strncpy(m.label, m.name, METRIC_NAME_LEN - 1);
      m.label[METRIC_NAME_LEN - 1] = '\0';
      m.displayOrder = metricData.count;
      m.companionId = 0;
      m.position = 255;  // Default: None/Hidden
      m.barPosition = 255;  // Default: No bar
      m.barMin = 0;
      m.barMax = 100;
      m.barWidth = 60;
      m.barOffsetX = 0;

      // Update stored name to the new sensor
      strncpy(settings.metricNames[id - 1], m.name, METRIC_NAME_LEN - 1);
      settings.metricNames[id - 1][METRIC_NAME_LEN - 1] = '\0';

      // Clear stored label since it's for a different sensor
      settings.metricLabels[id - 1][0] = '\0';
    }
  } else {
    // Default values for new metrics
    strncpy(m.label, m.name, METRIC_NAME_LEN - 1);
    m.label[METRIC_NAME_LEN - 1] = '\0';
    m.displayOrder = metricData.count;
    m.companionId = 0;  // No companion for new metrics
    m.position = 255;  // Default: None/Hidden
    m.barPosition = 255;  // Default: No bar
    m.barMin = 0;
    m.barMax = 100;
    m.barWidth = 60;  // Default width
    m.barOffsetX = 0;  // Default: no offset
  }

  metricData.count++;
}

// Parse v1.0 protocol (legacy format)
void parseStatsV1(JsonDocument& doc) {
  metricData.count = 0;

  // Map legacy fields to metrics array (using modern ArduinoJson API)
  if (!doc["cpu_percent"].isNull()) {
    addLegacyMetric(1, "CPU", (int)doc["cpu_percent"].as<float>(), "%");
  }
  if (!doc["ram_percent"].isNull()) {
    addLegacyMetric(2, "RAM", (int)doc["ram_percent"].as<float>(), "%");
  }
  if (!doc["cpu_temp"].isNull()) {
    addLegacyMetric(3, "CPU_TEMP", doc["cpu_temp"] | 0, "C");
  }
  if (!doc["gpu_temp"].isNull()) {
    addLegacyMetric(4, "GPU_TEMP", doc["gpu_temp"] | 0, "C");
  }
  if (!doc["fan_speed"].isNull()) {
    addLegacyMetric(5, "FAN", doc["fan_speed"] | 0, "RPM");
  }
  if (!doc["disk_percent"].isNull()) {
    addLegacyMetric(6, "DISK", (int)doc["disk_percent"].as<float>(), "%");
  }

  // Extract timestamp
  const char* ts = doc["timestamp"];
  if (ts) {
    strncpy(metricData.timestamp, ts, 5);
    metricData.timestamp[5] = '\0';
  }

  // Also update legacy stats struct for backward compatibility
  stats.cpu_percent = doc["cpu_percent"] | 0.0;
  stats.ram_percent = doc["ram_percent"] | 0.0;
  stats.ram_used_gb = doc["ram_used_gb"] | 0.0;
  stats.ram_total_gb = doc["ram_total_gb"] | 0.0;
  stats.disk_percent = doc["disk_percent"] | 0.0;
  stats.cpu_temp = doc["cpu_temp"] | 0;
  stats.gpu_temp = doc["gpu_temp"] | 0;
  stats.fan_speed = doc["fan_speed"] | 0;
  if (ts) {
    strncpy(stats.timestamp, ts, 5);
    stats.timestamp[5] = '\0';
  }
  stats.online = true;

  metricData.online = true;
}

// Parse v2.0 protocol (dynamic metrics)
void parseStatsV2(JsonDocument& doc) {
  // Extract timestamp
  const char* ts = doc["timestamp"];
  if (ts) {
    strncpy(metricData.timestamp, ts, 5);
    metricData.timestamp[5] = '\0';
  }

  // Extract metrics array
  JsonArray metricsArray = doc["metrics"];
  metricData.count = 0;

  for (JsonObject metricObj : metricsArray) {
    if (metricData.count >= MAX_METRICS) break;

    Metric& m = metricData.metrics[metricData.count];

    m.id = metricObj["id"] | 0;

    const char* name = metricObj["name"];
    if (name) {
      strncpy(m.name, name, METRIC_NAME_LEN - 1);
      m.name[METRIC_NAME_LEN - 1] = '\0';
      trimTrailingSpaces(m.name);
    }

    const char* unit = metricObj["unit"];
    if (unit) {
      strncpy(m.unit, unit, METRIC_UNIT_LEN - 1);
      m.unit[METRIC_UNIT_LEN - 1] = '\0';
    }

    m.value = metricObj["value"] | 0;

    // Load visibility from settings (ID-based indexing)
    if (m.id > 0 && m.id <= MAX_METRICS) {
      // Check if stored metric name matches current metric name
      // If no stored name exists OR name matches, apply settings
      // If name exists but doesn't match, this is a different sensor - use defaults
      bool nameMatches = (settings.metricNames[m.id - 1][0] == '\0' ||  // No stored name
                          strcmp(settings.metricNames[m.id - 1], m.name) == 0);  // Name matches

      if (nameMatches) {
        // Apply stored settings
        // Apply custom label if set (preserve '^' character - it will be converted during display)
        if (settings.metricLabels[m.id - 1][0] != '\0') {
          strncpy(m.label, settings.metricLabels[m.id - 1], METRIC_NAME_LEN - 1);
          m.label[METRIC_NAME_LEN - 1] = '\0';
        } else {
          // No custom label, copy name to label
          strncpy(m.label, m.name, METRIC_NAME_LEN - 1);
          m.label[METRIC_NAME_LEN - 1] = '\0';
        }

        // Load display order
        m.displayOrder = settings.metricOrder[m.id - 1];

        // Load companion metric
        m.companionId = settings.metricCompanions[m.id - 1];

        // Load position assignment (255 = hidden, 0-11 = visible)
        m.position = settings.metricPositions[m.id - 1];

        // Load progress bar settings
        m.barPosition = settings.metricBarPositions[m.id - 1];
        m.barMin = settings.metricBarMin[m.id - 1];
        m.barMax = settings.metricBarMax[m.id - 1];
        m.barWidth = settings.metricBarWidths[m.id - 1];
        m.barOffsetX = settings.metricBarOffsets[m.id - 1];

        // Store/update the metric name for future validation
        strncpy(settings.metricNames[m.id - 1], m.name, METRIC_NAME_LEN - 1);
        settings.metricNames[m.id - 1][METRIC_NAME_LEN - 1] = '\0';
      } else {
        // Name mismatch - this is a different sensor now, use defaults
        Serial.printf("Metric ID %d name changed: '%s' -> '%s', using defaults\n",
                      m.id, settings.metricNames[m.id - 1], m.name);

        strncpy(m.label, m.name, METRIC_NAME_LEN - 1);
        m.label[METRIC_NAME_LEN - 1] = '\0';
        m.displayOrder = metricData.count;
        m.companionId = 0;
        m.position = 255;  // Default: None/Hidden
        m.barPosition = 255;  // Default: No bar
        m.barMin = 0;
        m.barMax = 100;
        m.barWidth = 60;
        m.barOffsetX = 0;

        // Update stored name to the new sensor
        strncpy(settings.metricNames[m.id - 1], m.name, METRIC_NAME_LEN - 1);
        settings.metricNames[m.id - 1][METRIC_NAME_LEN - 1] = '\0';

        // Clear stored label since it's for a different sensor
        settings.metricLabels[m.id - 1][0] = '\0';
      }
    } else {
      // Default values for new metrics
      strncpy(m.label, m.name, METRIC_NAME_LEN - 1);
      m.label[METRIC_NAME_LEN - 1] = '\0';
      m.displayOrder = metricData.count;
      m.companionId = 0;  // No companion for new metrics
      m.position = 255;  // Default: None/Hidden
      m.barPosition = 255;  // Default: No bar
      m.barMin = 0;
      m.barMax = 100;
      m.barWidth = 60;  // Default width
      m.barOffsetX = 0;  // Default: no offset
    }

    metricData.count++;
  }

  metricData.online = true;

  // Debug output
  Serial.print("Received ");
  Serial.print(metricData.count);
  Serial.print(" metrics, ");
  int visibleCount = 0;
  for (int i = 0; i < metricData.count; i++) {
    if (metricData.metrics[i].position != 255) visibleCount++;
  }
  Serial.print(visibleCount);
  Serial.println(" visible (position assigned)");
}

// Main parsing function with version detection
void parseStats(const char* json) {
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, json);

  if (error) {
    Serial.print("JSON parse error: ");
    Serial.println(error.c_str());
    return;
  }

  // Version detection
  const char* version = doc["version"];
  if (version && strcmp(version, "2.0") == 0) {
    // New dynamic protocol
    parseStatsV2(doc);
    Serial.println("Parsed v2.0 protocol");
  } else {
    // Legacy fixed protocol (backward compatibility)
    parseStatsV1(doc);
    Serial.println("Parsed v1.0 protocol (legacy)");
  }
}

// Progress bars layout (legacy, 4-5 metrics)
void displayStatsProgressBars() {
  display.setTextSize(1);
  int currentY = 0;
  const int LINE_HEIGHT = 14;

  // Create sorted array of metric indices by displayOrder
  int sortedIndices[MAX_METRICS];
  int sortedCount = 0;

  // Build sorted index array (only include metrics with assigned positions)
  for (int order = 0; order < MAX_METRICS; order++) {
    for (int i = 0; i < metricData.count; i++) {
      if (metricData.metrics[i].displayOrder == order && metricData.metrics[i].position != 255) {
        sortedIndices[sortedCount++] = i;
        break;
      }
    }
  }

  // Dynamic rendering loop with sorted order
  for (int idx = 0; idx < sortedCount && currentY < 64; idx++) {
    Metric& m = metricData.metrics[sortedIndices[idx]];

    // Label + Value (use custom label, strip trailing % if present)
    display.setCursor(0, currentY);

    // Process label: convert '^' to spaces, strip trailing '%', move trailing spaces to after colon
    char displayLabel[METRIC_NAME_LEN];
    strncpy(displayLabel, m.label, METRIC_NAME_LEN - 1);
    displayLabel[METRIC_NAME_LEN - 1] = '\0';

    // Convert '^' to spaces for custom alignment
    convertCaretToSpaces(displayLabel);

    // Count and remove trailing spaces (to be added after colon)
    int len = strlen(displayLabel);
    int trailingSpaces = 0;
    while (len > 0 && displayLabel[len - 1] == ' ') {
      trailingSpaces++;
      displayLabel[len - 1] = '\0';
      len--;
    }

    // Strip trailing '%' if present (after removing spaces)
    if (len > 0 && displayLabel[len - 1] == '%') {
      displayLabel[len - 1] = '\0';
    }

    // Display: Label, colon, spaces, value, unit
    display.print(displayLabel);
    display.print(":");
    for (int i = 0; i < trailingSpaces; i++) {
      display.print(" ");
    }
    display.print(m.value);
    display.print(m.unit);

    // Check for companion metric (display on same line)
    if (m.companionId > 0) {
      // Find companion metric by ID
      for (int c = 0; c < metricData.count; c++) {
        if (metricData.metrics[c].id == m.companionId) {
          Metric& companion = metricData.metrics[c];
          display.print(" ");
          display.print(companion.value);
          display.print(companion.unit);
          break;
        }
      }
    }

    // Show timestamp on first visible metric (if clock enabled)
    if (idx == 0 && settings.showClock) {
      display.setCursor(85, currentY);
      display.print(metricData.timestamp);
    }

    // Progress bar for percentage metrics
    if (strcmp(m.unit, "%") == 0) {
      int barWidth = map(m.value, 0, 100, 0, 56);
      barWidth = constrain(barWidth, 0, 56);
      display.drawRect(70, currentY, 58, 8, SSD1306_WHITE);
      if (barWidth > 0) {
        display.fillRect(71, currentY + 1, barWidth, 6, SSD1306_WHITE);
      }
    } else if (strcmp(m.unit, "C") == 0) {
      // Temperature bar (0-100C scale)
      int barWidth = map(m.value, 0, 100, 0, 56);
      barWidth = constrain(barWidth, 0, 56);
      display.drawRect(70, currentY, 58, 8, SSD1306_WHITE);
      if (barWidth > 0) {
        display.fillRect(71, currentY + 1, barWidth, 6, SSD1306_WHITE);
      }
    }

    currentY += LINE_HEIGHT;
  }

  // No metrics shown edge case
  if (sortedCount == 0) {
    display.setCursor(0, 24);
    display.print("No metrics");
    display.setCursor(0, 36);
    display.print("selected");
  }
}

// Compact grid layout - position-based (6 rows max)
void displayStatsCompactGrid() {
  display.setTextSize(1);

  const int COL1_X = 0;
  const int COL2_X = 62;  // Moved 2px left to give right column more space

  // Dynamic row configuration based on user settings
  int startY;
  int ROW_HEIGHT;
  const int MAX_ROWS = (settings.displayRowMode == 0) ? 5 : 6;

  if (settings.displayRowMode == 0) {  // 5-row mode - optimized spacing
    startY = 0;  // Start at very top to maximize space
    // Use 13px spacing for maximum readability, except with centered clock (11px to fit)
    ROW_HEIGHT = (settings.showClock && settings.clockPosition == 0) ? 11 : 13;
  } else {  // 6-row mode - compact layout
    startY = 2;
    ROW_HEIGHT = 10;
  }

  // Clock positioning: 0=Center, 1=Left, 2=Right
  if (settings.showClock) {
    if (settings.clockPosition == 0) {
      // Center - Clock at top center, metrics below
      display.setCursor(48 + settings.clockOffset, startY);
      display.print(metricData.timestamp);
      startY += 10;  // Clock height (8px) + 2px gap
    } else if (settings.clockPosition == 1) {
      // Clock in left column, first row
      display.setCursor(COL1_X + settings.clockOffset, startY);
      display.print(metricData.timestamp);
    } else if (settings.clockPosition == 2) {
      // Clock in right column, first row
      display.setCursor(COL2_X + settings.clockOffset, startY);
      display.print(metricData.timestamp);
    }
  }

  // Render 6 rows using position-based system
  int visibleCount = 0;

  for (int row = 0; row < MAX_ROWS; row++) {
    int y = startY + (row * ROW_HEIGHT);

    // Check for overflow
    if (y + 8 > 64) break;

    // Calculate position indices for this row
    uint8_t leftPos = row * 2;      // 0, 2, 4, 6, 8, 10
    uint8_t rightPos = row * 2 + 1; // 1, 3, 5, 7, 9, 11

    // Skip first row left if clock is positioned there
    bool clockInLeft = (settings.showClock && settings.clockPosition == 1 && row == 0);
    bool clockInRight = (settings.showClock && settings.clockPosition == 2 && row == 0);

    // Find and render left column (check for bar first, then text)
    if (!clockInLeft) {
      bool rendered = false;

      // First check if any metric wants to display a bar at this position
      for (int i = 0; i < metricData.count; i++) {
        Metric& m = metricData.metrics[i];
        if (m.barPosition == leftPos) {
          drawProgressBar(COL1_X, y, 60, &m);  // Full-size bar for left column
          visibleCount++;
          rendered = true;
          break;
        }
      }

      // If no bar, check for text metric at this position
      if (!rendered) {
        for (int i = 0; i < metricData.count; i++) {
          Metric& m = metricData.metrics[i];
          if (m.position == leftPos) {
            display.setCursor(COL1_X, y);
            displayMetricCompact(&m);
            visibleCount++;
            break;
          }
        }
      }
    }

    // Find and render right column (check for bar first, then text)
    if (!clockInRight) {
      bool rendered = false;

      // First check if any metric wants to display a bar at this position
      for (int i = 0; i < metricData.count; i++) {
        Metric& m = metricData.metrics[i];
        if (m.barPosition == rightPos) {
          drawProgressBar(COL2_X, y, 64, &m);  // Full-size bar for right column
          visibleCount++;
          rendered = true;
          break;
        }
      }

      // If no bar, check for text metric at this position
      if (!rendered) {
        for (int i = 0; i < metricData.count; i++) {
          Metric& m = metricData.metrics[i];
          if (m.position == rightPos) {
            display.setCursor(COL2_X, y);
            displayMetricCompact(&m);
            visibleCount++;
            break;
          }
        }
      }
    }
  }

  // No metrics edge case
  if (visibleCount == 0) {
    display.setCursor(20, 28);
    display.print("No metrics");
  }
}

// Helper function to display a metric in compact format
void displayMetricCompact(Metric* m) {
  // Process label: convert '^' to spaces, strip trailing '%', move trailing spaces to after colon
  char displayLabel[METRIC_NAME_LEN];
  strncpy(displayLabel, m->label, METRIC_NAME_LEN - 1);
  displayLabel[METRIC_NAME_LEN - 1] = '\0';

  // Convert '^' to spaces for custom alignment
  convertCaretToSpaces(displayLabel);

  // Count and remove trailing spaces (to be added after colon)
  int labelLen = strlen(displayLabel);
  int trailingSpaces = 0;
  while (labelLen > 0 && displayLabel[labelLen - 1] == ' ') {
    trailingSpaces++;
    displayLabel[labelLen - 1] = '\0';
    labelLen--;
  }

  // Strip trailing '%' if present (after removing spaces)
  if (labelLen > 0 && displayLabel[labelLen - 1] == '%') {
    displayLabel[labelLen - 1] = '\0';
  }

  // Format: "LABEL: VAL" with spaces after colon if needed
  char text[40];
  char spaces[11] = "";  // Max 10 spaces
  for (int i = 0; i < trailingSpaces && i < 10; i++) {
    spaces[i] = ' ';
    spaces[i + 1] = '\0';
  }

  if (settings.useRpmKFormat && strcmp(m->unit, "RPM") == 0 && m->value >= 1000) {
    // RPM with K suffix: "FAN1: 1.2K"
    snprintf(text, 40, "%s:%s%.1fK", displayLabel, spaces, m->value / 1000.0);
  } else {
    // Normal: "CPU: 45%" or "FAN1: 1800RPM"
    snprintf(text, 40, "%s:%s%d%s", displayLabel, spaces, m->value, m->unit);
  }

  // Check for companion metric (append to same line)
  if (m->companionId > 0) {
    // Find companion metric by ID
    for (int c = 0; c < metricData.count; c++) {
      if (metricData.metrics[c].id == m->companionId) {
        Metric& companion = metricData.metrics[c];
        char companionText[15];
        snprintf(companionText, 15, " %d%s", companion.value, companion.unit);
        strncat(text, companionText, 40 - strlen(text) - 1);
        break;
      }
    }
  }

  display.print(text);
}

// Helper function to draw a full-size progress bar (occupies entire position slot)
void drawProgressBar(int x, int y, int width, Metric* m) {
  // Apply custom width and offset
  int actualX = x + m->barOffsetX;
  int actualWidth = m->barWidth;

  // Constrain to ensure bar doesn't exceed screen boundaries
  if (actualX + actualWidth > 128) {
    actualWidth = 128 - actualX;
  }

  // Calculate bar fill percentage based on min/max values
  int range = m->barMax - m->barMin;
  if (range <= 0) range = 100;  // Avoid division by zero

  int valueInRange = constrain(m->value, m->barMin, m->barMax) - m->barMin;
  int fillWidth = map(valueInRange, 0, range, 0, actualWidth - 2);

  // Draw bar outline (8px tall, full row height)
  display.drawRect(actualX, y, actualWidth, 8, SSD1306_WHITE);

  // Fill bar based on value
  if (fillWidth > 0) {
    display.fillRect(actualX + 1, y + 1, fillWidth, 6, SSD1306_WHITE);
  }
}

// Main display function - always uses compact grid layout
void displayStats() {
  displayStatsCompactGrid();   // Compact 2-column grid layout
}

// ========== Standard Clock Display ==========
void displayStandardClock() {
  struct tm timeinfo;
  if(!getTimeWithTimeout(&timeinfo)) {
    display.setTextSize(1);
    display.setCursor(20, 28);
    if (!ntpSynced) {
      display.print("Syncing time...");
    } else {
      display.print("Time Error");
    }
    return;
  }
  
  // Time display
  display.setTextSize(3);
  char timeStr[9];
  
  int displayHour = timeinfo.tm_hour;
  bool isPM = false;
  
  if (!settings.use24Hour) {
    isPM = displayHour >= 12;
    displayHour = displayHour % 12;
    if (displayHour == 0) displayHour = 12;
  }
  
  sprintf(timeStr, "%02d:%02d", displayHour, timeinfo.tm_min);
  
  // Center time
  int time_width = 5 * 18;  // 5 chars * 18px
  int time_x = (SCREEN_WIDTH - time_width) / 2;
  display.setCursor(time_x, 8);
  display.print(timeStr);
  
  // AM/PM indicator for 12-hour format
  if (!settings.use24Hour) {
    display.setTextSize(1);
    display.setCursor(110, 8);
    display.print(isPM ? "PM" : "AM");
  }
  
  // Date display
  display.setTextSize(1);
  char dateStr[12];
  
  switch (settings.dateFormat) {
    case 0:  // DD/MM/YYYY
      sprintf(dateStr, "%02d/%02d/%04d", timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900);
      break;
    case 1:  // MM/DD/YYYY
      sprintf(dateStr, "%02d/%02d/%04d", timeinfo.tm_mon + 1, timeinfo.tm_mday, timeinfo.tm_year + 1900);
      break;
    case 2:  // YYYY-MM-DD
      sprintf(dateStr, "%04d-%02d-%02d", timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday);
      break;
  }
  
  int date_x = (SCREEN_WIDTH - 60) / 2;
  display.setCursor(date_x, 38);
  display.print(dateStr);
  
  // Day of week
  const char* days[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
  const char* dayName = days[timeinfo.tm_wday];
  int day_width = strlen(dayName) * 6;
  int day_x = (SCREEN_WIDTH - day_width) / 2;
  display.setCursor(day_x, 52);
  display.print(dayName);
}

// ========== Large Clock Display ==========
void displayLargeClock() {
  struct tm timeinfo;
  if(!getTimeWithTimeout(&timeinfo)) {
    display.setTextSize(1);
    display.setCursor(20, 28);
    if (!ntpSynced) {
      display.print("Syncing time...");
    } else {
      display.print("Time Error");
    }
    return;
  }
  
  int displayHour = timeinfo.tm_hour;
  bool isPM = false;
  
  if (!settings.use24Hour) {
    isPM = displayHour >= 12;
    displayHour = displayHour % 12;
    if (displayHour == 0) displayHour = 12;
  }
  
  // Large time display - size 4 (24px per char)
  display.setTextSize(4);
  char timeStr[6];
  sprintf(timeStr, "%02d:%02d", displayHour, timeinfo.tm_min);
  
  // Center time: 5 chars * 24px = 120px, centered in 128px
  int time_x = (SCREEN_WIDTH - 120) / 2;
  display.setCursor(time_x, 4);
  display.print(timeStr);
  
  // AM/PM indicator for 12-hour format
  if (!settings.use24Hour) {
    display.setTextSize(1);
    display.setCursor(116, 4);
    display.print(isPM ? "PM" : "AM");
  }
  
  // Date at bottom
  display.setTextSize(1);
  char dateStr[12];
  
  switch (settings.dateFormat) {
    case 0:  // DD/MM/YYYY
      sprintf(dateStr, "%02d/%02d/%04d", timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900);
      break;
    case 1:  // MM/DD/YYYY
      sprintf(dateStr, "%02d/%02d/%04d", timeinfo.tm_mon + 1, timeinfo.tm_mday, timeinfo.tm_year + 1900);
      break;
    case 2:  // YYYY-MM-DD
      sprintf(dateStr, "%04d-%02d-%02d", timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday);
      break;
  }
  
  int date_x = (SCREEN_WIDTH - 60) / 2;
  display.setCursor(date_x, 54);
  display.print(dateStr);
}

// ========== Mario Clock Functions ==========
void triggerDigitBounce(int digitIndex) {
  if (digitIndex >= 0 && digitIndex < 5) {
    digit_velocity[digitIndex] = DIGIT_BOUNCE_POWER;
  }
}

void updateDigitBounce() {
  for (int i = 0; i < 5; i++) {
    if (digit_offset_y[i] != 0 || digit_velocity[i] != 0) {
      digit_velocity[i] += DIGIT_GRAVITY;
      digit_offset_y[i] += digit_velocity[i];
      
      if (digit_offset_y[i] >= 0) {
        digit_offset_y[i] = 0;
        digit_velocity[i] = 0;
      }
    }
  }
}

void drawTimeWithBounce() {
  display.setTextSize(3);
  
  char digits[5];
  digits[0] = '0' + (displayed_hour / 10);
  digits[1] = '0' + (displayed_hour % 10);
  digits[2] = ':';
  digits[3] = '0' + (displayed_min / 10);
  digits[4] = '0' + (displayed_min % 10);
  
  for (int i = 0; i < 5; i++) {
    int y = TIME_Y + (int)digit_offset_y[i];
    display.setCursor(DIGIT_X[i], y);
    display.print(digits[i]);
  }
}

void displayClockWithMario() {
  struct tm timeinfo;
  if(!getTimeWithTimeout(&timeinfo)) {
    display.setTextSize(1);
    display.setCursor(20, 28);
    if (!ntpSynced) {
      display.print("Syncing time...");
    } else {
      display.print("Time Error");
    }
    return;
  }
  
  if (!time_overridden) {
    displayed_hour = timeinfo.tm_hour;
    displayed_min = timeinfo.tm_min;
  }
  
  if (time_overridden && timeinfo.tm_hour == displayed_hour && timeinfo.tm_min == displayed_min) {
    time_overridden = false;
  }
  
  // Date at top
  display.setTextSize(1);
  char dateStr[12];
  
  switch (settings.dateFormat) {
    case 0:
      sprintf(dateStr, "%02d/%02d/%04d", timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900);
      break;
    case 1:
      sprintf(dateStr, "%02d/%02d/%04d", timeinfo.tm_mon + 1, timeinfo.tm_mday, timeinfo.tm_year + 1900);
      break;
    case 2:
      sprintf(dateStr, "%04d-%02d-%02d", timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday);
      break;
  }
  
  int date_x = (SCREEN_WIDTH - 60) / 2;
  display.setCursor(date_x, 4);
  display.print(dateStr);
  
  updateDigitBounce();
  drawTimeWithBounce();
  
  updateMarioAnimation(&timeinfo);
  
  int mario_draw_y = mario_base_y + (int)mario_jump_y;
  bool isJumping = (mario_state == MARIO_JUMPING);
  drawMario((int)mario_x, mario_draw_y, mario_facing_right, mario_walk_frame, isJumping);
}

void advanceDisplayedTime() {
  displayed_min++;
  if (displayed_min >= 60) {
    displayed_min = 0;
    displayed_hour++;
    if (displayed_hour >= 24) {
      displayed_hour = 0;
    }
  }
  time_overridden = true;
}

void updateSpecificDigit(int digitIndex, int newValue) {
  // Update the specific digit in displayed_hour or displayed_min
  // digitIndex corresponds to DIGIT_X array: 0=hour tens, 1=hour ones, 3=min tens, 4=min ones
  int hour_tens = displayed_hour / 10;
  int hour_ones = displayed_hour % 10;
  int min_tens = displayed_min / 10;
  int min_ones = displayed_min % 10;

  if (digitIndex == 0) {
    hour_tens = newValue;
    displayed_hour = hour_tens * 10 + hour_ones;
  } else if (digitIndex == 1) {
    hour_ones = newValue;
    displayed_hour = hour_tens * 10 + hour_ones;
  } else if (digitIndex == 3) {
    min_tens = newValue;
    displayed_min = min_tens * 10 + min_ones;
  } else if (digitIndex == 4) {
    min_ones = newValue;
    displayed_min = min_tens * 10 + min_ones;
  }

  time_overridden = true;
}

void calculateTargetDigits(int hour, int min) {
  num_targets = 0;

  int next_min = (min + 1) % 60;
  int next_hour = hour;
  if (next_min == 0) {
    next_hour = (hour + 1) % 24;
  }

  int curr_digits[4] = {hour / 10, hour % 10, min / 10, min % 10};
  int next_digits[4] = {next_hour / 10, next_hour % 10, next_min / 10, next_min % 10};

  // Add targets from LEFT to RIGHT (hour first, then minutes)
  // Text size 3 digits are 15px wide (5*3), center is at +7
  if (curr_digits[0] != next_digits[0]) {
    target_x_positions[num_targets] = DIGIT_X[0] + 7;
    target_digit_index[num_targets] = 0;
    target_digit_values[num_targets] = next_digits[0];
    num_targets++;
  }
  if (curr_digits[1] != next_digits[1]) {
    target_x_positions[num_targets] = DIGIT_X[1] + 7;
    target_digit_index[num_targets] = 1;
    target_digit_values[num_targets] = next_digits[1];
    num_targets++;
  }
  if (curr_digits[2] != next_digits[2]) {
    target_x_positions[num_targets] = DIGIT_X[3] + 7;
    target_digit_index[num_targets] = 3;
    target_digit_values[num_targets] = next_digits[2];
    num_targets++;
  }
  if (curr_digits[3] != next_digits[3]) {
    target_x_positions[num_targets] = DIGIT_X[4] + 7;
    target_digit_index[num_targets] = 4;
    target_digit_values[num_targets] = next_digits[3];
    num_targets++;
  }
}

void updateMarioAnimation(struct tm* timeinfo) {
  unsigned long currentMillis = millis();
  
  if (currentMillis - last_mario_update < MARIO_ANIM_SPEED) {
    return;
  }
  last_mario_update = currentMillis;
  
  int seconds = timeinfo->tm_sec;
  int current_minute = timeinfo->tm_min;
  
  if (current_minute != last_minute) {
    last_minute = current_minute;
    animation_triggered = false;
  }
  
  if (seconds >= 55 && !animation_triggered && mario_state == MARIO_IDLE) {
    animation_triggered = true;
    calculateTargetDigits(displayed_hour, displayed_min);
    if (num_targets > 0) {
      current_target_index = 0;
      mario_x = -15;
      mario_state = MARIO_WALKING;
      mario_facing_right = true;
      digit_bounce_triggered = false;
    }
  }
  
  switch (mario_state) {
    case MARIO_IDLE:
      mario_walk_frame = 0;
      mario_x = -15;
      break;
      
    case MARIO_WALKING:
      if (current_target_index < num_targets) {
        int target = target_x_positions[current_target_index];
        
        if (abs(mario_x - target) > 3) {
          if (mario_x < target) {
            mario_x += 2.5;
            mario_facing_right = true;
          } else {
            mario_x -= 2.5;
            mario_facing_right = false;
          }
          mario_walk_frame = (mario_walk_frame + 1) % 2;
        } else {
          mario_x = target;
          mario_state = MARIO_JUMPING;
          jump_velocity = JUMP_POWER;
          mario_jump_y = 0;
          digit_bounce_triggered = false;
        }
      } else {
        mario_state = MARIO_WALKING_OFF;
        mario_facing_right = true;
      }
      break;
      
    case MARIO_JUMPING:
      {
        jump_velocity += GRAVITY;
        mario_jump_y += jump_velocity;

        int mario_head_y = mario_base_y + (int)mario_jump_y - MARIO_HEAD_OFFSET;

        if (!digit_bounce_triggered && mario_head_y <= DIGIT_BOTTOM) {
          digit_bounce_triggered = true;
          triggerDigitBounce(target_digit_index[current_target_index]);

          // Update only the specific digit that Mario just hit
          updateSpecificDigit(target_digit_index[current_target_index],
                             target_digit_values[current_target_index]);

          jump_velocity = 2.0;
        }

        if (mario_jump_y >= 0) {
          mario_jump_y = 0;
          jump_velocity = 0;

          current_target_index++;

          if (current_target_index < num_targets) {
            mario_state = MARIO_WALKING;
            mario_facing_right = (target_x_positions[current_target_index] > mario_x);
            digit_bounce_triggered = false;
          } else {
            mario_state = MARIO_WALKING_OFF;
            mario_facing_right = true;
          }
        }
      }
      break;
      
    case MARIO_WALKING_OFF:
      mario_x += 2.5;
      mario_walk_frame = (mario_walk_frame + 1) % 2;
      
      if (mario_x > SCREEN_WIDTH + 15) {
        mario_state = MARIO_IDLE;
        mario_x = -15;
      }
      break;
  }
}

void drawMario(int x, int y, bool facingRight, int frame, bool jumping) {
  if (x < -10 || x > SCREEN_WIDTH + 10) return;

  int sx = x - 4;
  int sy = y - 10;

  if (jumping) {
    display.fillRect(sx + 2, sy, 4, 3, SSD1306_WHITE);
    display.fillRect(sx + 2, sy + 3, 4, 3, SSD1306_WHITE);
    display.drawPixel(sx + 1, sy + 2, SSD1306_WHITE);
    display.drawPixel(sx + 6, sy + 2, SSD1306_WHITE);
    display.drawPixel(sx + 0, sy + 1, SSD1306_WHITE);
    display.drawPixel(sx + 7, sy + 1, SSD1306_WHITE);
    display.fillRect(sx + 2, sy + 6, 2, 3, SSD1306_WHITE);
    display.fillRect(sx + 4, sy + 6, 2, 3, SSD1306_WHITE);
  } else {
    display.fillRect(sx + 2, sy, 4, 3, SSD1306_WHITE);
    if (facingRight) {
      display.drawPixel(sx + 6, sy + 1, SSD1306_WHITE);
    } else {
      display.drawPixel(sx + 1, sy + 1, SSD1306_WHITE);
    }

    display.fillRect(sx + 2, sy + 3, 4, 3, SSD1306_WHITE);

    if (facingRight) {
      display.drawPixel(sx + 1, sy + 4, SSD1306_WHITE);
      display.drawPixel(sx + 6, sy + 3 + (frame % 2), SSD1306_WHITE);
    } else {
      display.drawPixel(sx + 6, sy + 4, SSD1306_WHITE);
      display.drawPixel(sx + 1, sy + 3 + (frame % 2), SSD1306_WHITE);
    }

    if (frame == 0) {
      display.fillRect(sx + 2, sy + 6, 2, 3, SSD1306_WHITE);
      display.fillRect(sx + 4, sy + 6, 2, 3, SSD1306_WHITE);
    } else {
      display.fillRect(sx + 1, sy + 6, 2, 3, SSD1306_WHITE);
      display.fillRect(sx + 5, sy + 6, 2, 3, SSD1306_WHITE);
    }
  }
}

// ========== Space Invader Animation Functions (Clock Style 3 - Sliding) ==========

// Draw Space Invaders alien sprite (11x11 pixels, classic invader design)
void drawInvader(int x, int y, int frame) {
  // Bounds check
  if (x < -12 || x > SCREEN_WIDTH + 12) return;
  if (y < -10 || y > SCREEN_HEIGHT + 10) return;

  int sx = x - 5;
  int sy = y - 4;

  // Antennae
  display.drawPixel(sx + 2, sy, SSD1306_WHITE);
  display.drawPixel(sx + 8, sy, SSD1306_WHITE);

  // Head
  display.fillRect(sx + 3, sy + 1, 5, 1, SSD1306_WHITE);

  // Body
  display.fillRect(sx + 2, sy + 2, 7, 1, SSD1306_WHITE);
  display.fillRect(sx + 1, sy + 3, 9, 1, SSD1306_WHITE);

  // Eyes
  display.fillRect(sx, sy + 4, 3, 1, SSD1306_WHITE);
  display.drawPixel(sx + 5, sy + 4, SSD1306_WHITE);
  display.fillRect(sx + 8, sy + 4, 3, 1, SSD1306_WHITE);

  // Mouth
  display.fillRect(sx, sy + 5, 11, 1, SSD1306_WHITE);

  // Legs (frame-dependent)
  if (frame == 0) {
    // Legs down
    display.drawPixel(sx + 1, sy + 6, SSD1306_WHITE);
    display.fillRect(sx + 4, sy + 6, 3, 1, SSD1306_WHITE);
    display.drawPixel(sx + 9, sy + 6, SSD1306_WHITE);
    display.fillRect(sx, sy + 7, 2, 1, SSD1306_WHITE);
    display.drawPixel(sx + 5, sy + 7, SSD1306_WHITE);
    display.fillRect(sx + 9, sy + 7, 2, 1, SSD1306_WHITE);
  } else {
    // Legs up
    display.fillRect(sx + 2, sy + 6, 7, 1, SSD1306_WHITE);
    display.drawPixel(sx + 1, sy + 7, SSD1306_WHITE);
    display.drawPixel(sx + 9, sy + 7, SSD1306_WHITE);
    display.fillRect(sx, sy + 8, 2, 1, SSD1306_WHITE);
    display.fillRect(sx + 9, sy + 8, 2, 1, SSD1306_WHITE);
  }
}

// Handle patrol state - slow left-right drift
void handleInvaderPatrolState() {
  invader_x += INVADER_PATROL_SPEED * invader_patrol_direction;

  // Reverse direction at boundaries
  if (invader_x <= INVADER_PATROL_LEFT) {
    invader_x = INVADER_PATROL_LEFT;
    invader_patrol_direction = 1;
  } else if (invader_x >= INVADER_PATROL_RIGHT) {
    invader_x = INVADER_PATROL_RIGHT;
    invader_patrol_direction = -1;
  }
}

// Handle sliding to target position - fast horizontal movement
void handleInvaderSlidingState() {
  float target_x = target_x_positions[current_target_index];

  // Slide horizontally to target
  if (abs(invader_x - target_x) > 1.0) {
    if (invader_x < target_x) {
      invader_x += INVADER_SLIDE_SPEED;
      if (invader_x > target_x) invader_x = target_x;
    } else {
      invader_x -= INVADER_SLIDE_SPEED;
      if (invader_x < target_x) invader_x = target_x;
    }
  } else {
    // Reached target position - start shooting
    invader_x = target_x;
    invader_state = INVADER_SHOOTING;
    fireInvaderLaser(target_digit_index[current_target_index]);
  }
}

// Handle shooting state - laser update handles transition
void handleInvaderShootingState() {
  // Laser update handles transition to EXPLODING_DIGIT
}

// Handle exploding state - move away quickly after 5 frames
void handleInvaderExplodingState() {
  invader_explosion_timer++;
  // Move away quickly - don't wait for explosion to finish
  if (invader_explosion_timer >= 5) {
    current_target_index++;
    if (current_target_index < num_targets) {
      invader_state = INVADER_MOVING_NEXT;
    } else {
      invader_state = INVADER_RETURNING;
    }
  }
}

// Handle moving to next target - slide to next digit
void handleInvaderMovingNextState() {
  float target_x = target_x_positions[current_target_index];

  if (abs(invader_x - target_x) > 1.0) {
    if (invader_x < target_x) {
      invader_x += INVADER_SLIDE_SPEED;
      if (invader_x > target_x) invader_x = target_x;
    } else {
      invader_x -= INVADER_SLIDE_SPEED;
      if (invader_x < target_x) invader_x = target_x;
    }
  } else {
    invader_x = target_x;
    invader_state = INVADER_SHOOTING;
    fireInvaderLaser(target_digit_index[current_target_index]);
  }
}

// Handle returning to patrol - slide back to center
void handleInvaderReturningState() {
  float center_x = 64;

  if (abs(invader_x - center_x) > 1.0) {
    if (invader_x < center_x) {
      invader_x += INVADER_PATROL_SPEED;
      if (invader_x > center_x) invader_x = center_x;
    } else {
      invader_x -= INVADER_PATROL_SPEED;
      if (invader_x < center_x) invader_x = center_x;
    }
  } else {
    invader_x = center_x;
    invader_state = INVADER_PATROL;
    time_overridden = false;  // Allow time to resync
  }
}

// Draw invader laser beam (upward)
void drawInvaderLaser(Laser* laser) {
  if (!laser->active) return;

  // Vertical laser beam shooting UPWARD
  for (int i = 0; i < (int)laser->length; i += 2) {
    int ly = (int)laser->y - i;  // Subtract to go upward
    if (ly >= 0 && ly < SCREEN_HEIGHT) {
      display.drawPixel((int)laser->x, ly, SSD1306_WHITE);
      display.drawPixel((int)laser->x + 1, ly, SSD1306_WHITE);
    }
  }

  // Impact flash at end (top of beam)
  int end_y = (int)(laser->y - laser->length);
  if (end_y >= 0 && end_y < SCREEN_HEIGHT) {
    display.drawPixel((int)laser->x - 1, end_y, SSD1306_WHITE);
    display.drawPixel((int)laser->x + 2, end_y, SSD1306_WHITE);
  }
}

// Update invader laser
void updateInvaderLaser() {
  if (!invader_laser.active) return;

  invader_laser.length += LASER_EXTEND_SPEED;

  // Check if reached digit (bottom of time digits)
  const int INVADER_TIME_Y = 16;
  int digit_bottom_y = INVADER_TIME_Y + 24;
  int laser_end_y = invader_laser.y - invader_laser.length;

  if (laser_end_y <= digit_bottom_y) {
    invader_laser.active = false;
    spawnInvaderExplosion(invader_laser.target_digit_idx);
    updateSpecificDigit(target_digit_index[current_target_index],
                       target_digit_values[current_target_index]);
    invader_explosion_timer = 0;
    invader_state = INVADER_EXPLODING_DIGIT;
  }

  if (invader_laser.length > LASER_MAX_LENGTH) {
    invader_laser.length = LASER_MAX_LENGTH;
  }
}

// Fire invader laser
void fireInvaderLaser(int target_digit_idx) {
  invader_laser.x = invader_x;
  invader_laser.y = invader_y - 4;  // Start from top of invader
  invader_laser.length = 0;
  invader_laser.active = true;
  invader_laser.target_digit_idx = target_digit_idx;
}

// Spawn invader explosion fragments
void spawnInvaderExplosion(int digitIndex) {
  const int INVADER_TIME_Y = 16;
  int digit_x = DIGIT_X[digitIndex] + 9;
  int digit_y = INVADER_TIME_Y + 12;

  int frag_count = 10;
  float angle_step = (2 * PI) / frag_count;

  for (int i = 0; i < frag_count; i++) {
    InvaderFragment* f = findFreeInvaderFragment();
    if (!f) break;

    float angle = i * angle_step + random(-30, 30) / 100.0;
    float speed = INVADER_FRAG_SPEED + random(-50, 50) / 100.0;

    f->x = digit_x + random(-4, 4);
    f->y = digit_y + random(-6, 6);
    f->vx = cos(angle) * speed;
    f->vy = sin(angle) * speed - 1.0;
    f->active = true;
  }
}

// Update invader fragments
void updateInvaderFragments() {
  for (int i = 0; i < MAX_INVADER_FRAGMENTS; i++) {
    if (invader_fragments[i].active) {
      invader_fragments[i].vy += INVADER_FRAG_GRAVITY;
      invader_fragments[i].x += invader_fragments[i].vx;
      invader_fragments[i].y += invader_fragments[i].vy;

      if (invader_fragments[i].y > 70 ||
          invader_fragments[i].x < -5 ||
          invader_fragments[i].x > 133) {
        invader_fragments[i].active = false;
      }
    }
  }
}

// Draw invader fragments
void drawInvaderFragments() {
  for (int i = 0; i < MAX_INVADER_FRAGMENTS; i++) {
    if (invader_fragments[i].active) {
      display.fillRect((int)invader_fragments[i].x,
                      (int)invader_fragments[i].y, 2, 2, SSD1306_WHITE);
    }
  }
}

// Check if all invader fragments are inactive
bool allInvaderFragmentsInactive() {
  for (int i = 0; i < MAX_INVADER_FRAGMENTS; i++) {
    if (invader_fragments[i].active) return false;
  }
  return true;
}

// Find free invader fragment
InvaderFragment* findFreeInvaderFragment() {
  for (int i = 0; i < MAX_INVADER_FRAGMENTS; i++) {
    if (!invader_fragments[i].active) return &invader_fragments[i];
  }
  return nullptr;
}

// Main invader animation update
void updateInvaderAnimation(struct tm* timeinfo) {
  unsigned long currentMillis = millis();

  const int INVADER_ANIM_SPEED = 50;  // 50ms = 20 FPS
  const int SPRITE_TOGGLE_SPEED = 200;  // Slow retro animation

  if (currentMillis - last_invader_update < INVADER_ANIM_SPEED) return;
  last_invader_update = currentMillis;

  int seconds = timeinfo->tm_sec;
  int current_minute = timeinfo->tm_min;

  // Reset trigger
  if (current_minute != last_minute) {
    last_minute = current_minute;
    animation_triggered = false;
  }

  // Toggle sprite
  if (currentMillis - last_invader_sprite_toggle >= SPRITE_TOGGLE_SPEED) {
    invader_anim_frame = 1 - invader_anim_frame;
    last_invader_sprite_toggle = currentMillis;
  }

  // Trigger at 55 seconds - transition from PATROL to SLIDING
  if (seconds >= 55 && !animation_triggered && invader_state == INVADER_PATROL) {
    animation_triggered = true;
    time_overridden = true;
    calculateTargetDigits(displayed_hour, displayed_min);

    if (num_targets > 0) {
      current_target_index = 0;
      invader_state = INVADER_SLIDING;
    }
  }

  updateInvaderFragments();
  updateInvaderLaser();

  switch (invader_state) {
    case INVADER_PATROL:
      handleInvaderPatrolState();
      break;
    case INVADER_SLIDING:
      handleInvaderSlidingState();
      break;
    case INVADER_SHOOTING:
      handleInvaderShootingState();
      break;
    case INVADER_EXPLODING_DIGIT:
      handleInvaderExplodingState();
      break;
    case INVADER_MOVING_NEXT:
      handleInvaderMovingNextState();
      break;
    case INVADER_RETURNING:
      handleInvaderReturningState();
      break;
  }
}

// Display clock with invader animation
void displayClockWithInvader() {
  struct tm timeinfo;
  if(!getTimeWithTimeout(&timeinfo)) {
    display.setTextSize(1);
    display.setCursor(20, 28);
    if (!ntpSynced) {
      display.print("Syncing time...");
    } else {
      display.print("Time Error");
    }
    return;
  }

  // Update animation FIRST so time advances before drawing
  updateInvaderAnimation(&timeinfo);

  // Time management (same as ship)
  if (!time_overridden) {
    displayed_hour = timeinfo.tm_hour;
    displayed_min = timeinfo.tm_min;
  }

  // Reset time_overridden when real time catches up AND invader is in PATROL state
  if (time_overridden && timeinfo.tm_hour == displayed_hour &&
      timeinfo.tm_min == displayed_min && invader_state == INVADER_PATROL) {
    time_overridden = false;
  }

  // Date (at top, Y=4)
  display.setTextSize(1);
  char dateStr[12];
  switch (settings.dateFormat) {
    case 0: sprintf(dateStr, "%02d/%02d/%04d", timeinfo.tm_mday,
                    timeinfo.tm_mon + 1, timeinfo.tm_year + 1900); break;
    case 1: sprintf(dateStr, "%02d/%02d/%04d", timeinfo.tm_mon + 1,
                    timeinfo.tm_mday, timeinfo.tm_year + 1900); break;
    case 2: sprintf(dateStr, "%04d-%02d-%02d", timeinfo.tm_year + 1900,
                    timeinfo.tm_mon + 1, timeinfo.tm_mday); break;
  }
  display.setCursor((SCREEN_WIDTH - 60) / 2, 4);
  display.print(dateStr);

  // Time digits (Invader uses Y=16, same as ship)
  const int INVADER_TIME_Y = 16;
  display.setTextSize(3);
  char digits[5];
  digits[0] = '0' + (displayed_hour / 10);
  digits[1] = '0' + (displayed_hour % 10);
  digits[2] = ':';
  digits[3] = '0' + (displayed_min / 10);
  digits[4] = '0' + (displayed_min % 10);

  for (int i = 0; i < 5; i++) {
    display.setCursor(DIGIT_X[i], INVADER_TIME_Y);
    display.print(digits[i]);
  }

  // Render invader (ALWAYS visible - either patrolling or attacking)
  drawInvader((int)invader_x, (int)invader_y, invader_anim_frame);

  // Render laser if active
  if (invader_laser.active) {
    drawInvaderLaser(&invader_laser);
  }

  // Render explosion fragments
  drawInvaderFragments();
}


// ========== Space Ship Animation Functions (Clock Style 4) ==========

// Draw Space Invaders ship sprite (11x7 pixels, classic ship design)
void drawShip(int x, int y, int frame) {
  // Bounds check
  if (x < -12 || x > SCREEN_WIDTH + 12) return;
  if (y < -10 || y > SCREEN_HEIGHT + 10) return;

  int sx = x - 5;
  int sy = y - 3;

  // Top point
  display.drawPixel(sx + 5, sy, SSD1306_WHITE);

  // Upper body
  display.fillRect(sx + 4, sy + 1, 3, 1, SSD1306_WHITE);
  display.fillRect(sx + 3, sy + 2, 5, 1, SSD1306_WHITE);

  // Main body
  display.fillRect(sx + 1, sy + 3, 9, 1, SSD1306_WHITE);
  display.fillRect(sx, sy + 4, 11, 1, SSD1306_WHITE);

  // Wings - animate between two frames for thruster effect
  if (frame == 0) {
    // Wings down
    display.fillRect(sx, sy + 5, 3, 1, SSD1306_WHITE);
    display.fillRect(sx + 8, sy + 5, 3, 1, SSD1306_WHITE);
    display.drawPixel(sx, sy + 6, SSD1306_WHITE);
    display.drawPixel(sx + 10, sy + 6, SSD1306_WHITE);
  } else {
    // Wings up (thruster pulse)
    display.fillRect(sx + 1, sy + 5, 2, 1, SSD1306_WHITE);
    display.fillRect(sx + 8, sy + 5, 2, 1, SSD1306_WHITE);
    display.drawPixel(sx + 1, sy + 6, SSD1306_WHITE);
    display.drawPixel(sx + 9, sy + 6, SSD1306_WHITE);
  }
}

// Handle patrol state - slow left-right drift
void handleShipPatrolState() {
  ship_x += SHIP_PATROL_SPEED * ship_patrol_direction;

  // Reverse direction at boundaries
  if (ship_x <= SHIP_PATROL_LEFT) {
    ship_x = SHIP_PATROL_LEFT;
    ship_patrol_direction = 1;
  } else if (ship_x >= SHIP_PATROL_RIGHT) {
    ship_x = SHIP_PATROL_RIGHT;
    ship_patrol_direction = -1;
  }
}

// Handle sliding to target position - fast horizontal movement
void handleShipSlidingState() {
  float target_x = target_x_positions[current_target_index];

  // Slide horizontally to target
  if (abs(ship_x - target_x) > 1.0) {
    if (ship_x < target_x) {
      ship_x += SHIP_SLIDE_SPEED;
      if (ship_x > target_x) ship_x = target_x;
    } else {
      ship_x -= SHIP_SLIDE_SPEED;
      if (ship_x < target_x) ship_x = target_x;
    }
  } else {
    // Reached target position - start shooting
    ship_x = target_x;
    ship_state = SHIP_SHOOTING;
    fireShipLaser(target_digit_index[current_target_index]);
  }
}

// Handle shooting state - laser update handles transition
void handleShipShootingState() {
  // Laser update handles transition to EXPLODING_DIGIT
}

// Handle exploding state - move away quickly after 5 frames
void handleShipExplodingState() {
  ship_explosion_timer++;
  // Move away quickly - don't wait for explosion to finish
  if (ship_explosion_timer >= 5) {
    current_target_index++;
    if (current_target_index < num_targets) {
      ship_state = SHIP_MOVING_NEXT;
    } else {
      ship_state = SHIP_RETURNING;
    }
  }
}

// Handle moving to next target - slide to next digit
void handleShipMovingNextState() {
  float target_x = target_x_positions[current_target_index];

  if (abs(ship_x - target_x) > 1.0) {
    if (ship_x < target_x) {
      ship_x += SHIP_SLIDE_SPEED;
      if (ship_x > target_x) ship_x = target_x;
    } else {
      ship_x -= SHIP_SLIDE_SPEED;
      if (ship_x < target_x) ship_x = target_x;
    }
  } else {
    ship_x = target_x;
    ship_state = SHIP_SHOOTING;
    fireShipLaser(target_digit_index[current_target_index]);
  }
}

// Handle returning to patrol - slide back to center
void handleShipReturningState() {
  float center_x = 64;

  if (abs(ship_x - center_x) > 1.0) {
    if (ship_x < center_x) {
      ship_x += SHIP_PATROL_SPEED;
      if (ship_x > center_x) ship_x = center_x;
    } else {
      ship_x -= SHIP_PATROL_SPEED;
      if (ship_x < center_x) ship_x = center_x;
    }
  } else {
    ship_x = center_x;
    ship_state = SHIP_PATROL;
    time_overridden = false;  // Allow time to resync
  }
}

// Draw ship laser beam (upward)
void drawShipLaser(Laser* laser) {
  if (!laser->active) return;

  // Vertical laser beam shooting UPWARD
  for (int i = 0; i < (int)laser->length; i += 2) {
    int ly = (int)laser->y - i;  // Subtract to go upward
    if (ly >= 0 && ly < SCREEN_HEIGHT) {
      display.drawPixel((int)laser->x, ly, SSD1306_WHITE);
      display.drawPixel((int)laser->x + 1, ly, SSD1306_WHITE);
    }
  }

  // Impact flash at end (top of beam)
  int end_y = (int)(laser->y - laser->length);
  if (end_y >= 0 && end_y < SCREEN_HEIGHT) {
    display.drawPixel((int)laser->x - 1, end_y, SSD1306_WHITE);
    display.drawPixel((int)laser->x + 2, end_y, SSD1306_WHITE);
  }
}

// Update ship laser
void updateShipLaser() {
  if (!ship_laser.active) return;

  ship_laser.length += LASER_EXTEND_SPEED;

  // Check if reached digit (bottom of time digits)
  const int SHIP_TIME_Y = 16;
  int digit_bottom_y = SHIP_TIME_Y + 24;
  int laser_end_y = ship_laser.y - ship_laser.length;

  if (laser_end_y <= digit_bottom_y) {
    ship_laser.active = false;
    spawnShipExplosion(ship_laser.target_digit_idx);
    updateSpecificDigit(target_digit_index[current_target_index],
                       target_digit_values[current_target_index]);
    ship_explosion_timer = 0;
    ship_state = SHIP_EXPLODING_DIGIT;
  }

  if (ship_laser.length > LASER_MAX_LENGTH) {
    ship_laser.length = LASER_MAX_LENGTH;
  }
}

// Fire ship laser
void fireShipLaser(int target_digit_idx) {
  ship_laser.x = ship_x;
  ship_laser.y = ship_y - 3;  // Start from top of ship
  ship_laser.length = 0;
  ship_laser.active = true;
  ship_laser.target_digit_idx = target_digit_idx;
}

// Spawn ship explosion fragments
void spawnShipExplosion(int digitIndex) {
  const int SHIP_TIME_Y = 16;
  int digit_x = DIGIT_X[digitIndex] + 9;
  int digit_y = SHIP_TIME_Y + 12;

  int frag_count = 10;
  float angle_step = (2 * PI) / frag_count;

  for (int i = 0; i < frag_count; i++) {
    ShipFragment* f = findFreeShipFragment();
    if (!f) break;

    float angle = i * angle_step + random(-30, 30) / 100.0;
    float speed = INVADER_FRAG_SPEED + random(-50, 50) / 100.0;

    f->x = digit_x + random(-4, 4);
    f->y = digit_y + random(-6, 6);
    f->vx = cos(angle) * speed;
    f->vy = sin(angle) * speed - 1.0;
    f->active = true;
  }
}

// Update ship fragments
void updateShipFragments() {
  for (int i = 0; i < MAX_INVADER_FRAGMENTS; i++) {
    if (ship_fragments[i].active) {
      ship_fragments[i].vy += INVADER_FRAG_GRAVITY;
      ship_fragments[i].x += ship_fragments[i].vx;
      ship_fragments[i].y += ship_fragments[i].vy;

      if (ship_fragments[i].y > 70 ||
          ship_fragments[i].x < -5 ||
          ship_fragments[i].x > 133) {
        ship_fragments[i].active = false;
      }
    }
  }
}

// Draw ship fragments
void drawShipFragments() {
  for (int i = 0; i < MAX_INVADER_FRAGMENTS; i++) {
    if (ship_fragments[i].active) {
      display.fillRect((int)ship_fragments[i].x,
                      (int)ship_fragments[i].y, 2, 2, SSD1306_WHITE);
    }
  }
}

// Check if all ship fragments are inactive
bool allShipFragmentsInactive() {
  for (int i = 0; i < MAX_INVADER_FRAGMENTS; i++) {
    if (ship_fragments[i].active) return false;
  }
  return true;
}

// Find free ship fragment
ShipFragment* findFreeShipFragment() {
  for (int i = 0; i < MAX_INVADER_FRAGMENTS; i++) {
    if (!ship_fragments[i].active) return &ship_fragments[i];
  }
  return nullptr;
}

// Main ship animation update
void updateShipAnimation(struct tm* timeinfo) {
  unsigned long currentMillis = millis();

  if (currentMillis - last_ship_update < SHIP_ANIM_SPEED) return;
  last_ship_update = currentMillis;

  int seconds = timeinfo->tm_sec;
  int current_minute = timeinfo->tm_min;

  // Reset trigger
  if (current_minute != last_minute) {
    last_minute = current_minute;
    animation_triggered = false;
  }

  // Toggle sprite
  if (currentMillis - last_ship_sprite_toggle >= SHIP_SPRITE_TOGGLE_SPEED) {
    ship_anim_frame = 1 - ship_anim_frame;
    last_ship_sprite_toggle = currentMillis;
  }

  // Trigger at 55 seconds - transition from PATROL to SLIDING
  if (seconds >= 55 && !animation_triggered && ship_state == SHIP_PATROL) {
    animation_triggered = true;
    time_overridden = true;
    calculateTargetDigits(displayed_hour, displayed_min);

    if (num_targets > 0) {
      current_target_index = 0;
      ship_state = SHIP_SLIDING;
    }
  }

  updateShipFragments();
  updateShipLaser();

  switch (ship_state) {
    case SHIP_PATROL:
      handleShipPatrolState();
      break;
    case SHIP_SLIDING:
      handleShipSlidingState();
      break;
    case SHIP_SHOOTING:
      handleShipShootingState();
      break;
    case SHIP_EXPLODING_DIGIT:
      handleShipExplodingState();
      break;
    case SHIP_MOVING_NEXT:
      handleShipMovingNextState();
      break;
    case SHIP_RETURNING:
      handleShipReturningState();
      break;
  }
}

// Display clock with ship animation
void displayClockWithShip() {
  struct tm timeinfo;
  if(!getTimeWithTimeout(&timeinfo)) {
    display.setTextSize(1);
    display.setCursor(20, 28);
    if (!ntpSynced) {
      display.print("Syncing time...");
    } else {
      display.print("Time Error");
    }
    return;
  }

  // Update animation FIRST so time advances before drawing
  updateShipAnimation(&timeinfo);

  // Time management (same as invader)
  if (!time_overridden) {
    displayed_hour = timeinfo.tm_hour;
    displayed_min = timeinfo.tm_min;
  }

  // Reset time_overridden when real time catches up AND ship is in PATROL state
  if (time_overridden && timeinfo.tm_hour == displayed_hour &&
      timeinfo.tm_min == displayed_min && ship_state == SHIP_PATROL) {
    time_overridden = false;
  }

  // Date (at top, Y=4)
  display.setTextSize(1);
  char dateStr[12];
  switch (settings.dateFormat) {
    case 0: sprintf(dateStr, "%02d/%02d/%04d", timeinfo.tm_mday,
                    timeinfo.tm_mon + 1, timeinfo.tm_year + 1900); break;
    case 1: sprintf(dateStr, "%02d/%02d/%04d", timeinfo.tm_mon + 1,
                    timeinfo.tm_mday, timeinfo.tm_year + 1900); break;
    case 2: sprintf(dateStr, "%04d-%02d-%02d", timeinfo.tm_year + 1900,
                    timeinfo.tm_mon + 1, timeinfo.tm_mday); break;
  }
  display.setCursor((SCREEN_WIDTH - 60) / 2, 4);
  display.print(dateStr);

  // Time digits (Ship uses Y=16, same as invader)
  const int SHIP_TIME_Y = 16;
  display.setTextSize(3);
  char digits[5];
  digits[0] = '0' + (displayed_hour / 10);
  digits[1] = '0' + (displayed_hour % 10);
  digits[2] = ':';
  digits[3] = '0' + (displayed_min / 10);
  digits[4] = '0' + (displayed_min % 10);

  for (int i = 0; i < 5; i++) {
    display.setCursor(DIGIT_X[i], SHIP_TIME_Y);
    display.print(digits[i]);
  }

  // Render ship (ALWAYS visible - either patrolling or attacking)
  drawShip((int)ship_x, (int)ship_y, ship_anim_frame);

  // Render laser if active
  if (ship_laser.active) {
    drawShipLaser(&ship_laser);
  }

  // Render explosion fragments
  drawShipFragments();
}