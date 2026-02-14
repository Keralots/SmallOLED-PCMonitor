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
#define DEFAULT_DISPLAY_TYPE 1

// I2C pins for ESP32-C3
#define I2C_SDA_PIN 8
#define I2C_SCL_PIN 9

// Screen dimensions
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

// I2C Address (typically 0x3C, some displays use 0x3D)
#define DISPLAY_I2C_ADDRESS 0x3C

// ========== Display Interface ==========
// Interface type:
//   0 = I2C (default, uses SDA/SCL pins above)
//   1 = SPI (uses SPI pins below, faster refresh for animations)
//
// CHANGE THIS VALUE to use SPI instead of I2C
#define DISPLAY_INTERFACE 0

// SPI pins for ESP32-C3 (only used when DISPLAY_INTERFACE = 1)
#define SPI_MOSI_PIN 6 //SDA
#define SPI_SCK_PIN  4 //SCK SPI Clock
#define SPI_CS_PIN   5 //CS (Chip Select)
#define SPI_DC_PIN   3 //DC (Data/Command)
#define SPI_RST_PIN  10   //RES Set to -1 if RST is not connected

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
// - Quick tap (< 500ms): Toggle metrics/clock mode or cycle clock styles
// - Medium press (500ms-1s, release): Toggle LED night light on/off
// - Long hold (> 1s): Ramp LED brightness up/down (gamma-corrected)
// Note: If TTP223 is not connected, GPIO 7 just floats harmlessly
#define TOUCH_BUTTON_ENABLED 1           // 1 = enabled, 0 = disabled (always enabled now)
#define TOUCH_BUTTON_PIN 7               // GPIO pin for TTP223 signal (default: GPIO 7)
#define TOUCH_DEBOUNCE_MS 50            // Debounce delay in milliseconds (default: 100ms)
#define TOUCH_ACTIVE_LEVEL HIGH          // HIGH = active HIGH, LOW = active LOW (TTP223 default: HIGH)

// ========== LED PWM Night Light Configuration ==========
// Filament LED night light control via GPIO 1 and 2N2222 transistor
// Gesture-based control using TTP223 touch button
#define LED_PWM_ENABLED 1                // 1 = enabled, 0 = disabled (default: 0)
#define LED_PWM_PIN 1                    // GPIO pin for PWM LED control (GPIO 1)
#define LED_PWM_CHANNEL 0                // PWM channel (0-15)
#define LED_PWM_FREQ 5000                // PWM frequency in Hz
#define LED_PWM_RESOLUTION 8             // 8-bit resolution (0-255 brightness levels)

// ========== QR Code Setup Configuration ==========
// Display QR code during WiFi AP setup for easy mobile connection
// When enabled: OLED shows scannable QR code instead of text instructions
// When disabled: Traditional text instructions (original behavior)
#define QR_SETUP_ENABLED 0               // 1 = QR code, 0 = text instructions

#endif // USER_CONFIG_H
