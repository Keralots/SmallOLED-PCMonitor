## Download Instructions

**Make sure you download the correct version for your screen size!**

| Use Case | File to Download |
|----------|------------------|
| **New device** (first time flashing) | `firmware-v1.8.0-OLED_0.96inch.bin`, `firmware-v1.8.0-OLED_1.3inch.bin` or `firmware-v1.8.0-OLED_1.54inch.bin` |
| **Existing device** (OTA update via web interface) | `OTA_ONLY_firmware-v1.8.0-OLED_0.96inch.bin`, `OTA_ONLY_firmware-v1.8.0-OLED_1.3inch.bin` or `OTA_ONLY_firmware-v1.8.0-OLED_1.54inch.bin` |

> 0.96" SSD1306 and 2.42" SSD1309 share the `0.96inch` image. 1.3" SH1106 and 1.54" CH1116 each have their own image - do not mix them up.

## PC companion app

No changes since v1.7.0 - keep using `pc_stats_monitor_v4.exe` from the [v1.7.0 release](https://github.com/Keralots/SmallOLED-PCMonitor/releases/tag/v1.7.0). Linux users run it from source: [`PC-Companion-App-v4/linux-companion/`](https://github.com/Keralots/SmallOLED-PCMonitor/tree/main/PC-Companion-App-v4/linux-companion).

You can also flash directly from your browser (no tools to install): https://smalloled.stolaris.dev/

> **Older devices need one USB re-flash.** Devices still on the original partition layout (first flashed before v1.5.1 and only updated over the air since) cannot take this build over OTA. Export your config, re-flash once with the browser flasher, then import the backup.


# v1.8.0 - Changelog

## New

- **Game mode.** Pair an Xbox Wireless Controller over Bluetooth and play six games on the OLED: Falling Blocks, Snake, Bricks, Space Rocks, Runner and Defenders. High scores are saved. Start it from the web page ("Game mode") or with a triple tap on the touch button. The controller needs a recent firmware (BLE capable, update it with the Xbox Accessories app).
- **Game of Life clock.** A Conway colony lives around the digits; a changed digit dissolves into the colony and the survivors form the new one.
- **Dragon Ball clock.** Goku trains, spars and fights visitors, then blasts the changed digit with a Kamehameha.
- **Custom clock rotation.** "Cycle All Styles" is now your own list: pick the clocks, their order and how long each one stays.
- **Configurable touch pin.** The touch button GPIO is now a setting, no reflash needed.
- **LED control over HTTP.** The night light can be switched and dimmed through the API.

## Fixes

- **Tap also toggled the LED.** A quick tap on the touch button could switch the night light on top of its normal action.
- **Metric layout wiped on save.** Saving or importing settings before the PC sent any data reset positions, companions and progress bars.
- **Wrong display type reported.** The web page header and `/api/info` always said SSD1306, whatever panel was fitted.
- **Animated clocks stuck on an old minute.** Switching back from the stats screen or the visualizer near the end of a minute could leave the clock counting from the wrong time.
