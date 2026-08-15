## Download Instructions

**Make sure you download the correct version for your screen size!**

| Use Case | File to Download |
|----------|------------------|
| **New device** (first time flashing) | `firmware-v1.6.3-OLED_0.96inch.bin`, `firmware-v1.6.3-OLED_1.3inch.bin` or `firmware-v1.6.3-OLED_1.54inch.bin` |
| **Existing device** (OTA update via web interface) | `OTA_ONLY_firmware-v1.6.3-OLED_0.96inch.bin`, `OTA_ONLY_firmware-v1.6.3-OLED_1.3inch.bin` or `OTA_ONLY_firmware-v1.6.3-OLED_1.54inch.bin` |

> 0.96" SSD1306 and 2.42" SSD1309 share the `0.96inch` image. 1.3" SH1106 and 1.54" CH1116 each have their own image - do not mix them up.

## PC companion app

`pc_stats_monitor_v4.exe` (Windows) is attached to this release - download and double-click it, no Python needed. It sends your PC's sensors to the device and gives you a web-style config window (1:1 OLED preview, drag-and-drop layout, sensor picker) that lives in the system tray. Linux users run the same app from source: [`PC-Companion-App-v4/linux-companion/`](https://github.com/Keralots/SmallOLED-PCMonitor/tree/main/PC-Companion-App-v4/linux-companion).

Full setup guide - LibreHardwareMonitor, sensor picking, autostart and troubleshooting: [win-companion README](https://github.com/Keralots/SmallOLED-PCMonitor/blob/main/PC-Companion-App-v4/win-companion/README.md#for-end-users-using-the-exe).

> **First run:** the exe is unsigned, so Windows SmartScreen may show *"Windows protected your PC"*. Click **More info -> Run anyway**. This is normal for small unsigned tools and not a sign of anything wrong with the download.

> The companion app is versioned separately from the firmware; this exe is the current build and works with any 1.6.x device.

You can also flash directly from your browser (no tools to install): https://keralots.github.io/SmallOLED-PCMonitor/

> **Heads up for older devices:** the firmware has grown and is now close to the OTA size limit on devices still using the original partition layout. If an OTA update ever fails with a "size" or "no space" error, export your config from the Maintenance page, re-flash once with the browser flasher (this updates the storage layout automatically), then import your backup. After that, OTA updates work normally again.


# v1.6.3 - Changelog

## New

- **Half-hour and quarter-hour timezones:** added the regions that sit on :30 and :45 UTC offsets and were previously missing from the list - UTC+3:30, UTC+4:30 (Kabul), UTC+5:45 (Kathmandu), UTC+6:30 (Yangon) and UTC-3:30 (Newfoundland). If your region uses a half-hour offset and you couldn't find it before, it's in the list now.
- **Timezone list shows the UTC offset:** every entry in the Timezone dropdown is now prefixed with its offset, e.g. `(UTC+03:30) ...`, so you can locate your zone by number instead of hunting through country names.

## Fixes

- **Night-time wake-up:** a single bad clock reading could briefly pull the panel out of scheduled night mode and light it up. One glitchy reading no longer wakes the display at night.
