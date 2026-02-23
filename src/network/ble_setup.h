/*
 * SmallOLED-PCMonitor - BLE WiFi Provisioning
 *
 * Enables the Android app to push WiFi credentials to the device via Bluetooth
 * instead of the traditional WiFiManager AP portal. Active only on first boot
 * when no WiFi credentials are saved. Falls back to AP mode on timeout/failure.
 *
 * Enable via: #define BLE_SETUP_ENABLED 1  in src/config/user_config.h
 */

#ifndef BLE_SETUP_H
#define BLE_SETUP_H

#include "../config/user_config.h"

#if BLE_SETUP_ENABLED

// Try connecting with ESP32's internally saved WiFi credentials (fast path).
// Skips BLE entirely if valid credentials exist and the connection succeeds.
// Returns true if WiFi connected, false if no credentials saved or connection failed.
bool tryConnectSavedWiFi();

// Run BLE provisioning: advertise a GATT server, wait for the Android app to
// write SSID + password, then attempt WiFi connection.
// Sends BLE status notifications so the app knows the result.
// Returns true if WiFi successfully connected, false on 2-min timeout or failure.
// On failure, caller should fall through to initNetwork() (AP mode).
bool runBleProvisioning();

#endif // BLE_SETUP_ENABLED
#endif // BLE_SETUP_H
