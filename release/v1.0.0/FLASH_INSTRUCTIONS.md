# PC Stats Monitor - Firmware v1.0.0

## Pre-built Firmware for ESP32-C3

This release contains pre-compiled binary files ready to flash to your ESP32-C3 board.

## What's Included

### Single File (Recommended)
- `firmware-complete.bin` - Complete firmware (1 MB) - **Flash this for easiest setup!**

### Separate Files (Advanced)
- `bootloader.bin` - ESP32-C3 bootloader
- `partitions.bin` - Partition table
- `firmware.bin` - Main application firmware

## Flashing Instructions

### Method 1: Using esptool.py (Recommended)

#### Prerequisites
Install esptool:
```bash
pip install esptool
```

#### Flash Command - Single File (Easiest!)
Connect your ESP32-C3 via USB and run:

```bash
esptool.py --chip esp32c3 --port COM3 --baud 460800 write_flash 0x0 firmware-complete.bin
```

#### Alternative - Separate Files (Advanced)
```bash
esptool.py --chip esp32c3 --port COM3 --baud 460800 write_flash -z 0x0 bootloader.bin 0x8000 partitions.bin 0x10000 firmware.bin
```

**Important:** Replace `COM3` with your actual port:
- **Windows**: Check Device Manager (usually COM3, COM4, etc.)
- **Linux/Mac**: Usually `/dev/ttyUSB0` or `/dev/ttyACM0`

To find your port:
- **Windows**: Device Manager → Ports (COM & LPT)
- **Linux**: `ls /dev/tty*`
- **Mac**: `ls /dev/cu.*`

### Method 2: Using ESP32 Flash Download Tool (Windows)

1. Download [Flash Download Tool](https://www.espressif.com/en/support/download/other-tools)
2. Run the tool and select **ESP32-C3**
3. **Option A - Single File (Easiest):**
   - `firmware-complete.bin` at address `0x0`
4. **Option B - Separate Files:**
   - `bootloader.bin` at address `0x0`
   - `partitions.bin` at address `0x8000`
   - `firmware.bin` at address `0x10000`
5. Select your COM port
6. Set baud rate to 460800
7. Click **START**

### Method 3: Web-Based Flasher (No Installation Required!)

Visit [ESP Tool Online](https://espressif.github.io/esptool-js/) and:
1. Connect ESP32-C3 via USB
2. Click "Connect" and select your port
3. Add file: `firmware-complete.bin` at offset `0x0`
4. Click "Program"

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

## Troubleshooting

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

**Version**: 1.0.0
**Build Date**: 2025-11-28
**Firmware Size**: ~954 KB (71% flash usage)
**RAM Usage**: ~40 KB (12.6% RAM usage)

## Source Code

This firmware is built from the source code in this repository. To build from source, see the [README](../../README.md) for PlatformIO instructions.
