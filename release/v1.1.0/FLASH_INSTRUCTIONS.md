# PC Stats Monitor - Firmware v1.1.0

## Pre-built Firmware for ESP32-C3

This release contains pre-compiled binary files ready to flash to your ESP32-C3 board.

## What's Included

- `firmware-complete.bin` - Complete firmware (1 MB) - **Flash this single file!**
- `flash.bat` / `flash.sh` - Automated flash scripts
- `FLASH_INSTRUCTIONS.md` - This file

## Flashing Instructions

### Method 1: Web-Based Flasher (Easiest - No Installation Required!)

**This is the recommended method for most users!**

Visit **[ESP Web Flasher](https://espressif.github.io/esptool-js/)** and follow these steps:

1. **Connect ESP32-C3** to your computer via USB
2. Click **"Connect"** button
3. Select your ESP32 port from the popup (usually shows as "USB Serial" or similar)
4. Click **"Choose File"** and select `firmware-complete.bin`
5. **IMPORTANT:** Change **Flash Address** to `0x0` (default may be different)
6. Click **"Program"** button
7. Wait ~30 seconds for flashing to complete
8. You should see "Flash complete!" message

**Advantages:**
- No software installation required
- Works in Chrome, Edge, or Opera browsers
- Simple and beginner-friendly
- Cross-platform (Windows, Mac, Linux)

### Method 2: Using esptool.py (Command Line)

#### Prerequisites
Install esptool:
```bash
pip install esptool
```

#### Flash Command
Connect your ESP32-C3 via USB and run:

```bash
esptool.py --chip esp32c3 --port COM3 --baud 460800 write_flash 0x0 firmware-complete.bin
```

**Important:** Replace `COM3` with your actual port:
- **Windows**: Check Device Manager (usually COM3, COM4, etc.)
- **Linux/Mac**: Usually `/dev/ttyUSB0` or `/dev/ttyACM0`

To find your port:
- **Windows**: Device Manager → Ports (COM & LPT)
- **Linux**: `ls /dev/tty*`
- **Mac**: `ls /dev/cu.*`

### Method 3: Using ESP32 Flash Download Tool (Windows Only)

1. Download [Flash Download Tool](https://www.espressif.com/en/support/download/other-tools)
2. Run the tool and select **ESP32-C3**
3. Add `firmware-complete.bin` at address `0x0`
4. Select your COM port
5. Set baud rate to 460800
6. Click **START**

## After Flashing

### First Boot - WiFi Setup
1. The ESP32 will create a WiFi access point
2. Connect to: **PCMonitor-Setup**
3. Password: **monitor123**
4. Open browser to: `192.168.4.1`
5. Configure your WiFi network
6. The device will connect and display its IP address

### Web Configuration
Once connected, access the ESP32's IP address in a browser to configure:
- Clock style (Mario, Standard, Large)
- Timezone settings
- Time format (12/24 hour)
- Date format
- Display labels (PUMP, FAN, etc.)
- **NEW in v1.1.0**: Metric visibility (show/hide individual metrics)
- **NEW in v1.1.0**: Clock visibility toggle in metrics mode

## Troubleshooting

**Web Flasher Issues:**
- **Port not detected**: Make sure you're using Chrome, Edge, or Opera (Firefox and Safari don't support Web Serial API)
- **Connection failed**: Try a different USB cable (must support data transfer)
- **Flash address not 0x0**: Double-check flash address is set to `0x0` (zero)
- **Still not working**: Try the automated script or command-line method below

**Can't find COM port:**
- Install [CH340 drivers](https://learn.sparkfun.com/tutorials/how-to-install-ch340-drivers) if needed
- Some ESP32-C3 boards use CP2102 - install [CP210x drivers](https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers)

**Flash fails:**
- Hold BOOT button while connecting USB
- Try lower baud rate: `115200` instead of `460800`
- Make sure USB cable supports data (not just charging)

**ESP32 won't start after flashing:**
- Press RESET button
- Disconnect and reconnect power
- Check serial monitor at 115200 baud for errors

**Erase flash before flashing (if having issues):**
```bash
esptool.py --chip esp32c3 erase_flash
```
Then flash normally.

## Serial Monitor

To view debug output:
```bash
esptool.py --port COM3 monitor
```

Or use any serial terminal at **115200 baud**.

## Next Steps

After successfully flashing:
1. Complete WiFi setup via captive portal
2. Note the IP address shown on OLED
3. Set up the [PC Stats Python script](../../pc_stats_monitor.py)
4. Configure it with your ESP32's IP address
5. Run the Python script to start monitoring

For complete setup instructions, see the main [README](../../README.md).

## Technical Specifications

- **Chip**: ESP32-C3
- **Flash Size**: 4MB
- **Partition Scheme**: Default (OTA support)
- **Framework**: Arduino
- **Board**: ESP32-C3-DevKitM-1

## Version Information

**Version**: 1.1.0
**Build Date**: 2025-12-02
**Firmware Size**: ~962 KB (71.8% flash usage)
**RAM Usage**: ~40 KB (12.6% RAM usage)

**Features in this version:**
- Customizable display labels (PUMP/FAN/etc.)
- **NEW**: Metric visibility toggles (show/hide Fan, CPU, RAM, GPU, Disk)
- **NEW**: Clock visibility option in metrics mode
- **NEW**: Dynamic layout that reflows when metrics are hidden
- Web configuration portal for all settings
- Dual display modes (PC online stats / offline clock)
- Three clock styles (Mario, Standard, Large)
- Persistent settings saved to flash

## Source Code

This firmware is built from the source code in this repository. To build from source, see the [README](../../README.md) for PlatformIO instructions.
