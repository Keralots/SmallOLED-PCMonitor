# PC Stats Monitor - ESP32 OLED Display
https://www.youtube.com/shorts/t2W5wxtOph8
A real-time PC monitoring system that displays CPU, RAM, GPU, and disk stats on a small OLED screen using ESP32 and a companion Python script.

## Features

- Real-time PC stats display (CPU, RAM, GPU temperature, disk usage, pump/fan speed)
- Animated Mario clock when PC is offline (or choose standard/large clock styles)
- WiFi configuration portal for easy setup
- Web-based settings interface
- Configurable timezone and date formats
- Optimized for minimal CPU usage on PC

## Hardware Requirements

### ESP32 Setup
- **ESP32-C3 Super Mini** (or compatible ESP32 board)
- **SSD1306 OLED Display** (128x64, I2C)
- **Wiring:**
  - SDA → GPIO 8
  - SCL → GPIO 9
  - VCC → 3.3V
  - GND → GND

## Software Setup

### 1. ESP32 Firmware

#### Option A: Pre-built Binary (Easy - No Compilation Needed)

**Download the latest release**: [v1.0.0](release/v1.0.0/)

Quick flash using the automated scripts:
- **Windows**: Run `flash.bat` and follow prompts
- **Linux/Mac**: Run `./flash.sh` and follow prompts

Or flash manually with esptool (single file - easiest!):
```bash
pip install esptool
esptool.py --chip esp32c3 --port COM3 --baud 460800 write_flash 0x0 firmware-complete.bin
```

For detailed instructions, see [release/v1.0.0/FLASH_INSTRUCTIONS.md](release/v1.0.0/FLASH_INSTRUCTIONS.md)

#### Option B: Build from Source

**Prerequisites:**
- [PlatformIO](https://platformio.org/) (or Arduino IDE)

**Installation:**
1. Clone this repository
2. Open the project in PlatformIO
3. Connect your ESP32 via USB
4. Build and upload:
   ```bash
   pio run --target upload
   ```

#### First-Time WiFi Setup
1. After uploading, the ESP32 will create a WiFi access point
2. Connect to the network: **PCMonitor-Setup**
3. Password: **monitor123**
4. Open your browser to `192.168.4.1`
5. Configure your WiFi credentials
6. The ESP32 will connect and display its IP address on the OLED

#### Web Configuration
Once connected to WiFi, access the configuration page:
1. Open a browser and navigate to the ESP32's IP address (shown on OLED)
2. Configure:
   - Clock style (Mario animation, Standard, or Large)
   - Timezone (GMT offset)
   - Daylight saving time
   - Time format (12/24 hour)
   - Date format (DD/MM/YYYY, MM/DD/YYYY, or YYYY-MM-DD)

### 2. PC Stats Sender (Python)

#### Prerequisites
- **Python 3.7+**
- **LibreHardwareMonitor** (for hardware sensor monitoring)

#### Installing LibreHardwareMonitor
1. Download from [LibreHardwareMonitor Releases](https://github.com/LibreHardwareMonitor/LibreHardwareMonitor/releases)
2. Extract and run `LibreHardwareMonitor.exe` as Administrator
3. Keep it running in the background (minimize to system tray)

#### Python Script Setup
1. Install required Python packages:
   ```bash
   pip install psutil pywin32
   ```

2. Configure the script:
   - Open [pc_stats_monitor.py](pc_stats_monitor.py)
   - Change the `ESP32_IP` to match your ESP32's IP address (displayed on OLED):
     ```python
     ESP32_IP = "192.168.0.163"  # Change this to your ESP32 IP
     ```

3. Run the script:
   ```bash
   python pc_stats_monitor.py
   ```

#### Running at Startup (Optional)
To automatically start monitoring when Windows boots:
1. Press `Win + R`, type `shell:startup`, press Enter
2. Create a shortcut to the Python script
3. Or use Task Scheduler for running as administrator

## Usage

### Normal Operation
1. **ESP32** should be powered and connected to WiFi
2. **LibreHardwareMonitor** must be running on your PC
3. **Python script** should be running

The OLED will display:
- **PC Online**: Real-time stats (CPU, RAM, GPU temp, disk, fan speed)
- **PC Offline**: Animated clock (Mario jumps to update the time at each minute change)

### Display Modes
- **Mario Clock**: Animated pixel Mario that jumps to "hit" digits when time changes
- **Standard Clock**: Simple centered clock with date and day
- **Large Clock**: Extra-large time display

### Sensor Configuration
The Python script monitors:
- **CPU Usage**: Overall percentage
- **CPU Temperature**: CPU Package sensor
- **RAM Usage**: Percentage and GB used/total
- **GPU Temperature**: GPU Core sensor
- **Disk Usage**: C: drive percentage
- **Fan Speed**: Pump Fan RPM (customize in script)

To monitor different sensors, modify the sensor names in [pc_stats_monitor.py](.claude/pc_stats_monitor.py):
```python
# Lines 35-44
if sensor.SensorType == 'Fan' and sensor.Name == 'Pump Fan':
    # Change 'Pump Fan' to your fan's name

elif sensor.SensorType == 'Temperature' and sensor.Name == 'CPU Package':
    # Change 'CPU Package' to your CPU sensor name

elif sensor.SensorType == 'Temperature' and sensor.Name == 'GPU Core':
    # Change 'GPU Core' to your GPU sensor name
```

## Troubleshooting

### ESP32 Issues

**Display not working**
- Check I2C wiring (SDA=GPIO8, SCL=GPIO9)
- Verify I2C address is 0x3C (common for SSD1306)

**Can't connect to WiFi portal**
- Make sure you're connected to "PCMonitor-Setup" network
- Try accessing `192.168.4.1` in your browser
- Reset WiFi settings via web interface

**ESP32 keeps restarting**
- Check power supply (use quality USB cable)
- Monitor serial output at 115200 baud for error messages

### Python Script Issues

**"WMI not found" or sensor errors**
- Make sure LibreHardwareMonitor is running as Administrator
- Check that WMI service is running: `services.msc` → Windows Management Instrumentation

**No data on display**
- Verify ESP32 IP address in script matches actual IP
- Check firewall isn't blocking UDP port 4210
- Ensure both devices are on the same network

**High CPU usage**
- Increase `BROADCAST_INTERVAL` in script (default: 3 seconds)
- Script is optimized to use minimal resources

**Sensors not found**
- Run the script once to see available sensors in console output
- Modify sensor names in `initialize_sensors()` function
- Check LibreHardwareMonitor GUI for correct sensor names

## Technical Details

### Communication
- **Protocol**: UDP
- **Port**: 4210
- **Format**: JSON
- **Update Rate**: 3 seconds (configurable)

### Libraries Used

**ESP32:**
- WiFiManager (tzapu)
- Adafruit SSD1306
- Adafruit GFX
- ArduinoJson

**Python:**
- psutil (system stats)
- pywin32/wmi (hardware sensors)

## License

This project is open source. Feel free to modify and share!

## Credits

Created for monitoring PC stats on a small OLED display. Mario animation inspired by classic pixel art.
