# PC Stats Monitor - Firmware Release v1.0.0

Pre-compiled firmware binaries for ESP32-C3 boards.

## Quick Start

### Windows
1. Install esptool: `pip install esptool`
2. Double-click `flash.bat`
3. Enter your COM port when prompted
4. Wait for flashing to complete

### Linux/Mac
1. Install esptool: `pip install esptool`
2. Run: `./flash.sh`
3. Enter your port when prompted (e.g., `/dev/ttyUSB0`)
4. Wait for flashing to complete

### Manual Flashing
See [FLASH_INSTRUCTIONS.md](FLASH_INSTRUCTIONS.md) for detailed instructions and alternative methods.

## Files Included

### Firmware
- `firmware-complete.bin` - **Complete firmware (1 MB) - Easiest to flash!**
- `bootloader.bin` - ESP32-C3 bootloader (separate, flash at 0x0)
- `partitions.bin` - Partition table (separate, flash at 0x8000)
- `firmware.bin` - Main firmware (separate, flash at 0x10000)

### Tools
- `flash.bat` - Automated flash script for Windows
- `flash.sh` - Automated flash script for Linux/Mac
- `merge_bins.py` - Script to recreate complete binary

### Documentation
- `FLASH_INSTRUCTIONS.md` - Detailed flashing guide
- `README.md` - This file

## After Flashing

1. ESP32 creates WiFi AP: **PCMonitor-Setup** (password: **monitor123**)
2. Connect and open `192.168.4.1` to configure WiFi
3. Note the IP address displayed on the OLED
4. Set up the Python monitoring script with this IP
5. Enjoy your PC stats monitor!

## Requirements

- ESP32-C3 board (tested on ESP32-C3-DevKitM-1)
- SSD1306 OLED display (128x64, I2C)
- USB cable with data support
- esptool.py installed

## Support

For complete setup instructions and Python script configuration, see the main [README](../../README.md).

For issues or questions, please open an issue on GitHub.

## Version Info

- **Version**: 1.0.0
- **Release Date**: 2025-11-28
- **Chip**: ESP32-C3
- **Flash Usage**: 71.5% (936,816 bytes)
- **RAM Usage**: 12.6% (41,396 bytes)

## Changelog

**v1.0.0 (2025-11-28)**
- Customizable display labels via web interface
- Dual display modes (PC online/offline)
- Three clock styles (Mario, Standard, Large)
- Web configuration portal for all settings
- Persistent settings in flash memory
- WiFi portal for easy setup
