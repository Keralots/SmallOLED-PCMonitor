/*
 * SmallOLED-PCMonitor - Network Module
 *
 * WiFi connection management, UDP packet handling, and NTP sync.
 */

#ifndef NETWORK_H
#define NETWORK_H

#include <WiFi.h>
#include <WiFiUdp.h>
#include <WiFiManager.h>
#include <ArduinoJson.h>
#include "../config/config.h"

// ========== Global Network Objects ==========
extern WiFiUDP udp;
extern WiFiManager wifiManager;

// ========== Network Functions ==========

// Initialize WiFi and UDP
void initNetwork();

// Apply static IP settings if configured
void applyStaticIP();

// Initialize NTP time synchronization
void initNTP();

// Apply timezone settings
void applyTimezone();

// Handle UDP packet reception
void handleUDP();

// Parse incoming stats JSON
void parseStats(const char* json);
void parseStatsV2(JsonDocument& doc);

// WiFi reconnection handling
void handleWiFiReconnection();

// Display connection status screens
void displaySetupInstructions();
void displayConnecting();
void displayConnected();
void displayErrorStatus(uint8_t status);

// WiFi callbacks for WiFiManager
void configModeCallback(WiFiManager *myWiFiManager);
void saveConfigCallback();

// Manual WiFi connection (for hardcoded credentials)
bool connectManualWiFi(const char* ssid, const char* password);

#endif // NETWORK_H
