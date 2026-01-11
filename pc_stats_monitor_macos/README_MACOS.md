# PC Stats Monitor v2.0 - macOS Edition

macOS system monitoring script that sends PC stats to an ESP32 OLED display via UDP.

## Features

- **System Metrics**: CPU usage, RAM usage, RAM used (GB), Disk usage
- **Hardware Sensors**: CPU temperature, Fan speeds (hardware-dependent)
- **Network Stats**: Upload/download data totals and speeds (KB/s)
- **GUI Configuration**: Tkinter-based interface for metric selection
- **Menu Bar App**: Background operation with rumps menu bar integration
- **Autostart**: LaunchAgent support for automatic startup on login
- **UDP Protocol v2.0**: Compatible with ESP32 firmware

## Hardware Compatibility

### Intel Macs
- **CPU Temperature**: Available via `ioreg`
- **Fan Speeds**: Available via `ioreg`
- **More sensors** exposed by the hardware

### Apple Silicon (M1/M2/M3)
- **Limited Temperature Sensors**: SOC, battery temps only
- **Few/No Fan Sensors**: Most models are fanless
- **Focus on**: CPU%, RAM%, Network throughput

> **Note**: Sensor detection is automatic - the script will discover available sensors at runtime.

## Installation

### Prerequisites
- macOS 10.15 (Catalina) or later
- Python 3.8 or later (included with macOS)
- ESP32 with OLED display on the same network

### Install Dependencies

```bash
# Using pip3
pip3 install psutil rumps

# Or using python3 -m pip
python3 -m pip install psutil rumps
```

### Download the Script

```bash
# Clone or download the repository
cd SmallOLED-PCMonitor

# Or download just the macOS script
curl -O https://raw.githubusercontent.com/your-repo/pc_stats_monitor_macos.py
```

## Usage

### First Run

```bash
python3 pc_stats_monitor_macos.py
```

This will:
1. Discover all available sensors on your Mac
2. Open the GUI configuration window
3. Allow you to select metrics to monitor
4. Save configuration to `monitor_config_macos.json`

### Reconfigure

```bash
python3 pc_stats_monitor_macos.py --configure
```

### Run in Menu Bar

```bash
python3 pc_stats_monitor_macos.py --minimized
```

This runs the monitor in the background with a menu bar icon for control.

### Enable Autostart

```bash
# Enable LaunchAgent (runs on login)
python3 pc_stats_monitor_macos.py --autostart enable

# Disable autostart
python3 pc_stats_monitor_macos.py --autostart disable
```

## Configuration

### GUI Settings

- **ESP32 IP**: IP address of your ESP32 device (default: 192.168.0.163)
- **UDP Port**: UDP port for communication (default: 4210)
- **Update Interval**: How often to send metrics in seconds (default: 3)

### Custom Labels

Each metric can have a custom label (max 10 characters) that will be displayed on the OLED:
- Use `^` character for spacing on the display
- Example: `CPU^^` will show "CPU" with spacing

## macOS Permissions

Depending on your macOS version and security settings, you may need to grant permissions:

### Full Disk Access

If temperature/fan sensors don't appear:
1. Open **System Settings** > **Privacy & Security**
2. Add **Terminal** or your Python interpreter to **Full Disk Access**
3. Restart the script

### Accessibility

For menu bar app features:
1. Open **System Settings** > **Privacy & Security**
2. Add **Terminal** or your Python interpreter to **Accessibility**

## Troubleshooting

### No sensors found

**Issue**: Only system metrics (CPU, RAM, Disk) appear

**Solution**:
1. Grant Full Disk Access (see above)
2. Check if your Mac exposes sensors via `ioreg`:
   ```bash
   ioreg -rn IOHWSensor
   ```
3. Apple Silicon Macs have limited sensors

### "rumps not available" warning

**Issue**: Menu bar mode falls back to console

**Solution**:
```bash
pip3 install rumps
```

### Cannot enable autostart

**Issue**: LaunchAgent setup fails

**Solution**:
1. Ensure `~/Library/LaunchAgents/` directory exists
2. Try manually loading the plist:
   ```bash
   launchctl load ~/Library/LaunchAgents/com.pctools.monitor.plist
   ```

### ESP32 not receiving data

**Issue**: Metrics not showing on display

**Checklist**:
- [ ] ESP32 is powered on
- [ ] ESP32 is on the same network as your Mac
- [ ] Correct ESP32 IP in configuration
- [ ] UDP port matches (default: 4210)
- [ ] No firewall blocking UDP traffic

## Command Line Options

```
usage: pc_stats_monitor_macos.py [-h] [--configure] [--edit]
                                 [--autostart {enable,disable}]
                                 [--minimized]

PC Stats Monitor v2.0 - macOS Edition

optional arguments:
  -h, --help            show this help message and exit
  --configure           Force configuration GUI
  --edit                Edit existing configuration
  --autostart {enable,disable}
                        Enable/disable autostart (LaunchAgent)
  --minimized           Run minimized to menu bar
```

## Configuration File

The script saves configuration to `monitor_config_macos.json`:

```json
{
  "version": "2.0",
  "esp32_ip": "192.168.0.163",
  "udp_port": 4210,
  "update_interval": 3,
  "metrics": [
    {
      "id": 1,
      "name": "CPU",
      "display_name": "CPU Usage",
      "source": "psutil",
      "type": "percent",
      "unit": "%",
      "custom_label": "",
      "current_value": 45,
      "psutil_method": "cpu_percent"
    }
  ]
}
```

## Available Metrics

| Category | Metrics | Source | Notes |
|----------|---------|--------|-------|
| System | CPU Usage % | psutil | Always available |
| System | RAM Usage % | psutil | Always available |
| System | RAM Used (GB) | psutil | Always available |
| System | Disk Usage % | psutil | Root partition `/` |
| Temperature | CPU Temperature | ioreg | Intel: available, M1/M2/M3: limited |
| Fan | Fan Speed (RPM) | ioreg | Intel: available, M1/M2/M3: fanless models have none |
| Data | Network Download (GB) | psutil | Total data downloaded |
| Data | Network Upload (GB) | psutil | Total data uploaded |
| Throughput | Download Speed (KB/s) | psutil | Current download rate |
| Throughput | Upload Speed (KB/s) | psutil | Current upload rate |

## UDP Protocol (v2.0)

The script sends JSON payloads via UDP:

```json
{
  "version": "2.0",
  "timestamp": "14:35",
  "metrics": [
    {"id": 1, "name": "CPU", "value": 45, "unit": "%"},
    {"id": 2, "name": "GPUT", "value": 65, "unit": "°C"}
  ]
}
```

## Dependencies

- **psutil** - Cross-platform system metrics
- **rumps** - macOS menu bar application framework
- **tkinter** - GUI (included with macOS Python)

## License

This script is part of the SmallOLED-PCMonitor project.

## Support

For issues or questions:
1. Check the troubleshooting section above
2. Verify your macOS version and Python version
3. Test sensor availability: `ioreg -rn IOHWSensor`
