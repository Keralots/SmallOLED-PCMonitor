@echo off
REM Quick flash script for Windows
REM Make sure esptool is installed: pip install esptool

echo ========================================
echo PC Stats Monitor - ESP32-C3 Flasher
echo ========================================
echo.

REM Check if esptool is installed
where esptool.py >nul 2>nul
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: esptool.py not found!
    echo Install it with: pip install esptool
    pause
    exit /b 1
)

echo Available COM ports:
echo.
mode | findstr "COM"
echo.

set /p PORT="Enter your COM port (e.g., COM3): "

echo.
echo Flashing firmware to %PORT%...
echo This will take about 30 seconds...
echo.

esptool.py --chip esp32c3 --port %PORT% --baud 460800 write_flash 0x0 firmware-complete.bin

if %ERRORLEVEL% EQU 0 (
    echo.
    echo ========================================
    echo Flashing completed successfully!
    echo ========================================
    echo.
    echo Next steps:
    echo 1. Disconnect and reconnect ESP32
    echo 2. Connect to WiFi: PCMonitor-Setup
    echo 3. Password: monitor123
    echo 4. Open: 192.168.4.1
    echo.
) else (
    echo.
    echo ========================================
    echo Flashing FAILED!
    echo ========================================
    echo.
    echo Troubleshooting:
    echo - Try holding BOOT button while connecting
    echo - Check if correct COM port
    echo - Try lower baud rate: 115200
    echo.
)

pause
