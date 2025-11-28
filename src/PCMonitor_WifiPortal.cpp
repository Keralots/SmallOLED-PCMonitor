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
#include <WiFiUDP.h>
#include <WebServer.h>
#include <Preferences.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ArduinoJson.h>
#include <time.h>

// ========== WiFi Portal Configuration ==========
const char* AP_NAME = "PCMonitor-Setup";
const char* AP_PASSWORD = "monitor123";

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

// ========== Settings (loaded from flash) ==========
struct Settings {
  int clockStyle;        // 0 = Mario, 1 = Standard, 2 = Large
  int gmtOffset;         // GMT offset in hours (-12 to +14)
  bool daylightSaving;   // Daylight saving time
  bool use24Hour;        // 24-hour format
  int dateFormat;        // 0 = DD/MM/YYYY, 1 = MM/DD/YYYY, 2 = YYYY-MM-DD
  char fanLabel[16];     // Custom label for fan/pump (e.g., "PUMP", "FAN", "COOLER")
  char cpuLabel[16];     // Custom label for CPU
  char ramLabel[16];     // Custom label for RAM
  char gpuLabel[16];     // Custom label for GPU
  char diskLabel[16];    // Custom label for Disk
};

Settings settings;

// ========== PC Stats Structure ==========
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

PCStats stats;
unsigned long lastReceived = 0;
const unsigned long TIMEOUT = 6000;

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
const int TIME_Y = 26;

// ========== WiFiManager ==========
WiFiManager wifiManager;

// Forward declarations
void loadSettings();
void saveSettings();
void setupWebServer();
void handleRoot();
void handleSave();
void handleReset();
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
void parseStats(const char* json);
void displayStats();

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n========================================");
  Serial.println("PC Stats Monitor - Web Config Version");
  Serial.println("========================================");
  
  // Load settings from flash
  loadSettings();
  
  Wire.begin(SDA_PIN, SCL_PIN);
  
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("ERROR: Display initialization failed!");
    while(1);
  }
  
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(10, 20);
  display.println("PC Monitor");
  display.setCursor(10, 35);
  display.println("Starting...");
  display.display();
  
  wifiManager.setConfigPortalTimeout(180);
  wifiManager.setAPCallback(configModeCallback);
  wifiManager.setSaveConfigCallback(saveConfigCallback);
  
  Serial.println("Attempting WiFi connection...");
  
  if (!wifiManager.autoConnect(AP_NAME, AP_PASSWORD)) {
    Serial.println("Failed to connect and hit timeout");
    display.clearDisplay();
    display.setCursor(10, 20);
    display.println("WiFi Timeout!");
    display.setCursor(10, 35);
    display.println("Restarting...");
    display.display();
    delay(3000);
    ESP.restart();
  }
  
  Serial.println("WiFi Connected!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
  
  // Apply timezone and start NTP
  applyTimezone();
  Serial.println("NTP time synchronized");
  
  // Start UDP listener
  udp.begin(UDP_PORT);
  Serial.print("UDP listening on port ");
  Serial.println(UDP_PORT);
  
  // Setup web server for configuration
  setupWebServer();
  Serial.println("Web server started on port 80");
  
  // Show IP address for 5 seconds
  displayConnected();
  delay(5000);
  
  Serial.println("Setup complete!");
  Serial.println("========================================");
}

void configModeCallback(WiFiManager *myWiFiManager) {
  Serial.println("Entered config mode");
  displaySetupInstructions();
}

void saveConfigCallback() {
  Serial.println("Config saved");
  displayConnecting();
}

void loadSettings() {
  preferences.begin("pcmonitor", true);  // Read-only
  settings.clockStyle = preferences.getInt("clockStyle", 0);  // Default: Mario
  settings.gmtOffset = preferences.getInt("gmtOffset", 1);    // Default: GMT+1
  settings.daylightSaving = preferences.getBool("dst", true); // Default: true
  settings.use24Hour = preferences.getBool("use24Hour", true); // Default: 24h
  settings.dateFormat = preferences.getInt("dateFormat", 0);  // Default: DD/MM/YYYY

  // Load custom labels with defaults
  String fanLbl = preferences.getString("fanLabel", "PUMP");
  String cpuLbl = preferences.getString("cpuLabel", "CPU");
  String ramLbl = preferences.getString("ramLabel", "RAM");
  String gpuLbl = preferences.getString("gpuLabel", "GPU");
  String diskLbl = preferences.getString("diskLabel", "DISK");

  strncpy(settings.fanLabel, fanLbl.c_str(), 15);
  strncpy(settings.cpuLabel, cpuLbl.c_str(), 15);
  strncpy(settings.ramLabel, ramLbl.c_str(), 15);
  strncpy(settings.gpuLabel, gpuLbl.c_str(), 15);
  strncpy(settings.diskLabel, diskLbl.c_str(), 15);
  settings.fanLabel[15] = '\0';
  settings.cpuLabel[15] = '\0';
  settings.ramLabel[15] = '\0';
  settings.gpuLabel[15] = '\0';
  settings.diskLabel[15] = '\0';

  preferences.end();

  Serial.println("Settings loaded:");
  Serial.print("  Clock Style: "); Serial.println(settings.clockStyle);
  Serial.print("  GMT Offset: "); Serial.println(settings.gmtOffset);
  Serial.print("  DST: "); Serial.println(settings.daylightSaving ? "Yes" : "No");
  Serial.print("  24-Hour: "); Serial.println(settings.use24Hour ? "Yes" : "No");
  Serial.print("  Date Format: "); Serial.println(settings.dateFormat);
  Serial.print("  Fan Label: "); Serial.println(settings.fanLabel);
  Serial.print("  CPU Label: "); Serial.println(settings.cpuLabel);
  Serial.print("  RAM Label: "); Serial.println(settings.ramLabel);
  Serial.print("  GPU Label: "); Serial.println(settings.gpuLabel);
  Serial.print("  Disk Label: "); Serial.println(settings.diskLabel);
}

