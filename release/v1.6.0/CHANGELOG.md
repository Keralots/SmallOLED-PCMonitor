## Download Instructions

**Make sure you download the correct version for your screen size!**

| Use Case | File to Download |
|----------|------------------|
| **New device** (first time flashing) | `firmware-v1.6.0-OLED_0.96inch.bin`, `firmware-v1.6.0-OLED_1.3inch.bin` or `firmware-v1.6.0-OLED_1.54inch.bin` |
| **Existing device** (OTA update via web interface) | `OTA_ONLY_firmware-v1.6.0-OLED_0.96inch.bin`, `OTA_ONLY_firmware-v1.6.0-OLED_1.3inch.bin` or `OTA_ONLY_firmware-v1.6.0-OLED_1.54inch.bin` |

> 0.96" SSD1306 and 2.42" SSD1309 share the `0.96inch` image. 1.3" SH1106 and 1.54" CH1116 each have their own image - do not mix them up.

You can also flash directly from your browser (no tools to install): https://keralots.github.io/SmallOLED-PCMonitor/

> **Heads up for older devices:** the firmware has grown and is now close to the OTA size limit on devices still using the original partition layout. If an OTA update ever fails with a "size" or "no space" error, export your config from the Maintenance page, re-flash once with the browser flasher (this updates the storage layout automatically), then import your backup. After that, OTA updates work normally again.


# v1.6.0 - Changelog

## New Features

### Redesigned configuration web portal
- The on-device web portal has a completely new "paper docs" look in a master-detail layout: a left section navigation (Clock, Display, Display layout, Visible metrics, Network, Timezone, Maintenance), content cards, and a sticky save bar.
- Top-right accent picker (green/amber) and a light/dark toggle, both remembered per browser. An inline head script applies your theme before the page paints, so there is no flash of the wrong colours.
- The portal's CSS and JavaScript are now served as their own cached routes (`/portal.css`, `/portal.js`) instead of being inlined, so the main page is smaller and reloads are faster.

### Visible metrics - live 1:1 OLED preview with chip-tray placement
- The Visible metrics page now shows a pixel-exact 1:1 render of your OLED that updates live from the device.
- A compact chip tray sits right under the preview, one chip per metric showing its name and current slot. Place a metric by dragging its chip onto a slot, or tap the chip then tap a slot (works on touch, where drag-and-drop does not).
- Remove a metric by clicking it on the preview (or dragging its chip off the screen); this also clears its progress bar so nothing is left behind. Cancel a selection with Esc, by tapping empty tray space, or by tapping the chip again.
- Per-metric details (custom label, pairing, progress bar) live in a collapsible card whose heading reflects your custom label live.

### Two new animated clocks
- **Asteroids (style 10):** a wireframe vector clock - the ship drifts with inertia and splits tumbling rocks while idle, then aims at and shoots each changed digit into spinning line shards at the minute change.
- **Dino Runner (style 11):** a Chrome T-Rex homage - the dino runs and auto-jumps cacti over a scrolling ground with parallax clouds; at the minute change a pterodactyl swoops in, snatches the old digit and the new one drops in from above.

### Mobile-friendly portal
- On narrow screens the sidebar collapses into a hamburger menu.
- Factory reset moved into a dedicated **Maintenance** section alongside OTA update and configuration backup.

### OTA partition-limit warning
- Devices still on the original 4 MB partition layout now show a clear warning on the Maintenance page when the firmware is close to the OTA size limit, with step-by-step recovery guidance (export config, re-flash once via the browser flasher to repartition, import config). Devices already on the larger layout don't see the warning.

## Companion App (beta)

- The PC companion app folder was renamed to `PC-Companion-App-v3-beta`.
- Its layout engine and layout editor were split into separate modules with unit tests.
- The Customize window gained a pixel-exact 1:1 device preview and live push to the device.
- Customize editor rework: layout backup/restore, name-safe push, and a fix for live values not updating in the preview.

## Internal / Docs Changes

- The web portal markup remains a chunk-streamed PROGMEM document with `%TOKEN%` substitution; only the static CSS/JS were split out to cached routes, keeping peak heap low on the ESP32-C3.
- Per-clock-style settings panels are shown/hidden generically off the `clockStyle` dropdown.
- Asset URLs are cache-busted with a version query so browsers always fetch the matching CSS/JS after an update.

Related commits: `430775e` (portal redesign), `ded6168` (mobile nav + Maintenance + cache-busting), `8795e34` (1:1 live preview + drag-and-drop), `ae0fcfb` (OTA partition warning), `9727e5a` (chip-tray drag/tap placement), `8985004` (Asteroids clock), `fef5369` (Dino Runner clock), `c452f7c` / `6baac3e` / `5403419` / `48f5b02` (companion app v3 beta rework), `c7cf0a2` (version bump).
