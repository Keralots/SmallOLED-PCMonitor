## Download Instructions

**Make sure you download the correct version for your screen size!**

| Use Case | File to Download |
|----------|------------------|
| **New device** (first time flashing) | `firmware-v1.9.0-OLED_0.96inch.bin`, `firmware-v1.9.0-OLED_1.3inch.bin` or `firmware-v1.9.0-OLED_1.54inch.bin` |
| **Existing device** (OTA update via web interface) | `OTA_ONLY_firmware-v1.9.0-OLED_0.96inch.bin`, `OTA_ONLY_firmware-v1.9.0-OLED_1.3inch.bin` or `OTA_ONLY_firmware-v1.9.0-OLED_1.54inch.bin` |
| **New ESP32-S3 device** | `firmware-v1.9.0-ESP32-S3-OLED_0.96inch.bin`, `firmware-v1.9.0-ESP32-S3-OLED_1.3inch.bin` or `firmware-v1.9.0-ESP32-S3-OLED_1.54inch.bin` |
| **Existing ESP32-S3 device** | `OTA_ONLY_firmware-v1.9.0-ESP32-S3-OLED_0.96inch.bin`, `OTA_ONLY_firmware-v1.9.0-ESP32-S3-OLED_1.3inch.bin` or `OTA_ONLY_firmware-v1.9.0-ESP32-S3-OLED_1.54inch.bin` |

> 0.96" SSD1306 and 2.42" SSD1309 share the `0.96inch` image. 1.3" SH1106 and 1.54" CH1116 each have their own image - do not mix them up. ESP32-S3 boards (SuperMini or any S3 with 4MB+ flash) take the `ESP32-S3` images only; the wiring is the same as on the C3.

## PC companion app

No changes since v1.7.0. `pc_stats_monitor_v4.exe` is attached to this release as well. Linux users run it from source: [`PC-Companion-App-v4/linux-companion/`](https://github.com/Keralots/SmallOLED-PCMonitor/tree/main/PC-Companion-App-v4/linux-companion).

You can also flash directly from your browser (no tools to install): https://smalloled.stolaris.dev/

> **Older devices need one USB re-flash.** Devices still on the original partition layout (first flashed before v1.5.1 and only updated over the air since) cannot take this build over OTA. Export your config, re-flash once with the browser flasher, then import the backup.


# v1.9.0 - Changelog

## New

- **Weather clock.** Current conditions from Open-Meteo (free, no account or API key): an animated icon (sun, moon and stars, drifting clouds, rain, snow, fog, lightning, gusts when it is windy), the temperature, today's high and low, humidity and sunrise/sunset. Five layouts: classic, or current weather next to the next three days (as rows or columns), a 12-hour temperature curve with rain chances, or tomorrow in focus. Find your city on the web page, Celsius or Fahrenheit. It can also join the clock rotation.
- **Update notice.** When a newer release is out, the web page shows a badge in the top bar, and the Maintenance page names the update file that matches your board and display.

## Fixes

- **Settings could be changed from other websites.** A page open in your browser could make it send a save, factory reset or firmware upload to the device. These requests now need a header that only the device's own page sends. If you update or save settings from your own scripts, add `-H "X-SmallOLED: 1"` (for example `curl -H "X-SmallOLED: 1" -F "firmware=@firmware.bin" http://smalloled.local/update`). The PC companion app is not affected.
- **Clock rotation cut animations short.** With "Cycle All Styles", a minute change that started near the end of a clock's turn was cut off, most visibly on Snake, Tetris and Dino when the hour changed. The rotation now waits for the animation to finish.
- **Brisbane and Darwin jumped an hour in October.** Queensland and the Northern Territory do not use daylight saving time, but the only matching regions (Sydney, Adelaide) do. Both now have their own region without it, and the timezone list is sorted by UTC offset.
