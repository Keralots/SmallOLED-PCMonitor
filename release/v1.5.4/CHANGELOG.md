## Download Instructions

**Make sure you download the correct version for your screen size!**

| Use Case | File to Download |
|----------|------------------|
| **New device** (first time flashing) | `firmware-1.5.4-OLED_0.96inch.bin` or `firmware-1.5.4-OLED_1.3inch.bin` |
| **Existing device** (OTA update via web interface) | `OTA_ONLY_firmware-1.5.4-OLED_0.96inch.bin` or `OTA_ONLY_firmware-1.5.4-OLED_1.3inch.bin` |


# v1.5.4 - Changelog

## New Features

### Additional Date Display Format
- Added a new `DD.MM.YYYY` date format option, for example `09.05.2026`
- The new format is selectable from the web UI alongside the existing options:
  - `DD/MM/YYYY`
  - `MM/DD/YYYY`
  - `YYYY-MM-DD`
- The new date format is supported across all clock displays:
  - **Standard Clock**
  - **Large Clock**
  - **Mario Animation**
  - **Space Invaders**
  - **Arkanoid / Pong**
  - **Pac-Man Clock**

## Bug Fixes

### Animated Clock Time Drift After Long Uptime
- Fixed animated clocks drifting several minutes behind real time after long uptime
- The issue affected cached-time animated clocks such as **Pac-Man** and **Arkanoid / Pong**, while Standard and Large clocks stayed correct
- Added shared override maintenance so Mario, Pac-Man, Space, and Pong all clear stale animated-time overrides consistently
- Pong now clears inherited time overrides instead of displaying a stale cached time indefinitely
- Touch-button clock cycling, web saves, and config imports now reset animated clock state cleanly when the clock style changes
- The animated-time timeout is no longer extended by every digit update, preventing long-running animations from sliding the safety window forward indefinitely

## Internal Changes

- Added centralized `maintainTimeOverride()` logic for animated clock time synchronization
- Added centralized `resetClockAnimationState()` logic for clock-style changes
- Preserved the existing 60 second `TIME_OVERRIDE_MAX_MS` safety window to avoid cutting off slow user-configured animations
- Related commits: `8d6ed3a`, `eb9076f`
