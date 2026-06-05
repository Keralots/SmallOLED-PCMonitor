# PC Companion App v3 (Windows)

Sends your PC's sensor readings to a SmallOLED-PCMonitor device over your local
network (UDP). This folder is the **Windows companion**, packaged as a single
`pc_stats_monitor_v3.exe` so end users do not need Python installed.

It is derived from the project's original `pc_stats_monitor_v2.py` (at the repo
root, left untouched) but is frozen-build aware: the config path, logging, and
"run at login" all work correctly inside a PyInstaller `.exe`.

A prebuilt `dist\pc_stats_monitor_v3.exe` ships with this folder.

---

## For end users (using the .exe)

### 1. First run / configuration
1. Double-click `pc_stats_monitor_v3.exe`.
   - First time only, Windows SmartScreen may say *"Windows protected your PC"*.
     Click **More info -> Run anyway**. This is normal for unsigned tools.
2. The configuration window opens:
   - Set the **ESP32 IP** (your device's IP), **UDP Port** (default 4210), and
     **Update Interval**.
   - The banner at the top shows whether **LibreHardwareMonitor** is connected
     (see below). Tick the metrics you want, optionally give them short labels,
     then click **Save & Start Monitoring**.

### 2. Hardware sensors (LibreHardwareMonitor)
CPU/RAM/Disk usage work out of the box. For temperatures, fan RPM, GPU, power,
etc. you need [LibreHardwareMonitor](https://github.com/LibreHardwareMonitor/LibreHardwareMonitor/releases):

1. Install and run it **as Administrator**.
2. Enable the REST API: **Options -> Remote Web Server -> Run** (port 8085).
3. Back in the config window, click **↻ Rescan sensors**. The banner should turn
   green and the new sensors appear in the list.

The **Get LibreHardwareMonitor** and **REST API help** buttons in the app link
to the download and walk through these steps.

### 3. Run at Windows login (autostart)
In the config window, next to **Windows Autostart**, click **Enable**. This adds
a per-user registry entry (`HKCU\...\CurrentVersion\Run`) that launches the app
minimized to the system tray at login, after a 10s delay so LibreHardwareMonitor
can start first. No administrator rights required.

Right-click the tray icon any time to **Configure** or **Quit**.

For the most reliable boot, also turn on these in LibreHardwareMonitor's Options:
**Run On Windows Startup**, **Start Minimized / Minimize To Tray**, and
**Remote Web Server**.

### Where settings and logs live
For the `.exe`, both live in `%APPDATA%\PCStatsMonitor\`:
- `monitor_config.json` - your saved configuration
- `monitor.log` - runtime output (useful for debugging autostart)

---

## Building the .exe from source

Requires Python 3.10+ on Windows.

```bat
python -m pip install -r requirements.txt
python -m pip install pyinstaller
build_exe.bat
```

Output: `dist\pc_stats_monitor_v3.exe` (a single self-contained file).

`build_exe.bat` builds windowed (no console) with the hidden-imports PyInstaller
needs for `wmi`, `pystray`, `pillow`, and `pywin32`.

### Running as a plain script (development)
The same file still runs as a normal script. In script mode, config/log are
written next to the `.py` instead of `%APPDATA%`:

```bat
python pc_stats_monitor_v3.py               # config GUI / console monitor
python pc_stats_monitor_v3.py --edit        # edit existing config
python pc_stats_monitor_v3.py --minimized   # tray mode
python pc_stats_monitor_v3.py --autostart enable   # register run-at-login
```

---

## Versioning notes
- Product / UI / config-file version: **3.0**.
- The UDP wire-protocol `version` field stays **2.2** on purpose: it's the
  contract the ESP32 firmware speaks, not the app's product version (the firmware
  doesn't read it today, but bumping it would be semantically wrong).

---

## Troubleshooting

- **"Windows protected your PC":** click **More info -> Run anyway**. The exe is
  unsigned (code-signing certificates cost money); this is expected.
- **Antivirus flags the exe:** PyInstaller one-file exes are a common false
  positive. Add an exception, or build it yourself from source.
- **No hardware sensors / banner is red or yellow:** LibreHardwareMonitor is not
  running, or its Remote Web Server is off. Start it as Administrator, enable
  **Options -> Remote Web Server -> Run**, then click **Rescan sensors**.
- **Device shows nothing:** confirm the ESP32 IP is correct and both devices are
  on the same network/subnet; check Windows Firewall isn't blocking outbound UDP.
- **Autostart didn't trigger:** check the value exists at
  `HKCU\Software\Microsoft\Windows\CurrentVersion\Run\PCStatsMonitor`, and read
  `%APPDATA%\PCStatsMonitor\monitor.log` for the startup output.
