# PC Stats Monitor - ESP32 OLED Display

[![Youtube Video](https://github.com/user-attachments/assets/d5232fde-a2ae-46ae-b4b4-74f2164f222e)](https://www.youtube.com/watch?v=D6zZIyOvWjY)

3D Printable case:
https://makerworld.com/en/models/2051935

Be aware that there are different sizes of the same SSD1306 OLED screen on aliexpress.
Im using this one "Original" version (do not purchase dual color, it won't work): 
https://aliexpress.com/item/1005006262908701.html

ESP32-C3 SuperMini:
https://aliexpress.com/item/1005008988143743.html

I have noticed that some of the cheap ESP32-C3 boards have WiFi related issues on hardware level. After some additional implementations to the code in attempt to fix issues (v1.2.1), some of those issues cannot be fixed as they are on hardware level.
What has helped a bit: Resoldering anthenna to improve connectivity with board, a small capacitor like 220uF or even 10uF between 3.3v and GND pins.
Some of those faluty boards have anthenna too close to other components. Like in this example (left side with 3.5mm gap is better)

<img width="791" height="200" alt="image" src="https://github.com/user-attachments/assets/0a624a91-db2b-45b0-b637-f3556f7561fb" />


A real-time PC monitoring system that displays CPU, RAM, GPU, and disk stats on a small OLED screen using ESP32 and a companion Python script.

## Features

- **Dual Display Modes:**
  - **PC Online**: Real-time stats (CPU, RAM, GPU temp, disk usage, fan/pump speed)
  - **PC Offline**: Animated clock (Mario, Space Invaders, Standard, or Large styles)
- **Web Configuration Portal**: Customize all settings via browser
  - Clock styles and time/date formats
  - Timezone and daylight saving
  - **Customizable display labels** (rename "PUMP" to "FAN", "COOLER", etc.)
- **WiFi Portal**: Easy first-time setup without code changes
- **Optimized Performance**: Minimal CPU usage on PC (<1%)
- **Persistent Settings**: All preferences saved to ESP32 flash memory

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

**Download the latest release**: [v1.2.1](release/v1.2.1/)

**Easiest Method - Web Flasher (No Installation Required!):**
1. Visit [ESP Web Flasher](https://espressif.github.io/esptool-js/)
2. Connect your ESP32-C3 via USB. If it constantly connects/disconnects, hold the **BOOT** button, connect to USB while still holding it, then release after connecting. Alternatively, hold **BOOT**, press **RESET** while holding **BOOT**, then release both buttons.
3. Click **"Connect"** and select your port
4. Click **"Choose File"** and select `firmware-v1.2.1.bin`
5. Set **Flash Address** to `0x0`
6. Click **"Program"** and wait ~30 seconds
7. Done! 🎉

**Alternative Methods:**
- **Windows**: Run `flash.bat` and follow prompts
- **Linux/Mac**: Run `./flash.sh` and follow prompts
- **Manual**: `esptool.py --chip esp32c3 --port COM3 --baud 460800 write_flash 0x0 firmware-complete.bin`

For detailed instructions, see [release/v1.1.0/FLASH_INSTRUCTIONS.md](release/v1.1.0/FLASH_INSTRUCTIONS.md)

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

#### Web Configuration Portal
Once connected to WiFi, access the full configuration page:
1. Open a browser and navigate to the ESP32's IP address (shown on OLED)
2. **Clock Settings:**
   - Idle clock style (Mario animation, Standard, or Large)
   - Time format (12/24 hour)
   - Date format (DD/MM/YYYY, MM/DD/YYYY, or YYYY-MM-DD)
3. **Timezone:**
   - GMT offset (-12 to +14 hours)
   - Daylight saving time toggle
4. **Display Labels (NEW!):**
   - Customize labels shown on OLED when PC is online
   - Fan/Pump label (e.g., "PUMP", "FAN", "COOLER")
   - CPU, RAM, GPU, and Disk labels
   - Perfect for personalizing your setup!

### 2. PC Stats Sender (Python)

#### Prerequisites
- **Python 3.7+**
- **LibreHardwareMonitor** (for hardware sensor monitoring)

#### Installing Python:
For windows download [e.g. this version](https://www.python.org/ftp/python/3.14.2/python-3.14.2-amd64.exe)
Check both checkboxes on installation screen (to use admin rights and add python.exe to PATH)

<img width="654" height="414" alt="Screenshot 2025-12-07 132252" src="https://github.com/user-attachments/assets/07d958e6-9776-4981-8971-c11d7d23d59a" />

At the end of installation, if asked to remove characters limit for path, agree on it.

#### Installing LibreHardwareMonitor
1. Download from [LibreHardwareMonitor Releases](https://github.com/LibreHardwareMonitor/LibreHardwareMonitor/releases)
2. Extract and run `LibreHardwareMonitor.exe` as Administrator
3. Windows defender may block if from running. This is false/positive, just add it to exception.
4. Check following options. Fisr 4 from the top:
<img width="442" height="627" alt="Screenshot 2025-12-07 132802" src="https://github.com/user-attachments/assets/983e41c4-9854-4d59-8ccc-4779315444d0" />



#### Python Script Setup
1. open CMD or powershell/terminal as admin.
2. Install required Python packages:
   ```bash
   pip install psutil pywin32
   pip install wmi pywin32
   ```
   For linux:
   ```bash
   pip install psutil
   ```
3. Configure the script:
   - Open [pc_stats_monitor.py](pc_stats_monitor.py) for windows systems or [pc_stats_monitor_linux.py](pc_stats_monitor_linux.py) for linux systems
   - Change the `ESP32_IP` to match your ESP32's IP address (displayed on OLED):
     ```python
     ESP32_IP = "192.168.0.163"  # Change this to your ESP32 IP
     ```

4. Run the script:
   ```bash
   python pc_stats_monitor.py
   ```

#### Running at Startup (Windows - Optional)
To automatically start monitoring when Windows boots:
1. Copy `autostart-monitor.bat` file exacly to: C:\script\autostart-monitor.bat
2. Copy `pc_stats_monitor.py` file exacly to C:\script\pc_stats_monitor.py
3. Press `Win + R`, type `shell:startup`, press Enter
4. Create a shortcut to `autostart-monitor.bat` (drag and drop should create a shortcut as well)
   
6. Or use Task Scheduler for running as administrator

## Usage

### Normal Operation
1. **ESP32** should be powered and connected to WiFi
2. **LibreHardwareMonitor** must be running on your PC
3. **Python script** should be running

The OLED will display:
- **PC Online**: Real-time stats (CPU, RAM, GPU temp, disk, fan speed)
- **PC Offline**: Animated clock (Mario jumps to update the time at each minute change)

### Display Modes

**When PC is Online (receiving stats):**
- Real-time monitoring display with customizable labels
- Shows CPU usage/temp, RAM usage, GPU temp, Disk usage, Fan/Pump speed
- Progress bars for visual representation
- Automatically switches when PC sends data

**When PC is Offline (idle mode):**
- **Mario Clock**: Animated pixel Mario that jumps to "hit" digits when time changes
- **Standard Clock**: Simple centered clock with date and day of week
- **Large Clock**: Extra-large time display with date

Change clock style anytime via the web configuration portal!

### Customizing Display Labels

**Via Web Portal (Recommended):**
1. Open ESP32's IP address in browser
2. Go to "Display Labels" section
3. Change labels to match your setup:
   - "PUMP" → "FAN" or "COOLER"
   - Customize CPU, RAM, GPU, Disk labels too
4. Save settings - changes apply immediately!

### Customizing Monitored Sensors

The Python script monitors these sensors (customize in [pc_stats_monitor.py](pc_stats_monitor.py)):
- **CPU Usage**: Overall percentage
- **CPU Temperature**: CPU Package sensor *(change sensor name in script)*
- **RAM Usage**: Percentage and GB used/total
- **GPU Temperature**: GPU Core sensor *(change sensor name in script)*
- **Disk Usage**: C: drive percentage
- **Fan/Pump Speed**: Pump Fan RPM *(change sensor name in script)*

To monitor different sensors, edit the sensor names in [pc_stats_monitor.py](pc_stats_monitor.py):
```python
# Lines 43-56 - Customize these for your system:
if sensor.SensorType == 'Fan' and sensor.Name == 'Pump Fan':
    # Change 'Pump Fan' to match your fan name
    # Examples: 'Fan #1', 'CPU Fan', 'Case Fan #2'

elif sensor.SensorType == 'Temperature' and sensor.Name == 'CPU Package':
    # Change 'CPU Package' to your CPU temp sensor
    # Examples: 'CPU Core #1', 'CPU (Tctl/Tdie)'

elif sensor.SensorType == 'Temperature' and sensor.Name == 'GPU Core':
    # Change 'GPU Core' to your GPU temp sensor
    # Examples: 'GPU Temperature', 'GPU Hot Spot'
```

**Tip:** Open LibreHardwareMonitor to see all available sensor names for your system!

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
