# PC Stats Monitor v2.0 - Usage Guide

## New Features

### 1. Configuration GUI
- **ESP32 IP Address**: Configure the IP address of your ESP32 device
- **UDP Port**: Change the UDP port (default: 4210)
- **Update Interval**: Set how often to send metrics (in seconds)
- **Custom Labels**: Set custom labels for each metric (max 10 characters)

### 2. Command Line Options

#### First Run (Initial Configuration)
```bash
python pc_stats_monitor_v2.py
```
This will open the GUI automatically if no configuration exists.

#### Edit Configuration
```bash
python pc_stats_monitor_v2.py --edit
```
Opens the GUI to edit your existing configuration (ESP IP, port, interval, metrics, labels).

#### Force Reconfiguration
```bash
python pc_stats_monitor_v2.py --configure
```
Opens the GUI for fresh configuration (doesn't load existing settings).

#### Run Minimized to System Tray
```bash
python pc_stats_monitor_v2.py --minimized
```
Runs the monitoring in the background with a system tray icon. Right-click the tray icon to:
- Configure settings
- Quit the application

**Note**: Requires additional packages (see below)

#### Enable Autostart on Windows
```bash
python pc_stats_monitor_v2.py --autostart enable
```
Creates a shortcut in Windows Startup folder. The script will run minimized to system tray on login.

#### Disable Autostart
```bash
python pc_stats_monitor_v2.py --autostart disable
```
Removes the startup shortcut.

### 3. Custom Labels

In the GUI, each metric now has a "Label:" field where you can enter a custom display name.
- Max 10 characters
- Will be sent to ESP32 instead of auto-generated name
- Use `^` character for spacing (e.g., `CPU^^` displays as `CPU:  45C`)
- Leave empty to use auto-generated name

**Examples**:
- CPU Usage → Custom label: `CPU%` or `CPU^^` (with spacing)
- CPU Package Temperature → Custom label: `CPU_TEMP`
- GPU Core Temperature → Custom label: `GPU`

### 4. System Tray Mode (Optional)

For running minimized to system tray, install these additional packages:
```bash
pip install pystray pillow
```

If these packages are not installed, the script will run in console mode instead.

### 5. Autostart Setup (Windows)

To enable autostart, you need `pywin32`:
```bash
pip install pywin32
```

Then enable autostart:
```bash
python pc_stats_monitor_v2.py --autostart enable
```

The script will automatically run minimized to system tray when Windows starts.

## Typical Workflow

### First Time Setup
1. Run the script: `python pc_stats_monitor_v2.py`
2. Configure ESP32 IP address and port
3. Select metrics to monitor (up to 12)
4. Set custom labels if desired
5. Click "Save & Start Monitoring"

### Enable Autostart
```bash
python pc_stats_monitor_v2.py --autostart enable
```

### Edit Configuration Later
```bash
python pc_stats_monitor_v2.py --edit
```

### Run Manually
```bash
# Console mode (see output)
python pc_stats_monitor_v2.py

# Background mode (system tray)
python pc_stats_monitor_v2.py --minimized
```

## Configuration File

All settings are saved to `monitor_config.json` in the same directory as the script. This includes:
- ESP32 IP address
- UDP port
- Update interval
- Selected metrics
- Custom labels

## Troubleshooting

### "No metrics received yet" on ESP32 web interface
- Check that Python script is running
- Verify ESP32 IP address is correct
- Ensure both devices are on the same network
- Check firewall settings

### Custom labels not appearing
- Custom labels must be set in Python GUI, not ESP32 web interface
- The Python script sends the label name to ESP32
- Use `--edit` to modify labels

### Autostart not working
- Make sure `pywin32` is installed
- Check Windows Startup folder for "PC Monitor.lnk"
- Verify the shortcut points to correct Python path
- System tray packages (pystray, pillow) must be installed for minimized mode

### System tray icon not appearing
- Install required packages: `pip install pystray pillow`
- Run with `--minimized` flag
- Check system tray settings in Windows

## Required Packages

**Core functionality**:
```bash
pip install psutil pywin32 wmi
```

**System tray support** (optional):
```bash
pip install pystray pillow
```

**Autostart support** (Windows):
```bash
pip install pywin32
```

All packages together:
```bash
pip install psutil pywin32 wmi pystray pillow
```
