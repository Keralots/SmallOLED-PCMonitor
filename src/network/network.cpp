/*
 * SmallOLED-PCMonitor - Network Module
 *
 * WiFi connection management, UDP packet handling, and NTP sync.
 */

#include "network.h"
#include "../display/display.h"
#include "../utils/utils.h"
#include <Preferences.h>
#include <esp_wifi.h>

#if TOUCH_BUTTON_ENABLED
extern bool manualClockMode;  // Defined in main.cpp
#endif

// Global network objects
WiFiUDP udp;
WiFiManager wifiManager;
extern Preferences preferences;

// ========== WiFi Callbacks ==========
void configModeCallback(WiFiManager *myWiFiManager) {
  Serial.println("Config mode entered");
  Serial.println(WiFi.softAPIP());

  if (displayAvailable) {
    displaySetupInstructions();
  }
}

void saveConfigCallback() {
  if (displayAvailable) {
    displayConnecting();
  }
}

// ========== Static IP Application ==========
void applyStaticIP() {
  if (settings.useStaticIP) {
    IPAddress local_IP, gateway_IP, subnet_IP, dns1_IP;

    if (local_IP.fromString(settings.staticIP) &&
        gateway_IP.fromString(settings.gateway) &&
        subnet_IP.fromString(settings.subnet) &&
        dns1_IP.fromString(settings.dns1)) {

      Serial.println("Configuring Static IP...");
      Serial.print("IP: "); Serial.println(local_IP);
      Serial.print("Gateway: "); Serial.println(gateway_IP);
      Serial.print("Subnet: "); Serial.println(subnet_IP);
      Serial.print("DNS1: "); Serial.println(dns1_IP);

      wifiManager.setSTAStaticIPConfig(local_IP, gateway_IP, subnet_IP, dns1_IP);
    } else {
      Serial.println("Invalid static IP configuration, using DHCP");
    }
  }
}

