# v1.5.1 - Changelog

## New Features

### mDNS Discovery
- Device is now discoverable on local network via `http://smalloled.local`
- No more memorizing IP addresses - just type the hostname in your browser
- Configurable device name (default: `smalloled`) via web interface Settings > Device Name
- mDNS name updates automatically when device name is changed

### BLE WiFi Provisioning (Experimental)
- New Bluetooth Low Energy setup option for the upcoming SmallOLED Android app
- On first boot (no saved WiFi), device advertises as BLE GATT server
- Android app connects, sends WiFi credentials, device connects and saves them
- Falls back to AP mode automatically if BLE times out (2 min) or fails
- Disabled by default (`BLE_SETUP_ENABLED 0` in `user_config.h`)
- Requires `min_spiffs.csv` partition table (already configured in `platformio.ini`)

### Android App API Endpoints
- Added new REST API endpoints to support the upcoming SmallOLED Android companion app
- Device discovery and configuration management from mobile

### Configurable AP Password
- WiFi setup Access Point is now open (no password) by default for easier first-time setup
- Password can be set via `AP_PASSWORD` in `user_config.h` if security is needed

### Factory Reset
- "Reset WiFi Settings" button replaced with full **Factory Reset**
- Erases all settings (WiFi, display, metrics, animations, network) and restarts in AP setup mode
- Two-step confirmation with backup reminder before proceeding

## Bug Fixes

- Fixed mDNS UDP socket error on startup
- Fixed LED initialization timing issue
- Fixed LED PWM brightness jump at 100% duty cycle — gamma output capped at 254 to keep LED drivers in PWM dimming mode (avoids visible jump when using constant-current driver boards)

## Documentation

### README.md Major Update
- Added new **Advanced / Optional Features** section covering:
  - SPI display interface (alternative to I2C, faster refresh)
  - LED PWM night light with TTP223 touch gestures
  - QR code WiFi setup display
  - BLE provisioning (experimental)
  - Scheduled dimming / night mode
  - OTA firmware updates
- Fixed typos: "anthenna" -> "antenna", "faluty" -> "faulty", "Fisr" -> "First"
- Renamed "Pong" clock to "Arkanoid" (correct name for breakout-style animation)
- Updated display type table: 2.42" SSD1309 works with type 0 (same as SSD1306)
- Added all 6 clock styles with descriptions
- Added Large 2-row/3-row display modes documentation
- Added mDNS discovery instructions
- Updated timezone section (automatic DST with ~50 regions)
- Added LibreHardwareMonitor 0.9.5+ REST API auto-detection note
- Added RGB LED note for SuperMini boards with external antenna
- Mentioned Android app development in progress

## Dependencies

- Added `h2zero/NimBLE-Arduino@^1.4.2` (BLE provisioning library, dead-stripped when disabled)
- Changed partition table to `min_spiffs.csv` (supports OTA + BLE)

## New Files

- `src/network/ble_setup.cpp` - BLE GATT server for WiFi provisioning
- `src/network/ble_setup.h` - BLE setup header and declarations

## Configuration Changes

- `user_config.h`: Added `BLE_SETUP_ENABLED`, `BLE_DEVICE_NAME`, `AP_PASSWORD` options
- `config.h`: Added `deviceName[32]` field to Settings struct
- `settings.cpp`: Added device name persistence (NVS key: `deviceName`)
