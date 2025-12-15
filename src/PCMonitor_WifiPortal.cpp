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
#define MAX_METRICS 12
#define METRIC_NAME_LEN 11  // 10 chars + null terminator
#define METRIC_UNIT_LEN 5   // 4 chars + null terminator

struct Metric {
  uint8_t id;                     // 1-12
  char name[METRIC_NAME_LEN];     // "CPU%", "FAN1", etc. (from Python)
  char label[METRIC_NAME_LEN];    // Custom label (user editable)
  char unit[METRIC_UNIT_LEN];     // "%", "C", "RPM", etc.
  int value;                      // Sensor value
  bool visible;                   // User-configured visibility
  uint8_t displayOrder;           // Display position (0-11)
  uint8_t companionId;            // ID of companion metric (0 = none, 1-12 = metric ID)
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

  // Display layout mode
  int displayLayout;     // 0 = Progress Bars (legacy), 1 = Compact Grid

  // Clock positioning
  int clockPosition;     // 0 = Center, 1 = Left, 2 = Right

  // Dynamic metric visibility
  bool metricVisibility[MAX_METRICS];  // Index = metric ID - 1

  // Custom metric labels
  char metricLabels[MAX_METRICS][METRIC_NAME_LEN];  // Custom display names

  // Metric display order
  uint8_t metricOrder[MAX_METRICS];  // Display position for each metric

  // Companion metrics (pair metrics on same line)
  uint8_t metricCompanions[MAX_METRICS];  // Companion metric ID (0 = none)

  // Clock toggle
  bool showClock;        // Show/hide timestamp in metrics display
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
    settings.displayLayout = 0;  // Progress bars by default
    settings.clockPosition = 0;  // Center by default
    settings.showClock = true;
    // All metrics visible by default
    for (int i = 0; i < MAX_METRICS; i++) {
      settings.metricVisibility[i] = true;
      settings.metricLabels[i][0] = '\0';  // Empty = use Python name
      settings.metricOrder[i] = i;  // Default order
      settings.metricCompanions[i] = 0;  // No companion by default
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
    preferences.putInt("displayLayout", 0);
    preferences.putInt("clockPos", 0);  // Center
    preferences.putBool("showClock", true);

    // Initialize all metrics as visible
    bool defaultVis[MAX_METRICS];
    uint8_t defaultOrder[MAX_METRICS];
    uint8_t defaultCompanions[MAX_METRICS];
    for (int i = 0; i < MAX_METRICS; i++) {
      defaultVis[i] = true;
      defaultOrder[i] = i;
      defaultCompanions[i] = 0;  // No companion
    }
    preferences.putBytes("metricVis", defaultVis, MAX_METRICS);
    preferences.putBytes("metricOrd", defaultOrder, MAX_METRICS);
    preferences.putBytes("metricComp", defaultCompanions, MAX_METRICS);

    Serial.println("Default settings written to NVS");
  }

  settings.clockStyle = preferences.getInt("clockStyle", 0);  // Default: Mario
  settings.gmtOffset = preferences.getInt("gmtOffset", 1);    // Default: GMT+1
  settings.daylightSaving = preferences.getBool("dst", true); // Default: true
  settings.use24Hour = preferences.getBool("use24Hour", true); // Default: 24h
  settings.dateFormat = preferences.getInt("dateFormat", 0);  // Default: DD/MM/YYYY
  settings.displayLayout = preferences.getInt("displayLayout", 0);  // Default: Progress bars
  settings.clockPosition = preferences.getInt("clockPos", 0);  // Default: Center
  settings.showClock = preferences.getBool("showClock", true);

