@echo off
REM ESP32 Configurator - Dependency Installer
REM Installs all required Python packages

echo ======================================================================
echo   ESP32 OLED CONFIGURATOR - DEPENDENCY INSTALLER
echo ======================================================================
echo.

echo [1/2] Checking Python installation...
python --version
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Python not found!
    echo Please install Python 3.8 or later from https://www.python.org/
    pause
    exit /b 1
)
echo [OK] Python installed
echo.

echo [2/2] Installing dependencies from requirements.txt...
pip install -r requirements.txt
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Failed to install dependencies!
    pause
    exit /b 1
)
echo [OK] All dependencies installed
echo.

echo ======================================================================
echo   INSTALLATION COMPLETE
echo ======================================================================
echo.
echo You can now run the application with:
echo   python esp32_configurator.py
echo.
pause
