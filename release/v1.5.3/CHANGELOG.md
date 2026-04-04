## Download Instructions

**Make sure you download the correct version for your screen size!**

| Use Case | File to Download |
|----------|------------------|
| **New device** (first time flashing) | `firmware-v1.5.3-OLED_0.96inch.bin` or `firmware-v1.5.3-OLED_1.3inch.bin` |
| **Existing device** (OTA update via web interface) | `OTA_ONLY_firmware-v1.5.3-OLED_0.96inch.bin` or `OTA_ONLY_firmware-v1.5.3-OLED_1.3inch.bin` |


# v1.5.3 - Changelog

## New Features

### Full 12-Hour Format Support for Offline Clocks
- Fixed the `12-hour / 24-hour` setting so it now works consistently across all offline/manual clock styles
- Animated clock styles now respect the selected time format:
  - **Mario Animation**
  - **Space Invaders**
  - **Arkanoid / Pong**
  - **Pac-Man Clock**
- Added `AM/PM` indicators for animated clock styles when 12-hour format is enabled
- Large Clock keeps its existing layout, with the `AM/PM` indicator moved to avoid overlapping the large minute digits
- Shared time formatting logic was unified so midnight/noon transitions behave correctly across styles:
  - `00:xx -> 12:xx AM`
  - `11:59 -> 12:00 PM`
  - `12:59 -> 01:00 PM`
  - `23:59 -> 12:00 AM`

### Touch-Gated OLED Off + Temporary Wake
- Display brightness `0` now means **true OLED off**, but only on builds with `TOUCH_BUTTON_ENABLED`
- Added **temporary tap-to-wake** when the display is off:
  - short press wakes the OLED for 10 seconds
  - after timeout, brightness returns to the scheduled/current target level
- This behavior is limited to devices that actually have the touch/button hardware enabled

## Bug Fixes

### Brightness Safety for No-Button Builds
- Devices built **without** touch button support can no longer set display brightness to `0`
- The same safety rule now applies to scheduled dim brightness
- Protection is enforced in multiple places:
  - Web UI sliders
  - form save handling
  - persisted settings loaded from NVS
- Prevents users from configuring a fully dark screen on hardware that has no input method to wake it

### Mario Clock - Koopa Facing Direction
- Fixed Koopa Troopa in Mario idle encounters walking with its head on the wrong side
- Koopa sprite now faces the actual movement direction during encounters

## UI Improvements

### Display Settings Page
- Brightness help text now changes depending on whether the build supports the touch button
- Builds with touch/button explain that `0%` can be used with tap-to-wake
- Builds without touch/button explain that the minimum brightness is `1%`

## Internal Changes

- Unified offline clock time formatting and rendered-time state handling for 12h/24h mode
- Added shared brightness sanitization helpers in settings persistence
- Refactored display brightness handling to work correctly across supported OLED driver types
- Added temporary wake handling inside the display module instead of relying on display-type-specific shortcuts

