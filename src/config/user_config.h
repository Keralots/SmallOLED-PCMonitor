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
//   1 = SH1106  (1.3" OLED, larger displays - has 132x64 RAM with 2-col offset)
//   2 = SSD1309 (2.42" OLED, uses SSD1306 driver - 128x64 RAM, no offset)
//
// CHANGE THIS VALUE to match your OLED display type!
#define DEFAULT_DISPLAY_TYPE 2

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
#define AP_PASSWORD "monitor123"

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

// Maximum time (ms) that animated clocks can override NTP time
// After this, force resync even if animation is still running
// Prevents clock drift when packets are dropped during animations
#define TIME_OVERRIDE_MAX_MS 60000

// ========== Watchdog Configuration ==========
// Watchdog timeout in seconds
#define WATCHDOG_TIMEOUT_SECONDS 30

// ========== Touch Button Configuration ==========
// TTP223 capacitive touch sensor support
// Enable touch button to toggle between PC metrics and clock mode
// - When PC is online: Button toggles between metrics and clock
// - When PC is offline: Button cycles through clock animations (0-6)
#define TOUCH_BUTTON_ENABLED 1           // 1 = enabled, 0 = disabled (default)
#define TOUCH_BUTTON_PIN 7               // GPIO pin for TTP223 signal (default: GPIO 7)
#define TOUCH_DEBOUNCE_MS 200            // Debounce delay in milliseconds (default: 200ms)
#define TOUCH_ACTIVE_LEVEL HIGH          // HIGH = active HIGH, LOW = active LOW (TTP223 default: HIGH)

#endif // USER_CONFIG_H
