# PC Companion App v4 (Windows) - BETA

Sends your PC's sensor readings to a SmallOLED-PCMonitor device over your local
network (UDP), and gives you a **web-based configuration window** that mirrors the
device's own config portal 1:1 - including a live pixel-accurate OLED preview and
a drag-and-drop layout editor.

v4 is a single always-running app: the config UI is a native window (Microsoft
Edge **WebView2**, via [pywebview](https://pywebview.flowrl.com/)) backed by a
tiny `127.0.0.1` web server. Closing the window hides it to the system tray; the
monitor keeps sending in the background. It is derived from v3 (left untouched in
`../PC-Companion-App-v3-beta/`) and reuses the same sensor discovery, UDP
protocol, autostart and single-instance machinery.

---

## For end users (using the .exe)

1. Double-click `pc_stats_monitor_v4.exe`.
   - First time only, Windows SmartScreen may say *"Windows protected your PC"*.
     Click **More info -> Run anyway**. This is normal for unsigned tools.
   - Requires the **WebView2 runtime** (preinstalled on Windows 11; on Windows 10
     get Microsoft's free *Evergreen* runtime if the window fails to open).
2. The configuration window opens with four sections:
   - **Connection** - device IP, UDP port, update interval, *Test connection*, and
     *Start with Windows* (autostart).
   - **Sensors** - tick which readings to send (up to 20). CPU / RAM / Disk work out
     of the box; temps, fans, GPU and power need **LibreHardwareMonitor** (see below).
     Use *Rescan sensors* after starting it.
   - **Layout & preview** - drag your chosen metrics onto the 1:1 OLED preview, pair
     companions, add progress bars, choose row mode. **Save & push to device** writes
     the config and pushes the layout to the device.
   - **Backup** - export / import the whole configuration as JSON.
3. Close the window to tuck it into the tray. Right-click the tray icon any time to
   **Configure** (reopen the window) or **Quit**.

### Hardware sensors (LibreHardwareMonitor)
For temperatures, fan RPM, GPU, power, etc. install
[LibreHardwareMonitor](https://github.com/LibreHardwareMonitor/LibreHardwareMonitor/releases):

1. Install and run it **as Administrator**.
2. Enable the REST API: **Options -> Remote Web Server -> Run** (port 8085).
3. In the app's **Sensors** page, click **Rescan sensors**. The banner turns green
   and the new sensors appear.

### Autostart
On the **Connection** page, enable **Start with Windows**. This adds a per-user
registry entry (`HKCU\...\CurrentVersion\Run`) that launches the app minimized to
the tray at login, after a 10s delay so LibreHardwareMonitor can start first. No
administrator rights required.

### Where settings and logs live
For the `.exe`, both live in `%APPDATA%\PCStatsMonitor\`:
- `monitor_config.json` - your saved configuration
- `monitor.log` - runtime output (useful for debugging autostart)

---

## Running from source (development)

Requires Python 3.10+ on Windows.

```bat
python -m pip install -r requirements.txt
python pc_stats_monitor_v4.py               # opens the config window + tray
python pc_stats_monitor_v4.py --minimized   # start hidden in the tray (autostart)
python pc_stats_monitor_v4.py --autostart enable   # register run-at-login
```

In script mode, config/log are written next to the `.py` instead of `%APPDATA%`.
If `pywebview` is not installed, the app still serves the UI and opens it in your
default browser at `http://127.0.0.1:8736/`.

### Building the .exe

```bat
python -m pip install -r requirements.txt
python -m pip install pyinstaller
build_exe.bat
```

Output: `dist\pc_stats_monitor_v4.exe` (single self-contained file). The build
bundles the `webui/` folder and the pywebview WebView2 backend.

### How the web UI is built
`webui/portal.css` and `webui/portal.js` are extracted from the firmware's
`src/web/web_pages.h` so the desktop UI stays 1:1 with the device portal;
`portal.js` is then adapted for the PC (sensor selection, connection, autostart;
the ESP-only OTA / factory-reset blocks removed). `webui/index.html` is the
PC-tailored page. The Python side (`server.py`) implements the same REST endpoints
the JS calls; `app_window.py` hosts it all in the pywebview window + tray.

---

## Architecture (one process, several threads)
- **main thread:** pywebview window (required on Windows).
- **http-server thread(s):** `server.py` on `127.0.0.1` - serves `webui/` and the
  REST API (`/metrics`, `/save`, `/api/sensors`, `/api/select`, `/api/connection`,
  `/api/test`, `/api/autostart`, `/api/export`, `/api/import`, `/api/status`).
- **monitor thread:** reads the live config from the shared state and sends UDP
  every interval.
- **tray thread:** pystray icon (Configure / Quit).
- All shared state goes through an `RLock`-guarded manager (`app_state.py`). Window
  show/hide/quit calls are funneled through the `gui_*` chokepoint.

The UDP wire-protocol `version` stays **2.2** (the contract the firmware speaks);
the config-file/product version is **4.0**.

---

## Troubleshooting
- **Window doesn't open / blank window:** install the WebView2 runtime (Win10), or
  run from source - the app falls back to opening the UI in your browser.
- **"Windows protected your PC":** click **More info -> Run anyway** (unsigned exe).
- **No hardware sensors:** start LibreHardwareMonitor as Administrator, enable
  **Options -> Remote Web Server -> Run**, then **Rescan sensors**.
- **Device shows nothing:** confirm the device IP is correct, both devices share the
  network/subnet, and Windows Firewall isn't blocking outbound UDP. Use *Test
  connection*.
- **Autostart didn't trigger:** check `HKCU\Software\Microsoft\Windows\CurrentVersion\Run\PCStatsMonitor`
  and read `%APPDATA%\PCStatsMonitor\monitor.log`.
