/*
 * SmallOLED-PCMonitor - User Configuration
 *
 * ============================================
 *   THIS IS THE ONLY FILE YOU NEED TO EDIT
 *   TO CONFIGURE YOUR HARDWARE!
 * ============================================
 *
 * Modify these values to match your hardware setup.
 */

#ifndef USER_CONFIG_H
#define USER_CONFIG_H

// ========== Display Configuration ==========
// Display type:
//   0 = SSD1306 (0.96" OLED, common small displays)
//   1 = SH1106  (1.3" OLED, larger displays)
//
// CHANGE THIS VALUE to match your OLED display type!
#define DEFAULT_DISPLAY_TYPE 1

// I2C pins for ESP32-C3
#define I2C_SDA_PIN 8
#define I2C_SCL_PIN 9

// Screen dimensions
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

// I2C Address (typically 0x3C, some displays use 0x3D)
#define DISPLAY_I2C_ADDRESS 0x3C

// ========== WiFi Configuration ==========
// Access Point name and password for initial setup
#define AP_NAME "PCMonitor-Setup"
#define AP_PASSWORD "pcmonitor123"

// ========== Optional Hardcoded WiFi Credentials ==========
// Use this if your ESP32 module has a faulty WiFi AP mode
// Set SSID and password to your home network, then upload
// Leave as empty strings "" to use normal WiFiManager portal
#define HARDCODED_WIFI_SSID ""
#define HARDCODED_WIFI_PASSWORD ""

// WiFi reconnection timeout (ms) - restart if WiFi lost for this long
#define WIFI_RECONNECT_TIMEOUT 60000

// ========== Network Configuration ==========
// UDP port for receiving PC stats
#define UDP_PORT 4210

// NTP servers
#define NTP_SERVER_PRIMARY "pool.ntp.org"
#define NTP_SERVER_SECONDARY "time.nist.gov"

// NTP resync interval (1 hour in ms)
#define NTP_RESYNC_INTERVAL 3600000

// ========== Timing Configuration ==========
// Timeout for PC stats (ms) - show clock if no data received
#define STATS_TIMEOUT 10000

// ========== Watchdog Configuration ==========
// Watchdog timeout in seconds
#define WATCHDOG_TIMEOUT_SECONDS 30

#endif // USER_CONFIG_H