// ========== Manual WiFi Connection ==========
bool connectManualWiFi(const char* ssid, const char* password) {
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

  // Apply static IP configuration if enabled
  if (settings.useStaticIP) {
    IPAddress local_IP, gateway_IP, subnet_IP, dns1_IP, dns2_IP;

    if (local_IP.fromString(settings.staticIP) &&
        gateway_IP.fromString(settings.gateway) &&
        subnet_IP.fromString(settings.subnet) &&
        dns1_IP.fromString(settings.dns1)) {

      dns2_IP.fromString(settings.dns2);

      Serial.println("Configuring Static IP for manual WiFi...");
      if (!WiFi.config(local_IP, gateway_IP, subnet_IP, dns1_IP, dns2_IP)) {
        Serial.println("Static IP configuration failed!");
      }
    } else {
      Serial.println("Invalid static IP configuration, using DHCP");
    }
  }

  WiFi.begin(ssid, password);

  int attempts = 0;
  int maxAttempts = 30;

  while (WiFi.status() != WL_CONNECTED && attempts < maxAttempts) {
    delay(1000);
    attempts++;

    if (displayAvailable && attempts % 5 == 0) {
      display.clearDisplay();
      display.setTextSize(1);
      display.setCursor(10, 20);
      display.println("Connecting...");
      display.setCursor(10, 35);
      display.print("Attempt: ");
      display.print(String(attempts).c_str());
      display.print("/");
      display.println(String(maxAttempts).c_str());
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

// ========== Check Stored WiFi Credentials ==========
bool hasStoredCredentials() {
  wifi_config_t conf;
  if (esp_wifi_get_config(WIFI_IF_STA, &conf) == ESP_OK) {
    return strlen((char*)conf.sta.ssid) > 0;
  }
  return false;
}

String getStoredSSID() {
  wifi_config_t conf;
  if (esp_wifi_get_config(WIFI_IF_STA, &conf) == ESP_OK) {
    return String((char*)conf.sta.ssid);
  }
  return "";
}

// ========== Network Initialization ==========
void initNetwork() {
  // Apply static IP if configured
  applyStaticIP();

  // Configure WiFiManager
  wifiManager.setConnectTimeout(30);
  wifiManager.setConfigPortalTimeout(180);
  wifiManager.setAPCallback(configModeCallback);
  wifiManager.setSaveConfigCallback(saveConfigCallback);
  wifiManager.setDebugOutput(false);

  WiFi.persistent(true);
  WiFi.setAutoReconnect(true);
  WiFi.mode(WIFI_STA);

  delay(100);

  if (hasStoredCredentials()) {
    String savedSSID = getStoredSSID();
    Serial.print("Stored WiFi credentials found for: ");
    Serial.println(savedSSID);

    const int maxRounds = 3;
    const int attemptsPerRound = 40;
    const int totalAttempts = maxRounds * attemptsPerRound;

    for (int round = 0; round < maxRounds; round++) {
      Serial.printf("Connection round %d/%d\n", round + 1, maxRounds);

      if (round > 0) {
        WiFi.disconnect(false);
        delay(1000);
      }

      WiFi.begin();

      int attempts = 0;
      while (WiFi.status() != WL_CONNECTED && attempts < attemptsPerRound) {
        delay(500);
        attempts++;
        Serial.print(".");

        if (displayAvailable) {
          int globalAttempt = round * attemptsPerRound + attempts;
          int progressPercent = globalAttempt * 100 / totalAttempts;

          display.clearDisplay();
          display.setTextColor(DISPLAY_WHITE);
          display.setTextSize(1);

          // SSID name at top
          display.setCursor(4, 2);
          display.print("WiFi: ");
          // Truncate SSID if too long
          if (savedSSID.length() > 14) {
            display.print(savedSSID.substring(0, 12).c_str());
            display.print("..");
          } else {
            display.print(savedSSID.c_str());
          }

          // Status message
          display.setCursor(4, 16);
          display.print("Connecting");
          // Animated dots
          for (int d = 0; d < (attempts % 4); d++) display.print(".");

          // Round info
          display.setCursor(4, 28);
          display.print("Attempt ");
          display.print(round + 1);
          display.print("/");
          display.print(maxRounds);

          // Progress bar
          int barWidth = 104;
          int barHeight = 10;
          int barX = (SCREEN_WIDTH - barWidth) / 2;
          int barY = 42;

          display.drawRoundRect(barX, barY, barWidth, barHeight, 3, DISPLAY_WHITE);
          int fillWidth = (barWidth - 4) * progressPercent / 100;
          if (fillWidth > 0) {
            display.fillRoundRect(barX + 2, barY + 2, fillWidth, barHeight - 4, 2, DISPLAY_WHITE);
          }

          // Percentage
          char pctBuf[8];
          snprintf(pctBuf, sizeof(pctBuf), "%d%%", progressPercent);
          int16_t x1, y1;
          uint16_t w, h;
          display.getTextBounds(pctBuf, 0, 0, &x1, &y1, &w, &h);
          display.setCursor((SCREEN_WIDTH - w) / 2, 55);
          display.print(pctBuf);

          display.display();
        }
      }
      Serial.println();

      if (WiFi.status() == WL_CONNECTED) {
        Serial.println("WiFi Connected using stored credentials!");
        Serial.print("IP Address: ");
        Serial.println(WiFi.localIP());

        WiFi.setTxPower(WIFI_POWER_19_5dBm);
        udp.begin(UDP_PORT);
        Serial.print("UDP listening on port ");
        Serial.println(UDP_PORT);
        return;
      }

      Serial.printf("Round %d failed", round + 1);
      if (round < maxRounds - 1) {
        Serial.println(", retrying after delay...");
        delay(3000);
      } else {
        Serial.println(", all rounds exhausted.");
      }
    }

    Serial.println("All connection attempts failed, starting WiFiManager portal...");
  } else {
    Serial.println("No stored WiFi credentials found.");
  }

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

  Serial.println("WiFi Connected!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  // Set WiFi TX power to maximum for better range
  WiFi.setTxPower(WIFI_POWER_19_5dBm);

  // Start UDP listener
  udp.begin(UDP_PORT);
  Serial.print("UDP listening on port ");
  Serial.println(UDP_PORT);
}

// ========== NTP Functions ==========
void applyTimezone() {
  int gmtOffset_sec = settings.gmtOffset * 60;  // Convert minutes to seconds
  int daylightOffset_sec = settings.daylightSaving ? 3600 : 0;

  configTime(gmtOffset_sec, daylightOffset_sec, NTP_SERVER_PRIMARY, NTP_SERVER_SECONDARY);
}

void initNTP() {
  applyTimezone();
  ntpSynced = false;

  if (displayAvailable) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(10, 20);
    display.println("Syncing time...");
    display.display();
  }

  struct tm timeinfo;
  for (int i = 0; i < 10; i++) {
    if (getLocalTime(&timeinfo, 100)) {
      if (timeinfo.tm_year > 120) {
        ntpSynced = true;
        lastNtpSyncTime = millis();
        Serial.println("NTP time synchronized successfully");
        break;
      }
    }
    delay(100);
  }

  if (!ntpSynced) {
    Serial.println("NTP sync pending, will retry in background");
  }
}

// ========== WiFi Reconnection Handling ==========
// Reconnection interval in milliseconds (try every 30 seconds)
#define WIFI_RECONNECT_INTERVAL 30000

void handleWiFiReconnection() {
  static unsigned long lastReconnectAttempt = 0;

  if (WiFi.status() != WL_CONNECTED) {
    // Update global flag for icon display
    wifiConnected = false;

    if (wifiDisconnectTime == 0) {
      wifiDisconnectTime = millis();
      Serial.println("WiFi disconnected");
    }

    // Periodic reconnection attempt every 30 seconds
    unsigned long currentMillis = millis();
    if (currentMillis - lastReconnectAttempt > WIFI_RECONNECT_INTERVAL) {
      Serial.println("Attempting WiFi reconnection...");
      WiFi.reconnect();
      lastReconnectAttempt = currentMillis;
    }

    // NOTE: Auto-reboot removed - device continues as clock-only
    // NOTE: Display drawing removed - clock functions show small icon instead
  } else {
    // WiFi is connected
    wifiConnected = true;

    if (wifiDisconnectTime != 0) {
      Serial.println("WiFi reconnected successfully!");
      Serial.print("IP Address: ");
      Serial.println(WiFi.localIP());
      wifiDisconnectTime = 0;
      ntpSynced = false;  // Force NTP resync after reconnection
    }
  }
}

// ========== UDP Packet Handling ==========
void handleUDP() {
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
        Serial.printf("ERROR: Packet %d bytes exceeds buffer %d bytes! Data truncated.\n",
                      packetSize, (int)sizeof(buffer));
        udp.flush();
        lastReceived = millis();
        return;
      }

      parseStats(buffer);
      lastReceived = millis();
    }
  }
}

