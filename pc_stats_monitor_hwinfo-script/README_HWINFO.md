# PC Stats Monitor - HWiNFO Edition v2.0

A Python script that reads hardware sensors from **HWiNFO64** or **LibreHardwareMonitor** and sends them to an ESP32 OLED display via UDP.

## Auto-Detection Feature

This script intelligently selects the best sensor source:
1. **Tries HWiNFO64 first** (preferred - more sensors, better performance)
2. **Falls back to LibreHardwareMonitor** if HWiNFO is not running
3. **Zero configuration needed** - just run it!

## What's New in HWiNFO Edition

This is an alternative to the LibreHardwareMonitor version with these advantages:

### Additional Sensors
- **Voltage Rails** - Monitor VCORE, 12V, 5V, 3.3V, VSOC, and more
- **Current Sensors** - Track amperage draw (CPU current, GPU current, etc.)
- **FPS Counters** - Display game framerates in real-time!
- **2-3× more sensors** than LibreHardwareMonitor

### Better Performance
- Direct memory access (no WMI polling delay)
- Lower CPU overhead
- Faster updates

### More Sensor Details
- Min/Max/Avg values available
- Better sensor labeling
- More device context

## Requirements

### Software
- **Python 3.8+** - Download from [python.org](https://www.python.org/)
- **HWiNFO64** (recommended) - Download FREE from [hwinfo.com](https://www.hwinfo.com/)
  - OR **LibreHardwareMonitor** - Download from [github.com/LibreHardwareMonitor](https://github.com/LibreHardwareMonitor/LibreHardwareMonitor)
- **ESP32** with OLED display (running SmallOLED-PCMonitor firmware)

**Note:** You only need ONE sensor source (HWiNFO OR LibreHardwareMonitor). The script auto-detects which is running.

### Python Packages
Install dependencies with:
```bash
pip install -r requirements_hwinfo.txt
```

Or manually:
```bash
pip install psutil pywin32 wmi pystray pillow
```

## Setup Instructions

### Option A: Using HWiNFO64 (Recommended - More Sensors)

#### 1. Install HWiNFO64
1. Download HWiNFO64 from https://www.hwinfo.com/ (free version is fine)
2. Install and run HWiNFO64
3. Choose **"Sensors-only"** mode (optional but faster)

#### 2. Enable Shared Memory Support
**This is CRITICAL for HWiNFO - the script won't detect it without this step!**

1. Click the **Settings** (gear) icon in HWiNFO
2. Go to the **"Safety"** tab
3. Check ☑ **"Shared Memory Support"**
4. Click **OK**
5. **RESTART HWiNFO64** (required for changes to take effect)

### Option B: Using LibreHardwareMonitor (Fallback)

If HWiNFO is not available, the script will automatically use LibreHardwareMonitor:

1. Download LibreHardwareMonitor from https://github.com/LibreHardwareMonitor/LibreHardwareMonitor
2. Extract and run `LibreHardwareMonitor.exe`
3. **No additional configuration needed** - WMI interface is enabled by default

**Note:** LibreHardwareMonitor provides fewer sensors than HWiNFO but requires no 12-hour restart.

### Verify Setup
Run the script:
```bash
python pc_stats_monitor_hwinfo.py --configure
```

**If using HWiNFO64**, you should see:
```
[2a] Trying HWiNFO64...
[OK] HWiNFO shared memory opened
  Version: X.X
  Sensors: XX
  Readings: XXX
[OK] Using HWiNFO64 as sensor source
```

**If using LibreHardwareMonitor**, you should see:
```
[2a] Trying HWiNFO64...
[INFO] HWiNFO64 not available: Shared memory not found
[2b] Falling back to LibreHardwareMonitor (WMI)...
[OK] Using LibreHardwareMonitor as sensor source
```

**If both fail**, you'll see an error message. Make sure at least one sensor source is running.

## Usage

### First-Time Setup
```bash
python pc_stats_monitor_hwinfo.py --configure
```

This will:
1. Discover all available sensors (50-200+ depending on your hardware)
2. Open a GUI where you can select up to 20 metrics
3. Configure ESP32 IP address and update interval
4. Save your configuration

### Start Monitoring
After configuration, run:
```bash
python pc_stats_monitor_hwinfo.py
```

The script will:
- Read sensor values from HWiNFO shared memory
- Send data to your ESP32 via UDP (JSON V2.0 format)
- Display real-time metrics on the OLED screen

### System Tray Mode
Run minimized to system tray:
```bash
python pc_stats_monitor_hwinfo.py --minimized
```

Right-click the tray icon to:
- **Configure** - Change metrics selection
- **Quit** - Exit the application

### Edit Configuration
```bash
python pc_stats_monitor_hwinfo.py --edit
```

### Windows Autostart
Enable autostart on Windows login:
```bash
python pc_stats_monitor_hwinfo.py --autostart enable
```

Disable autostart:
```bash
python pc_stats_monitor_hwinfo.py --autostart disable
```

Or use the **GUI's autostart buttons** in the configuration window.

## Sensor Categories

The script organizes sensors into categories:

- **SYSTEM METRICS** - CPU%, RAM%, RAM GB, Disk% (from psutil)
- **TEMPERATURES** - CPU cores, GPU, motherboard, drives, VRM
- **VOLTAGES** - VCORE, 12V, 5V, 3.3V, VSOC, VDDCR *(NEW)*
- **FANS & COOLING** - CPU fan, case fans, pump speeds
- **CURRENTS** - CPU current, GPU current, amperage draw *(NEW)*
- **LOADS** - CPU load, GPU load, GPU memory, video engine
- **CLOCKS** - CPU clock, GPU clock, memory clock
- **POWER** - CPU power, GPU power, package power

Plus special sensors like **FPS counters** for gaming!

## FPS Counter Support

HWiNFO can expose game FPS (frames per second) from various rendering APIs:
- D3D11 FPS
- OpenGL FPS
- Vulkan FPS

These will appear in the sensor list and can be displayed on your ESP32 in real-time!

## Custom Labels

Each metric supports a custom label (max 10 characters) that will be displayed on the ESP32 instead of the auto-generated name.

Example:
- Auto-generated: `CPU_C0`
- Custom label: `Core #0`

## 12-Hour Limitation (Free Version)

**Important:** HWiNFO free version limits shared memory support to 12 hours.

### What Happens After 12 Hours?
- HWiNFO will show a popup reminding you
- The script will display: `"Shared memory not found"`
- You need to restart HWiNFO and re-enable shared memory

### Solutions:
1. **Restart HWiNFO every 12 hours** (HWiNFO reminds you)
2. **Use Windows Task Scheduler** to auto-restart HWiNFO overnight
3. **Purchase HWiNFO Pro** for unlimited shared memory access

**Note:** This is a HWiNFO limitation, not a bug in the script.

## Configuration File

Settings are saved to `monitor_config_hwinfo.json`:
```json
{
  "version": "2.0",
  "esp32_ip": "192.168.0.163",
  "udp_port": 4210,
  "update_interval": 3,
  "metrics": [...]
}
```

## ESP32 Compatibility

This script uses the **same JSON V2.0 protocol** as the LibreHardwareMonitor version.

**No ESP32 firmware changes required!**

Your ESP32 will automatically display:
- New voltage sensors
- New current sensors
- FPS counters
- All existing metrics

## Troubleshooting

### Script says "Falling back to LibreHardwareMonitor"
- **Not an error!** This means HWiNFO is not running, so the script is using LibreHardwareMonitor instead
- **To use HWiNFO:** Make sure HWiNFO64 is running with shared memory enabled
- **To use LibreHardwareMonitor:** Make sure LibreHardwareMonitor.exe is running

### "No sensor source configured" or "0 sensors"
- **Cause:** Neither HWiNFO nor LibreHardwareMonitor is running
- **Fix:** Start at least one sensor source (HWiNFO64 or LibreHardwareMonitor)

### "Shared memory not found" (HWiNFO-specific)
- **Cause:** Shared memory not enabled in HWiNFO
- **Fix:** Enable in Settings → Safety → Shared Memory Support, then restart HWiNFO
- **Alternative:** The script will automatically fall back to LibreHardwareMonitor if available

### "Invalid signature" (HWiNFO-specific)
- **Cause:** Using HWiNFO32 instead of HWiNFO64
- **Fix:** Download and install HWiNFO**64** (64-bit version)

### Shared memory expired after 12 hours (HWiNFO-specific)
- **Cause:** Free version time limit
- **Fix:** Restart HWiNFO, re-enable shared memory, restart script
- **Alternative:** Switch to LibreHardwareMonitor (no time limit)

### ESP32 not receiving data
- **Cause:** Wrong IP address or network issue
- **Fix:**
  1. Check ESP32 IP in HWiNFO GUI configuration
  2. Verify ESP32 is on same network
  3. Check firewall settings (allow UDP port 4210)

## Comparison: Sensor Sources

**This script supports BOTH sources with automatic detection!**

| Feature | When Using HWiNFO64 | When Using LibreHardwareMonitor |
|---------|---------------------|--------------------------------|
| Temperature sensors | ✅ | ✅ |
| Fan sensors | ✅ | ✅ |
| Load/Usage sensors | ✅ | ✅ |
| Clock sensors | ✅ | ✅ |
| Power sensors | ✅ | ✅ |
| Voltage sensors | ✅ | ✅ |
| **Current sensors** | ✅ **More** | ❌ |
| **FPS counters** | ✅ **More** | ❌ |
| **Network Data (GB)** | ❌ | ✅ |
| **Network Speed (KB/s)** | ❌ | ✅ |
| Total sensors | 50-200+ | 20-80 |
| CPU overhead | Low (direct memory) | Medium (WMI) |
| Update speed | Faster | Standard |
| Setup complexity | Medium (shared memory) | Easy (auto-enabled) |
| Free version limit | 12 hours | None |
| **Auto-detection** | ✅ **Tries first** | ✅ **Automatic fallback** |

**Recommendation:** Use HWiNFO64 for maximum sensor coverage, or LibreHardwareMonitor for zero-configuration ease.

## Advanced: Short Name Generation

The script automatically generates 10-character names for ESP32 display:

| Sensor | Auto-Generated Name | Logic |
|--------|-------------------|-------|
| Core 0 Temperature | `CPU_C0` | Device prefix + core number |
| GPU Temperature | `GPU_TEMP` | Device detection |
| VCore Voltage | `VCORE` | Common rail mapping |
| Fan #1 | `FAN1` | Fan number extraction |
| CPU Package Power | `CPU_PWR` | Power sensor naming |
| D3D11 Framerate | `GPU_FPS` | FPS detection |

You can override any name with a custom label!

## Files

- `pc_stats_monitor_hwinfo.py` - Main script (~1400 lines)
- `monitor_config_hwinfo.json` - Your configuration (auto-generated)
- `requirements_hwinfo.txt` - Python dependencies
- `README_HWINFO.md` - This file

## Support

- **Issues:** https://github.com/Keralots/SmallOLED-PCMonitor/issues
- **ESP32 Firmware:** See main README.md
- **HWiNFO Support:** https://www.hwinfo.com/forum/

## Version History

- **v2.0** - Initial HWiNFO edition release
  - Full sensor support (temperature, voltage, fan, current, load, clock, power)
  - FPS counter support
  - GUI configuration
  - System tray mode
  - Windows autostart
  - 100% ESP32 protocol compatible

## License

Same as SmallOLED-PCMonitor project.

---

**Tip:** Run both versions side-by-side! They use separate config files:
- LibreHardwareMonitor: `monitor_config.json`
- HWiNFO: `monitor_config_hwinfo.json`