void saveSettings() {
  preferences.begin("pcmonitor", false);  // Read-write
  preferences.putInt("clockStyle", settings.clockStyle);
  preferences.putInt("gmtOffset", settings.gmtOffset);
  preferences.putBool("dst", settings.daylightSaving);
  preferences.putBool("use24Hour", settings.use24Hour);
  preferences.putInt("dateFormat", settings.dateFormat);
  preferences.putString("fanLabel", settings.fanLabel);
  preferences.putString("cpuLabel", settings.cpuLabel);
  preferences.putString("ramLabel", settings.ramLabel);
  preferences.putString("gpuLabel", settings.gpuLabel);
  preferences.putString("diskLabel", settings.diskLabel);
  preferences.end();

  Serial.println("Settings saved!");
}

void applyTimezone() {
  long gmtOffset_sec = settings.gmtOffset * 3600;
  int dstOffset_sec = settings.daylightSaving ? 3600 : 0;
  configTime(gmtOffset_sec, dstOffset_sec, ntpServer);
}

void setupWebServer() {
  server.on("/", handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/reset", handleReset);
  server.begin();
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
        <h3>&#128195; Display Labels</h3>
        <p style="color: #888; font-size: 14px; margin-top: 0;">Customize labels shown on OLED when PC stats are displayed</p>

        <label for="fanLabel">Fan/Pump Label</label>
        <input type="text" name="fanLabel" id="fanLabel" value=")rawliteral" + String(settings.fanLabel) + R"rawliteral(" maxlength="15" placeholder="PUMP, FAN, COOLER">

        <label for="cpuLabel">CPU Label</label>
        <input type="text" name="cpuLabel" id="cpuLabel" value=")rawliteral" + String(settings.cpuLabel) + R"rawliteral(" maxlength="15" placeholder="CPU, PROCESSOR">

        <label for="ramLabel">RAM Label</label>
        <input type="text" name="ramLabel" id="ramLabel" value=")rawliteral" + String(settings.ramLabel) + R"rawliteral(" maxlength="15" placeholder="RAM, MEMORY">

        <label for="gpuLabel">GPU Label</label>
        <input type="text" name="gpuLabel" id="gpuLabel" value=")rawliteral" + String(settings.gpuLabel) + R"rawliteral(" maxlength="15" placeholder="GPU, GRAPHICS">

        <label for="diskLabel">Disk Label</label>
        <input type="text" name="diskLabel" id="diskLabel" value=")rawliteral" + String(settings.diskLabel) + R"rawliteral(" maxlength="15" placeholder="DISK, STORAGE, SSD">
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

  // Save custom labels
  if (server.hasArg("fanLabel")) {
    String fanLbl = server.arg("fanLabel");
    if (fanLbl.length() > 0) {
      strncpy(settings.fanLabel, fanLbl.c_str(), 15);
      settings.fanLabel[15] = '\0';
    }
  }
  if (server.hasArg("cpuLabel")) {
    String cpuLbl = server.arg("cpuLabel");
    if (cpuLbl.length() > 0) {
      strncpy(settings.cpuLabel, cpuLbl.c_str(), 15);
      settings.cpuLabel[15] = '\0';
    }
  }
  if (server.hasArg("ramLabel")) {
    String ramLbl = server.arg("ramLabel");
    if (ramLbl.length() > 0) {
      strncpy(settings.ramLabel, ramLbl.c_str(), 15);
      settings.ramLabel[15] = '\0';
    }
  }
  if (server.hasArg("gpuLabel")) {
    String gpuLbl = server.arg("gpuLabel");
    if (gpuLbl.length() > 0) {
      strncpy(settings.gpuLabel, gpuLbl.c_str(), 15);
      settings.gpuLabel[15] = '\0';
    }
  }
  if (server.hasArg("diskLabel")) {
    String diskLbl = server.arg("diskLabel");
    if (diskLbl.length() > 0) {
      strncpy(settings.diskLabel, diskLbl.c_str(), 15);
      settings.diskLabel[15] = '\0';
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
  // Handle web server requests
  server.handleClient();
  
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi disconnected, restarting...");
    ESP.restart();
  }
  
  int packetSize = udp.parsePacket();
  if (packetSize) {
    char buffer[512];
    int len = udp.read(buffer, sizeof(buffer) - 1);
    if (len > 0) {
      buffer[len] = '\0';
      parseStats(buffer);
      lastReceived = millis();
    }
  }
  
  stats.online = (millis() - lastReceived) < TIMEOUT;
  
  display.clearDisplay();
  
  if (stats.online) {
    displayStats();
  } else {
    if (settings.clockStyle == 0) {
      displayClockWithMario();
    } else if (settings.clockStyle == 1) {
      displayStandardClock();
    } else {
      displayLargeClock();
    }
  }
  
  display.display();
  delay(30);
}

void parseStats(const char* json) {
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, json);
  
  if (error) {
    Serial.print("JSON parse error: ");
    Serial.println(error.c_str());
    return;
  }
  
  stats.cpu_percent = doc["cpu_percent"] | 0.0;
  stats.ram_percent = doc["ram_percent"] | 0.0;
  stats.ram_used_gb = doc["ram_used_gb"] | 0.0;
  stats.ram_total_gb = doc["ram_total_gb"] | 0.0;
  stats.disk_percent = doc["disk_percent"] | 0.0;
  stats.cpu_temp = doc["cpu_temp"] | 0;
  stats.gpu_temp = doc["gpu_temp"] | 0;
  stats.fan_speed = doc["fan_speed"] | 0;
  
  const char* ts = doc["timestamp"];
  if (ts) {
    strncpy(stats.timestamp, ts, 5);
    stats.timestamp[5] = '\0';
  }
}

void displayStats() {
  display.setTextSize(1);

  // Fan/Pump line
  display.setCursor(0, 0);
  display.print(settings.fanLabel);
  display.print(":");
  display.print(stats.fan_speed);
  display.print("RPM");

  display.setCursor(85, 0);
  display.print(stats.timestamp);

  // RAM line
  display.setCursor(0, 14);
  display.print(settings.ramLabel);
  display.print(": ");
  display.print((int)stats.ram_percent);
  display.print("%");

  int ram_bar = (int)(stats.ram_percent * 0.6);
  display.drawRect(70, 14, 58, 8, SSD1306_WHITE);
  if (ram_bar > 0) {
    display.fillRect(71, 15, ram_bar, 6, SSD1306_WHITE);
  }

  // CPU line
  display.setCursor(0, 28);
  display.print(settings.cpuLabel);
  display.print(": ");
  display.print((int)stats.cpu_percent);
  display.print("% ");
  display.print(stats.cpu_temp);
  display.print("C");

  int cpu_bar = (int)(stats.cpu_percent * 0.6);
  display.drawRect(70, 28, 58, 8, SSD1306_WHITE);
  if (cpu_bar > 0) {
    display.fillRect(71, 29, cpu_bar, 6, SSD1306_WHITE);
  }

  // GPU line
  display.setCursor(0, 42);
  display.print(settings.gpuLabel);
  display.print(": ");
  display.print(stats.gpu_temp);
  display.print("C");

  int gpu_bar = map(stats.gpu_temp, 0, 100, 0, 56);
  gpu_bar = constrain(gpu_bar, 0, 56);
  display.drawRect(70, 42, 58, 8, SSD1306_WHITE);
  if (gpu_bar > 0) {
    display.fillRect(71, 43, gpu_bar, 6, SSD1306_WHITE);
  }

  // Disk line
  display.setCursor(0, 56);
  display.print(settings.diskLabel);
  display.print(":");
  display.print((int)stats.disk_percent);
  display.print("%");

  int disk_bar = (int)(stats.disk_percent * 0.56);
  display.drawRect(70, 56, 58, 8, SSD1306_WHITE);
  if (disk_bar > 0) {
    display.fillRect(71, 57, disk_bar, 6, SSD1306_WHITE);
  }
}

// ========== Standard Clock Display ==========
void displayStandardClock() {
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)) {
    display.setTextSize(1);
    display.setCursor(20, 28);
    display.print("Time Error");
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
  if(!getLocalTime(&timeinfo)) {
    display.setTextSize(1);
    display.setCursor(20, 28);
    display.print("Time Error");
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
  if(!getLocalTime(&timeinfo)) {
    display.setTextSize(1);
    display.setCursor(20, 28);
    display.print("Time Error");
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
  if (curr_digits[0] != next_digits[0]) {
    target_x_positions[num_targets] = DIGIT_X[0] + 9;
    target_digit_index[num_targets] = 0;
    target_digit_values[num_targets] = next_digits[0];
    num_targets++;
  }
  if (curr_digits[1] != next_digits[1]) {
    target_x_positions[num_targets] = DIGIT_X[1] + 9;
    target_digit_index[num_targets] = 1;
    target_digit_values[num_targets] = next_digits[1];
    num_targets++;
  }
  if (curr_digits[2] != next_digits[2]) {
    target_x_positions[num_targets] = DIGIT_X[3] + 9;
    target_digit_index[num_targets] = 3;
    target_digit_values[num_targets] = next_digits[2];
    num_targets++;
  }
  if (curr_digits[3] != next_digits[3]) {
    target_x_positions[num_targets] = DIGIT_X[4] + 9;
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