// ========== Stats Parsing ==========
void parseStatsV2(JsonDocument& doc) {
  // Parse status code (1=OK, 2=API error, 3=LHM not running, etc.)
  uint8_t newStatus = doc["status"] | STATUS_OK;

  if (newStatus != metricData.status) {
    // Status changed - log it
    switch (newStatus) {
      case STATUS_OK:
        Serial.println("Status: LHM OK");
        break;
      case STATUS_API_ERROR:
        Serial.println("Status: LHM API error - check REST API");
        break;
      case STATUS_LHM_NOT_RUNNING:
        Serial.println("Status: LHM not running!");
        break;
      case STATUS_LHM_STARTING:
        Serial.println("Status: LHM starting up...");
        break;
      default:
        Serial.printf("Status: Unknown error (%d)\n", newStatus);
        break;
    }
  }
  metricData.status = newStatus;

  const char* ts = doc["timestamp"];
  if (ts && strlen(ts) > 0) {
    // Valid timestamp - update it
    strncpy(metricData.timestamp, ts, 5);
    metricData.timestamp[5] = '\0';
  } else {
    // Empty timestamp signals stale data from Python script (LHM may be down)
    // Keep the previous timestamp - don't overwrite with empty
    Serial.println("Warning: Empty timestamp received (LHM may be recovering)");
  }

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

    if (m.id > 0 && m.id <= MAX_METRICS) {
      bool nameMatches = (settings.metricNames[m.id - 1][0] == '\0' ||
                          strcmp(settings.metricNames[m.id - 1], m.name) == 0);

      if (nameMatches) {
        if (settings.metricLabels[m.id - 1][0] != '\0') {
          strncpy(m.label, settings.metricLabels[m.id - 1], METRIC_NAME_LEN - 1);
          m.label[METRIC_NAME_LEN - 1] = '\0';
        } else {
          strncpy(m.label, m.name, METRIC_NAME_LEN - 1);
          m.label[METRIC_NAME_LEN - 1] = '\0';
        }

        m.displayOrder = settings.metricOrder[m.id - 1];
        m.companionId = settings.metricCompanions[m.id - 1];
        m.position = settings.metricPositions[m.id - 1];
        m.barPosition = settings.metricBarPositions[m.id - 1];
        m.barMin = settings.metricBarMin[m.id - 1];
        m.barMax = settings.metricBarMax[m.id - 1];
        m.barWidth = settings.metricBarWidths[m.id - 1];
        m.barOffsetX = settings.metricBarOffsets[m.id - 1];

        strncpy(settings.metricNames[m.id - 1], m.name, METRIC_NAME_LEN - 1);
        settings.metricNames[m.id - 1][METRIC_NAME_LEN - 1] = '\0';
      } else {
        Serial.printf("Metric ID %d name changed: '%s' -> '%s', using defaults\n",
                      m.id, settings.metricNames[m.id - 1], m.name);

        strncpy(m.label, m.name, METRIC_NAME_LEN - 1);
        m.label[METRIC_NAME_LEN - 1] = '\0';
        m.displayOrder = metricData.count;
        m.companionId = 0;
        m.position = 255;
        m.barPosition = 255;
        m.barMin = 0;
        m.barMax = 100;
        m.barWidth = 60;
        m.barOffsetX = 0;

        strncpy(settings.metricNames[m.id - 1], m.name, METRIC_NAME_LEN - 1);
        settings.metricNames[m.id - 1][METRIC_NAME_LEN - 1] = '\0';
        settings.metricLabels[m.id - 1][0] = '\0';
      }
    } else {
      strncpy(m.label, m.name, METRIC_NAME_LEN - 1);
      m.label[METRIC_NAME_LEN - 1] = '\0';
      m.displayOrder = metricData.count;
      m.companionId = 0;
      m.position = 255;
      m.barPosition = 255;
      m.barMin = 0;
      m.barMax = 100;
      m.barWidth = 60;
      m.barOffsetX = 0;
    }

    metricData.count++;
  }

  metricData.online = true;

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

