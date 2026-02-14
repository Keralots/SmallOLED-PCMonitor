# v1.5.0 Release Notes

## New Features

- **Automatic Timezone & DST** - Replaced manual GMT offset + DST checkbox with a region-based timezone selector (~50 regions). DST transitions happen automatically using POSIX timezone strings. Old settings are auto-migrated.
- **Large Text Display Modes** - Two new row modes: Large 2-Row and Large 3-Row with double-size text in single-column layout for better readability at a distance.
- **SPI Display Interface** - Compile-time `DISPLAY_INTERFACE` switch to use SPI instead of I2C. Configurable SPI pins in `user_config.h`.
- **LED Gesture Control** - Redesigned TTP223 touch gestures: quick tap (< 500ms) toggles mode/cycles clocks, medium press (500ms-1s) toggles LED on/off, long hold (> 1s) ramps LED brightness with gamma correction.
- **Show IP at Boot** - New setting to show/hide the IP address on OLED at startup (configurable via web interface).
- **QR Code WiFi Setup** - Optional compile-time feature (`QR_SETUP_ENABLED`) that shows a scannable QR code on the OLED during WiFi AP setup.

## Bug Fixes

- **Fixed clock drifting ~3 hours after days of uptime** - WiFi reconnection now restarts the SNTP client and reapplies timezone. Added periodic hourly NTP re-sync as safety net.
- **Fixed animated clock digit revert** - Animation trigger changed from :55 to :56 seconds in Mario, Space Invaders, and Pac-Man clocks, preventing digits from briefly showing the old value.
- **Fixed Pong ball diagonal speed** - Balls moving diagonally no longer exceed maximum speed (clamped by magnitude instead of per-axis).
- **Fixed timezone dropdown showing wrong region** - Multiple regions sharing the same timezone rules (Central European, Scandinavian, Central Balkan) now correctly display the user's actual selection.
- **Fixed UDP oversized packet handling** - Packet size is validated before reading, preventing processing of truncated data.
- **Fixed Pac-Man eaten pellets initialization** - Prevents visual glitches from uninitialized memory on first display.
- **Fixed scheduled dimming edge case** - Same start/end hour now correctly treated as "no dim period".
- **Fixed progress bar off-screen drawing** - Bars outside display bounds are now skipped.
- **Fixed SH1106 initialization** - Now properly checks return value of `display.begin()`.
- **Fixed WiFi disconnect not marking PC offline** - Display correctly shows offline status when WiFi drops.
- **Fixed buffer overflow in `safeCopyString`** - Uses `strncpy` with explicit null termination.

## Improvements

- **Smoother animations** - Auto-boost refresh rate increased from 40 Hz to 60 Hz.
- **Faster touch response** - Button debounce reduced from 200ms to 50ms.
- **More reliable NTP sync** - Retry attempts increased from 10 to 30; SNTP client restarted on failure.
- **Metrics API uses ArduinoJson** - Proper JSON escaping for metric names/labels.
- **Code cleanup** - ~50 lines of redundant forward declarations removed.

## Breaking Changes

- **Timezone setting format** - Old GMT offset dropdown replaced by region selector. Existing configs auto-migrate.
- **Touch gesture timing** - Short press threshold reduced from 1000ms to 500ms. Medium press (500ms-1s) now toggles LED.
