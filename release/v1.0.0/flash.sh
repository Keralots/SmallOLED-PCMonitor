#!/bin/bash
# Quick flash script for Linux/Mac
# Make sure esptool is installed: pip install esptool

echo "========================================"
echo "PC Stats Monitor - ESP32-C3 Flasher"
echo "========================================"
echo ""

# Check if esptool is installed
if ! command -v esptool.py &> /dev/null; then
    echo "ERROR: esptool.py not found!"
    echo "Install it with: pip install esptool"
    exit 1
fi

echo "Available serial ports:"
echo ""
if [[ "$OSTYPE" == "linux-gnu"* ]]; then
    ls /dev/ttyUSB* /dev/ttyACM* 2>/dev/null || echo "No devices found"
elif [[ "$OSTYPE" == "darwin"* ]]; then
    ls /dev/cu.* 2>/dev/null || echo "No devices found"
fi
echo ""

read -p "Enter your port (e.g., /dev/ttyUSB0): " PORT

echo ""
echo "Flashing firmware to $PORT..."
echo "This will take about 30 seconds..."
echo ""

esptool.py --chip esp32c3 --port "$PORT" --baud 460800 write_flash 0x0 firmware-complete.bin

if [ $? -eq 0 ]; then
    echo ""
    echo "========================================"
    echo "Flashing completed successfully!"
    echo "========================================"
    echo ""
    echo "Next steps:"
    echo "1. Disconnect and reconnect ESP32"
    echo "2. Connect to WiFi: PCMonitor-Setup"
    echo "3. Password: monitor123"
    echo "4. Open: 192.168.4.1"
    echo ""
else
    echo ""
    echo "========================================"
    echo "Flashing FAILED!"
    echo "========================================"
    echo ""
    echo "Troubleshooting:"
    echo "- Try holding BOOT button while connecting"
    echo "- Check if correct port"
    echo "- Try: sudo esptool.py ... (may need root)"
    echo "- Try lower baud rate: 115200"
    echo ""
fi
