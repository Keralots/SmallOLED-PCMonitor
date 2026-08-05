## Download Instructions

**Make sure you download the correct version for your screen size!**

| Use Case | File to Download |
|----------|------------------|
| **New device** (first time flashing) | `firmware-v1.6.2-OLED_0.96inch.bin`, `firmware-v1.6.2-OLED_1.3inch.bin` or `firmware-v1.6.2-OLED_1.54inch.bin` |
| **Existing device** (OTA update via web interface) | `OTA_ONLY_firmware-v1.6.2-OLED_0.96inch.bin`, `OTA_ONLY_firmware-v1.6.2-OLED_1.3inch.bin` or `OTA_ONLY_firmware-v1.6.2-OLED_1.54inch.bin` |

> 0.96" SSD1306 and 2.42" SSD1309 share the `0.96inch` image. 1.3" SH1106 and 1.54" CH1116 each have their own image - do not mix them up.

You can also flash directly from your browser (no tools to install): https://keralots.github.io/SmallOLED-PCMonitor/

> **Heads up for older devices:** the firmware has grown and is now close to the OTA size limit on devices still using the original partition layout. If an OTA update ever fails with a "size" or "no space" error, export your config from the Maintenance page, re-flash once with the browser flasher (this updates the storage layout automatically), then import your backup. After that, OTA updates work normally again.


# v1.6.2 - Changelog

## Fixes

- **Scheduled night mode at boot:** if the device restarted during its scheduled dim hours, the panel used to light up at full brightness for up to a minute before correcting itself. It now applies the dimming schedule immediately at boot, and skips the 5-second boot IP screen when the schedule wants the panel dark - so a restart in the middle of the night no longer lights up the room.
- **Cycle All Styles clock:** when the clock rotated to the next style every 5 minutes it switched the instant the block changed, clipping the outgoing minute-change animation every time. The switch now waits a few seconds for the animation to finish before moving on.

## Internal

- Pinned the build platform to `espressif32@6.12.0` (arduino-esp32 2.0.17) so the firmware builds reliably regardless of which ESP32 platform version happens to be installed.
- Fixed a follow-up to the boot dimming change where the schedule check could briefly stall the main loop (laggy web UI, dropped touch presses, frozen frames) when WiFi was connected but the time had not synced yet.
- `/api/status` now reports the display's actual on/off state - scheduled dimming could drive the panel to 0 while the status still claimed it was on.
- `/api/info` now includes `resetReason`, so an unexplained restart can be traced after the fact.