void parseStats(const char* json) {
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, json);

  if (error) {
    Serial.print("JSON parse error: ");
    Serial.println(error.c_str());
    return;
  }

  parseStatsV2(doc);
}

// ========== Display Status Screens ==========
void displaySetupInstructions() {
  display.clearDisplay();
  display.setTextSize(1);

  display.setCursor(20, 0);
  display.println("WiFi Setup");
  display.drawLine(0, 10, 128, 10, DISPLAY_WHITE);

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
  display.println(ip.c_str());

  display.drawLine(0, 42, 128, 42, DISPLAY_WHITE);

  display.setCursor(4, 48);
  display.println("Open IP in browser");
  display.setCursor(12, 56);
  display.println("to change settings");

  display.display();
}

void displayErrorStatus(uint8_t status) {
  display.clearDisplay();
  display.setTextSize(1);

  // Header with warning
  display.setCursor(30, 0);
  display.println("PC MONITOR");
  display.drawLine(0, 10, 128, 10, DISPLAY_WHITE);

  // Status icon (exclamation mark in box)
  display.drawRect(4, 16, 20, 20, DISPLAY_WHITE);
  display.setTextSize(2);
  display.setCursor(10, 18);
  display.print("!");
  display.setTextSize(1);

  // Status message
  display.setCursor(30, 18);
  switch (status) {
    case STATUS_API_ERROR:
      display.println("LHM API Error");
      display.setCursor(30, 28);
      display.println("Check REST API");
      break;
    case STATUS_LHM_NOT_RUNNING:
      display.println("LHM Not Running");
      display.setCursor(30, 28);
      display.println("Start LHM app");
      break;
    case STATUS_LHM_STARTING:
      display.println("LHM Starting");
      display.setCursor(30, 28);
      display.println("Please wait...");
      break;
    default:
      display.println("Unknown Error");
      display.setCursor(30, 28);
      display.print("Code: ");
      display.println(status);
      break;
  }

  display.drawLine(0, 42, 128, 42, DISPLAY_WHITE);

  // Show timestamp if available
  if (metricData.timestamp[0] != '\0') {
    display.setCursor(4, 48);
    display.print("Last OK: ");
    display.println(metricData.timestamp);
  }

  // Show IP for reference
  display.setCursor(4, 56);
  display.print("IP: ");
  display.println(WiFi.localIP().toString().c_str());

  display.display();
}