  // Load metric visibility array
  size_t visSize = preferences.getBytesLength("metricVis");
  if (visSize == MAX_METRICS) {
    preferences.getBytes("metricVis", settings.metricVisibility, MAX_METRICS);
    Serial.println("Loaded metric visibility from NVS");
  } else {
    // Default all visible if not found (fresh install or upgrade from v1.0)
    Serial.println("Initializing metricVisibility to all visible (upgrade/fresh install)");
    for (int i = 0; i < MAX_METRICS; i++) {
      settings.metricVisibility[i] = true;
    }
    // Save defaults to NVS
    preferences.putBytes("metricVis", settings.metricVisibility, MAX_METRICS);
    if (!preferences.isKey("displayLayout")) {
      preferences.putInt("displayLayout", settings.displayLayout);
    }
  }

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

  preferences.end();

  Serial.println("Settings loaded (v2.0)");
  Serial.print("Display Layout: ");
  Serial.println(settings.displayLayout == 0 ? "Progress Bars" : "Compact Grid");
}

void saveSettings() {
  preferences.begin("pcmonitor", false);  // Read-write
  preferences.putInt("clockStyle", settings.clockStyle);
  preferences.putInt("gmtOffset", settings.gmtOffset);
  preferences.putBool("dst", settings.daylightSaving);
  preferences.putBool("use24Hour", settings.use24Hour);
  preferences.putInt("dateFormat", settings.dateFormat);
  preferences.putInt("displayLayout", settings.displayLayout);
  preferences.putInt("clockPos", settings.clockPosition);
  preferences.putBool("showClock", settings.showClock);

  // Save metric visibility array
  preferences.putBytes("metricVis", settings.metricVisibility, MAX_METRICS);

  // Save metric display order
  preferences.putBytes("metricOrd", settings.metricOrder, MAX_METRICS);

  // Save metric companions
  preferences.putBytes("metricComp", settings.metricCompanions, MAX_METRICS);

  // Save custom metric labels
  for (int i = 0; i < MAX_METRICS; i++) {
    String key = "label" + String(i);
    if (settings.metricLabels[i][0] != '\0') {
      preferences.putString(key.c_str(), settings.metricLabels[i]);
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
            ",\"visible\":" + String(m.visible ? "true" : "false") +
            ",\"displayOrder\":" + String(m.displayOrder) +
            ",\"companionId\":" + String(m.companionId) + "}";
  }

  json += "]}";

  server.send(200, "application/json", json);
}

void handleRoot() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>PC Monitor Settings</title>
  <style>
    body { font-family: Arial, sans-serif; margin: 20px; background: #1a1a2e; color: #eee; }
    .container { max-width: 400px; margin: 0 auto; }
    h1 { color: #00d4ff; text-align: center; }
    .card { background: #16213e; padding: 20px; border-radius: 10px; margin-bottom: 20px; }
    label { display: block; margin: 15px 0 5px; color: #00d4ff; }
    select, input { width: 100%; padding: 10px; border: none; border-radius: 5px; background: #0f3460; color: #fff; font-size: 16px; }
    select:focus, input:focus { outline: 2px solid #00d4ff; }
    button { width: 100%; padding: 15px; margin-top: 20px; border: none; border-radius: 5px; font-size: 18px; cursor: pointer; }
    .save-btn { background: #00d4ff; color: #1a1a2e; }
    .save-btn:hover { background: #00a8cc; }
    .reset-btn { background: #e94560; color: #fff; }
    .reset-btn:hover { background: #c73e54; }
    .info { text-align: center; color: #888; font-size: 12px; margin-top: 20px; }
    .status { background: #0f3460; padding: 10px; border-radius: 5px; text-align: center; margin-bottom: 20px; }
  </style>
</head>
<body>
  <div class="container">
    <h1>&#128421; PC Monitor</h1>
    <div class="status">
      <strong>IP:</strong> )rawliteral" + WiFi.localIP().toString() + R"rawliteral( | <strong>UDP Port:</strong> 4210
    </div>
    <form action="/save" method="POST">
      <div class="card">
        <h3>&#128348; Clock Settings</h3>
        
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
      
      <div class="card">
        <h3>&#127760; Timezone</h3>
        
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

      <div class="card">
        <h3 style="text-align: left;">&#128202; Display Layout</h3>
        <label for="displayLayout">Metrics Display Style</label>
        <select name="displayLayout" id="displayLayout">
          <option value="0" )rawliteral" + String(settings.displayLayout == 0 ? "selected" : "") + R"rawliteral(>Progress Bars (4-5 metrics)</option>
          <option value="1" )rawliteral" + String(settings.displayLayout == 1 ? "selected" : "") + R"rawliteral(>Compact Grid (10-12 metrics)</option>
        </select>
        <p style="color: #888; font-size: 12px; margin-top: 10px;">
          Compact mode shows more metrics without progress bars
        </p>

        <label for="clockPosition" style="margin-top: 15px; display: block;">Clock Position (Compact Grid Only)</label>
        <select name="clockPosition" id="clockPosition">
          <option value="0" )rawliteral" + String(settings.clockPosition == 0 ? "selected" : "") + R"rawliteral(>Center (Top)</option>
          <option value="1" )rawliteral" + String(settings.clockPosition == 1 ? "selected" : "") + R"rawliteral(>Left Column</option>
          <option value="2" )rawliteral" + String(settings.clockPosition == 2 ? "selected" : "") + R"rawliteral(>Right Column</option>
        </select>
        <p style="color: #888; font-size: 12px; margin-top: 10px;">
          Position clock to optimize space for metrics
        </p>
      </div>

      <div class="card">
        <h3 style="text-align: left;">&#128195; Visible Metrics</h3>
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
          Select up to 12 in pc_stats_monitor_v2.py
        </p>

        <hr style="margin: 20px 0; border: none; border-top: 1px solid #333;">

        <div style="display: flex; align-items: center;">
          <input type="checkbox" name="showClock" id="showClock" value="1" )rawliteral" + String(settings.showClock ? "checked" : "") + R"rawliteral( style="width: 20px; margin: 0;">
          <label for="showClock" style="margin: 0 0 0 10px; text-align: left; color: #00d4ff;">Show Clock/Time in metrics display</label>
        </div>

        <script>
          let metricsData = [];

          function renderMetrics() {
            const container = document.getElementById('metricsContainer');
            container.innerHTML = '';

            // Sort metrics by displayOrder
            const sortedMetrics = [...metricsData].sort((a, b) => a.displayOrder - b.displayOrder);

            sortedMetrics.forEach((metric, index) => {
              const div = document.createElement('div');
              div.style.cssText = 'background: #0f172a; padding: 12px; margin-bottom: 8px; border-radius: 6px; border: 1px solid #334155;';

              const checked = metric.visible ? 'checked' : '';

              // Build companion dropdown options
              let companionOptions = '<option value="0">None</option>';
              metricsData.forEach(m => {
                if (m.id !== metric.id) {
                  const selected = (metric.companionId === m.id) ? 'selected' : '';
                  companionOptions += `<option value="${m.id}" ${selected}>${m.name} (${m.unit})</option>`;
                }
              });

              div.innerHTML = `
                <div style="display: flex; align-items: center; gap: 10px; margin-bottom: 8px;">
                  <input type="checkbox" name="metric_${metric.id}" id="metric_${metric.id}"
                         value="1" ${checked} style="width: 20px; height: 20px; margin: 0;">
                  <label for="metric_${metric.id}" style="color: #00d4ff; font-weight: bold; flex: 1; margin: 0;">
                    ${metric.name} (${metric.unit})
                  </label>
                  <div style="display: flex; gap: 4px;">
                    <button type="button" onclick="moveUp(${metric.id})"
                            style="background: #1e293b; color: #00d4ff; border: 1px solid #334155;
                                   padding: 4px 10px; border-radius: 4px; cursor: pointer; font-size: 16px;"
                            ${index === 0 ? 'disabled' : ''}>&#9650;</button>
                    <button type="button" onclick="moveDown(${metric.id})"
                            style="background: #1e293b; color: #00d4ff; border: 1px solid #334155;
                                   padding: 4px 10px; border-radius: 4px; cursor: pointer; font-size: 16px;"
                            ${index === sortedMetrics.length - 1 ? 'disabled' : ''}>&#9660;</button>
                  </div>
                </div>
                <div style="display: flex; align-items: center; gap: 10px; margin-bottom: 6px;">
                  <label style="color: #888; font-size: 12px; min-width: 80px; margin: 0;">Custom Label:</label>
                  <input type="text" name="label_${metric.id}"
                         value="${metric.label}" maxlength="10" placeholder="${metric.name}"
                         style="flex: 1; padding: 6px; background: #16213e; border: 1px solid #334155;
                                color: #eee; border-radius: 4px; font-size: 13px;">
                  <input type="hidden" name="order_${metric.id}" value="${metric.displayOrder}">
                </div>
                <div style="display: flex; align-items: center; gap: 10px;">
                  <label style="color: #888; font-size: 12px; min-width: 80px; margin: 0;">Pair with:</label>
                  <select name="companion_${metric.id}"
                          style="flex: 1; padding: 6px; background: #16213e; border: 1px solid #334155;
                                 color: #eee; border-radius: 4px; font-size: 13px;">
                    ${companionOptions}
                  </select>
                </div>
              `;

              container.appendChild(div);
            });
          }

          function moveUp(metricId) {
            // Sort metrics by current displayOrder
            const sortedMetrics = [...metricsData].sort((a, b) => a.displayOrder - b.displayOrder);

            // Find metric's position in sorted array
            const currentIndex = sortedMetrics.findIndex(m => m.id === metricId);
            if (currentIndex <= 0) return;  // Already at top

            // Swap with previous metric in sorted array
            const temp = sortedMetrics[currentIndex].displayOrder;
            sortedMetrics[currentIndex].displayOrder = sortedMetrics[currentIndex - 1].displayOrder;
            sortedMetrics[currentIndex - 1].displayOrder = temp;

            renderMetrics();
          }

          function moveDown(metricId) {
            // Sort metrics by current displayOrder
            const sortedMetrics = [...metricsData].sort((a, b) => a.displayOrder - b.displayOrder);

            // Find metric's position in sorted array
            const currentIndex = sortedMetrics.findIndex(m => m.id === metricId);
            if (currentIndex < 0 || currentIndex >= sortedMetrics.length - 1) return;  // Already at bottom

            // Swap with next metric in sorted array
            const temp = sortedMetrics[currentIndex].displayOrder;
            sortedMetrics[currentIndex].displayOrder = sortedMetrics[currentIndex + 1].displayOrder;
            sortedMetrics[currentIndex + 1].displayOrder = temp;

            renderMetrics();
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

      <button type="submit" class="save-btn">&#128190; Save Settings</button>
    </form>
    
    <form action="/reset" method="GET" onsubmit="return confirm('Reset WiFi settings? Device will restart in AP mode.');">
      <button type="submit" class="reset-btn">&#128260; Reset WiFi Settings</button>
    </form>
    
    <div class="info">
      PC Stats Monitor v2.0<br>
      Configure Python script with IP shown above
    </div>
  </div>
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

  // Save display layout
  if (server.hasArg("displayLayout")) {
    settings.displayLayout = server.arg("displayLayout").toInt();
  }

  // Save clock position
  if (server.hasArg("clockPosition")) {
    settings.clockPosition = server.arg("clockPosition").toInt();
  }

  // Save clock visibility checkbox
  settings.showClock = server.hasArg("showClock");

  // Save dynamic metric visibility (unchecked = not present in POST data)
  for (int i = 0; i < MAX_METRICS; i++) {
    String argName = "metric_" + String(i + 1);
    settings.metricVisibility[i] = server.hasArg(argName);
  }

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

  // Apply visibility, labels, order, and companions to current metrics in memory
  for (int i = 0; i < metricData.count; i++) {
    Metric& m = metricData.metrics[i];
    if (m.id > 0 && m.id <= MAX_METRICS) {
      m.visible = settings.metricVisibility[m.id - 1];

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

  // Load visibility from settings (ID-based indexing)
  if (id > 0 && id <= MAX_METRICS) {
    m.visible = settings.metricVisibility[id - 1];

    // Apply custom label if set (don't trim - user may want trailing spaces for alignment)
    if (settings.metricLabels[id - 1][0] != '\0') {
      strncpy(m.label, settings.metricLabels[id - 1], METRIC_NAME_LEN - 1);
      m.label[METRIC_NAME_LEN - 1] = '\0';
      // Convert '^' to spaces for custom alignment
      convertCaretToSpaces(m.label);
    } else {
      strncpy(m.label, m.name, METRIC_NAME_LEN - 1);
      m.label[METRIC_NAME_LEN - 1] = '\0';
    }

    m.displayOrder = settings.metricOrder[id - 1];

    // Load companion metric
    m.companionId = settings.metricCompanions[id - 1];
  } else {
    m.visible = true;
    strncpy(m.label, m.name, METRIC_NAME_LEN - 1);
    m.label[METRIC_NAME_LEN - 1] = '\0';
    m.displayOrder = metricData.count;
    m.companionId = 0;  // No companion for new metrics
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
      m.visible = settings.metricVisibility[m.id - 1];

      // Apply custom label if set (empty = use Python name)
      // Don't trim - user may want trailing spaces for alignment
      if (settings.metricLabels[m.id - 1][0] != '\0') {
        strncpy(m.label, settings.metricLabels[m.id - 1], METRIC_NAME_LEN - 1);
        m.label[METRIC_NAME_LEN - 1] = '\0';
        // Convert '^' to spaces for custom alignment
        convertCaretToSpaces(m.label);
      } else {
        // No custom label, copy name to label
        strncpy(m.label, m.name, METRIC_NAME_LEN - 1);
        m.label[METRIC_NAME_LEN - 1] = '\0';
      }

      // Load display order
      m.displayOrder = settings.metricOrder[m.id - 1];

      // Load companion metric
      m.companionId = settings.metricCompanions[m.id - 1];
    } else {
      m.visible = true;  // Default visible for new metrics
      strncpy(m.label, m.name, METRIC_NAME_LEN - 1);
      m.label[METRIC_NAME_LEN - 1] = '\0';
      m.displayOrder = metricData.count;
      m.companionId = 0;  // No companion for new metrics
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
    if (metricData.metrics[i].visible) visibleCount++;
  }
  Serial.print(visibleCount);
  Serial.println(" visible");
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

  // Build sorted index array
  for (int order = 0; order < MAX_METRICS; order++) {
    for (int i = 0; i < metricData.count; i++) {
      if (metricData.metrics[i].displayOrder == order && metricData.metrics[i].visible) {
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

    // Process label: strip trailing '%' and move trailing spaces to after colon
    char displayLabel[METRIC_NAME_LEN];
    strncpy(displayLabel, m.label, METRIC_NAME_LEN - 1);
    displayLabel[METRIC_NAME_LEN - 1] = '\0';

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

// Compact grid layout (new, 10-12 metrics)
void displayStatsCompactGrid() {
  display.setTextSize(1);

  int COL1_X = 0;
  int COL2_X = 64;
  const int ROW_HEIGHT = 10;  // Compact spacing

  // Create sorted array of metric indices by displayOrder
  int sortedIndices[MAX_METRICS];
  int sortedCount = 0;

  // Build sorted index array (only visible metrics)
  for (int order = 0; order < MAX_METRICS; order++) {
    for (int i = 0; i < metricData.count; i++) {
      if (metricData.metrics[i].displayOrder == order && metricData.metrics[i].visible) {
        sortedIndices[sortedCount++] = i;
        break;
      }
    }
  }

  int startY = 2;
  int row = 0;
  int col = 0;

  // Clock positioning: 0=Center, 1=Left, 2=Right
  if (settings.showClock) {
    if (settings.clockPosition == 0) {
      // Center - Clock at top center, metrics below in 2 columns
      display.setCursor(48, startY);
      display.print(metricData.timestamp);
      startY += 12;
    } else if (settings.clockPosition == 1) {
      // Left - Clock in left column, metrics start in right column
      display.setCursor(0, startY);
      display.print(metricData.timestamp);
      // Start rendering metrics from right column (col 1)
      col = 1;
    } else if (settings.clockPosition == 2) {
      // Right - Clock in right column, metrics start in left column
      display.setCursor(64, startY);
      display.print(metricData.timestamp);
      // Metrics will flow: left col, then right col (but skip first right position)
      // We'll handle this by rendering normally
    }
  }

  // Render metrics in 2-column grid (sorted by displayOrder)
  for (int idx = 0; idx < sortedCount; idx++) {
    Metric& m = metricData.metrics[sortedIndices[idx]];

    // Skip first right column position if clock is on right and this is the first metric
    if (settings.showClock && settings.clockPosition == 2 && idx == 0) {
      col = 0;  // Start from left
      // Clock occupies first right position
    }

    int x = (col == 0) ? COL1_X : COL2_X;
    int y = startY + (row * ROW_HEIGHT);

    // Check for overflow
    if (y + 8 > 64) break;

    display.setCursor(x, y);

    // Process label: strip trailing '%' and move trailing spaces to after colon
    char displayLabel[METRIC_NAME_LEN];
    strncpy(displayLabel, m.label, METRIC_NAME_LEN - 1);
    displayLabel[METRIC_NAME_LEN - 1] = '\0';

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
    char text[40];  // Increased size to accommodate companion metrics
    char spaces[11] = "";  // Max 10 spaces
    for (int i = 0; i < trailingSpaces && i < 10; i++) {
      spaces[i] = ' ';
      spaces[i + 1] = '\0';
    }

    if (strcmp(m.unit, "RPM") == 0 && m.value >= 1000) {
      // RPM with K suffix: "FAN1: 1.2K"
      snprintf(text, 40, "%s:%s%.1fK", displayLabel, spaces, m.value / 1000.0);
    } else if (strlen(displayLabel) + trailingSpaces + strlen(m.unit) + 5 > 10) {
      // Truncate label if too long
      char shortLabel[6];
      strncpy(shortLabel, displayLabel, 5);
      shortLabel[5] = '\0';
      snprintf(text, 40, "%s:%s%d%s", shortLabel, spaces, m.value, m.unit);
    } else {
      // Normal: "CPU: 45%"
      snprintf(text, 40, "%s:%s%d%s", displayLabel, spaces, m.value, m.unit);
    }

    // Check for companion metric (append to same line)
    if (m.companionId > 0) {
      // Find companion metric by ID
      for (int c = 0; c < metricData.count; c++) {
        if (metricData.metrics[c].id == m.companionId) {
          Metric& companion = metricData.metrics[c];
          char companionText[15];
          snprintf(companionText, 15, " %d%s", companion.value, companion.unit);
          strncat(text, companionText, 40 - strlen(text) - 1);
          break;
        }
      }
    }

    display.print(text);

    // Move to next cell
    col++;
    if (col >= 2) {
      col = 0;
      row++;
    }
  }

  // No metrics edge case
  if (sortedCount == 0) {
    display.setCursor(20, 28);
    display.print("No metrics");
  }
}

// Main display function with layout switching
void displayStats() {
  if (settings.displayLayout == 0) {
    displayStatsProgressBars();  // Legacy layout
  } else {
    displayStatsCompactGrid();   // Compact 2-column grid
  }
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