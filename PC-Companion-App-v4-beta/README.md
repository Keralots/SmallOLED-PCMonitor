# PC Companion App v4 (beta)

The v4 desktop companion for SmallOLED-PCMonitor. It brings the device's own
config portal to the PC: a web-style window (1:1 OLED preview, drag-and-drop
layout, sensor picker, number formats, quick templates, pull/push, backup),
hosted in a native window + system tray, sending sensor readings to the device
over UDP.

## Layout

| Folder | What it is |
|--------|------------|
| [`win-companion/`](win-companion/) | **Windows** app (core + PyInstaller build). Ships a prebuilt `dist/pc_stats_monitor_v4.exe`. |
| [`linux-companion/`](linux-companion/) | **Linux** app - run from source (`python3 pc_stats_monitor_v4_linux.py`), no build step. |
| [`companion-common/`](companion-common/) | Shared, OS-neutral code used by both: the localhost web server, the pywebview window host, the `webui/` (HTML/CSS/JS extracted from the firmware portal), the layout engine and the device renderer. |

The two platform folders contain only the OS-specific core (sensor discovery,
autostart, packaging); everything else is shared from `companion-common/`.

## Quick start
- **Windows:** double-click `win-companion/dist/pc_stats_monitor_v4.exe` (needs the
  WebView2 runtime - preinstalled on Win11). See [`win-companion/README.md`](win-companion/README.md).
- **Linux:** `cd linux-companion && pip install -r requirements.txt && python3 pc_stats_monitor_v4_linux.py`.
  See [`linux-companion/README.md`](linux-companion/README.md).

The UDP wire-protocol version stays `2.2` (the contract the firmware speaks); the
product/config-file version is `4.0`.
