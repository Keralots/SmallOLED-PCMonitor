/*
 * SmallOLED-PCMonitor - Network Module
 *
 * WiFi connection management, UDP packet handling, and NTP sync.
 */

#include "network.h"
#include "../display/display.h"
#include "../utils/utils.h"
#include <Preferences.h>

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
void handleWiFiReconnection() {
  if (WiFi.status() != WL_CONNECTED) {
    if (wifiDisconnectTime == 0) {
      wifiDisconnectTime = millis();
      Serial.println("WiFi disconnected, attempting reconnection...");
      WiFi.reconnect();
    }

    if (millis() - wifiDisconnectTime > WIFI_RECONNECT_TIMEOUT) {
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
  const char* ts = doc["timestamp"];
  if (ts) {
    strncpy(metricData.timestamp, ts, 5);
    metricData.timestamp[5] = '\0';
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
