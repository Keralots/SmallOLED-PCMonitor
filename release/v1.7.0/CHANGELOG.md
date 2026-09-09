## Download Instructions

**Make sure you download the correct version for your screen size!**

| Use Case | File to Download |
|----------|------------------|
| **New device** (first time flashing) | `firmware-v1.7.0-OLED_0.96inch.bin`, `firmware-v1.7.0-OLED_1.3inch.bin` or `firmware-v1.7.0-OLED_1.54inch.bin` |
| **Existing device** (OTA update via web interface) | `OTA_ONLY_firmware-v1.7.0-OLED_0.96inch.bin`, `OTA_ONLY_firmware-v1.7.0-OLED_1.3inch.bin` or `OTA_ONLY_firmware-v1.7.0-OLED_1.54inch.bin` |

> 0.96" SSD1306 and 2.42" SSD1309 share the `0.96inch` image. 1.3" SH1106 and 1.54" CH1116 each have their own image - do not mix them up.

## PC companion app

`pc_stats_monitor_v4.exe` (Windows) is attached to this release - download and double-click it, no Python needed. It sends your PC's sensors to the device and gives you a web-style config window (1:1 OLED preview, drag-and-drop layout, sensor picker) that lives in the system tray. Linux users run the same app from source, no build step: [`PC-Companion-App-v4/linux-companion/`](https://github.com/Keralots/SmallOLED-PCMonitor/tree/main/PC-Companion-App-v4/linux-companion).

Full setup guide - LibreHardwareMonitor, sensor picking, autostart and troubleshooting: [win-companion README](https://github.com/Keralots/SmallOLED-PCMonitor/blob/main/PC-Companion-App-v4/win-companion/README.md#for-end-users-using-the-exe).

> **Update the app together with the firmware this time** - the audio visualizer needs the new companion to send the sound data.

> **First run:** the exe is unsigned, so Windows SmartScreen may show *"Windows protected your PC"*. Click **More info -> Run anyway**. This is normal for small unsigned tools and not a sign of anything wrong with the download.

You can also flash directly from your browser (no tools to install): https://keralots.github.io/SmallOLED-PCMonitor/

> **Heads up for older devices:** this build is the biggest yet and sits very close to the OTA size limit on devices still using the original partition layout. If an OTA update fails with a "size" or "no space" error, export your config from the Maintenance page, re-flash once with the browser flasher (this updates the storage layout automatically), then import your backup. After that, OTA updates work normally again.


# v1.7.0 - Changelog

## New

- **Audio visualizer.** The device now reacts to whatever your PC is playing. The companion app captures the sound coming out of your speakers and streams it over. Three styles: **Classic EQ**, **Mirror EQ** (bars growing from the centre) and an **Oscilloscope** trace, with solid / segmented / outline bars, optional peak-hold dots and an optional corner clock. It steps aside for the clock when the room goes quiet and comes straight back when sound returns. Turn it on in the companion app's **Audio** page.
- **TRON clock.** A new clock face inspired by the light-cycle arena, side-profile or overhead bike, with the arena grid and border each switchable.
- **Custom NTP servers.** Point the clock at your router or a local time source instead of the public pools. A **Test** button probes each server and reports them individually. Leave a field blank to keep the default.
- **The stats port is now a setting.** The UDP port the device listens on (4210) used to be baked into the firmware - if something on your network already used it, you had to edit the source and reflash. It is now a normal setting, applied without a reboot.
- **Linux load average.** The Linux companion can send the 1-minute load average as a metric. Thanks to [@taintedkernel](https://github.com/taintedkernel) (#62).

## Fixes

- **Setup WiFi network invisible on some ESP32-C3 clones** (#61). Cheap "SuperMini" clone boards ship with a mis-tuned antenna and a weak regulator that cannot feed the radio at full transmit power, so the **PCMonitor-Setup** network was invisible even with the board in your hand, sometimes with a boot loop alongside it. Transmit power for the setup portal is now capped, which fixes it. If your board was fine before, nothing changes.
- **"Last OK" time on the error screen kept counting.** When LibreHardwareMonitor stopped responding, CPU/RAM/disk readings kept flowing and dragged the timestamp with them, so the screen showed the current time instead of the last healthy reading. It now freezes at the moment the data was last actually good.

## Companion app (v4)

- **Runs alongside its forks.** The app shared its settings folder, autostart entry, lock and web port with every companion forked from this codebase, so two of them could not run at once and whichever saved last owned the config - a save could even land in the wrong app. It now has its own identity throughout. Existing settings are **copied** into the new folder on first launch; the old folder is left untouched.
- **One save button.** The Connection page had its own save that did nothing for the fields you could see, while the always-visible bar saved everything except them. A single save now persists the connection first, then the layout, and says where the problem is instead of failing quietly.
- **A damaged config is no longer lost.** An unreadable settings file used to be silently replaced with defaults and overwritten on the next save; it is now set aside as a copy first.
- **Audio visualizer on Linux**, same as on Windows (needs `soundcard` and `numpy`).
- **New tray and executable icon** matching the device's web portal.
