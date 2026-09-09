# PC Companion App v4 - Linux

The Linux sibling of the Windows v4 companion. Same web-based config UI (1:1 with
the device portal: live OLED preview, drag-and-drop layout, sensor picker), but
with Linux sensor discovery and **no build step** - run it straight from Python.

It shares all the OS-neutral code with the Windows app via `../companion-common`
(web server, window host, web UI, layout engine). The Linux-specific sensor
discovery + value reading is vendored as `linux_sensors.py` (the project's proven
v2 Linux backend: psutil + lm-sensors + NVIDIA pynvml + AMD sysfs hwmon).

## Run

```bash
python3 -m pip install -r requirements.txt
python3 pc_stats_monitor_v4_linux.py
```

- A **native window** (via pywebview) needs the GTK/WebKit system libs. On
  Debian/Ubuntu: `sudo apt install gir1.2-webkit2-4.1 python3-gi`. Without them
  the app automatically opens the UI in your **default browser** at
  `http://127.0.0.1:8737` - everything still works.
- For temperatures/fans: `sudo apt install lm-sensors && sudo sensors-detect`.
  For NVIDIA GPUs: `pip install pynvml`. AMD GPUs are read from sysfs (no extra
  package).

```bash
python3 pc_stats_monitor_v4_linux.py --minimized          # start hidden in the tray
python3 pc_stats_monitor_v4_linux.py --autostart enable   # systemd --user service
```

## Autostart
`--autostart enable` writes and enables a **systemd user service**
(`~/.config/systemd/user/smalloled-companion.service`) that launches the app minimized
at login (after a 10s delay). Disable with `--autostart disable`, or toggle
"Start with the system" in the Connection page of the UI.

## Where settings live
`~/.config/smalloled-companion/monitor_config.json` (honours `$XDG_CONFIG_HOME`).
Set `SMALLOLED_CONFIG_DIR` to override the folder entirely - that is how you run
two copies against two different devices.

### Upgrading from a version before this folder existed
Older builds used `~/.config/PCStatsMonitor/`, a folder every app forked from
this codebase also claimed. On first launch your configuration is **copied** into
the new folder; the old one is left in place, because another application may
still be reading it. A `MIGRATED-FROM.txt` note records where it came from.

The systemd unit is renamed from `pcstatsmonitor.service` to
`smalloled-companion.service`, but only when the old unit's `ExecStart` actually
references this script.

## Audio visualizer
Streams a 32-band FFT of whatever the PC is playing to the device, same as on
Windows. It needs two optional packages:

```
python3 -m pip install soundcard numpy
```

`soundcard` records the *monitor* source of the current default output, which
PulseAudio and PipeWire both provide, so nothing has to be routed by hand. The
output device is re-resolved on every reconnect, so switching to headphones is
picked up without a restart.

Enable it in the **Audio** page of the UI. *Auto* starts the visualizer when
sound is playing and releases the device back to its clock after a configurable
quiet period. Without the packages the page says so and stays disabled.

## Notes
- The system tray needs an AppIndicator-capable desktop; on GNOME it may require
  the AppIndicator extension. If the tray is unavailable, use the window/browser.
- The UDP wire-protocol version stays `2.2` (the contract the firmware speaks);
  the config-file/product version is `4.0`. The device IP, layout, labels, bars,
  companions, number formats and clock options work exactly as on Windows.
