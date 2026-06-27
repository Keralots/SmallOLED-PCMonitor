@echo off
REM ---------------------------------------------------------------------------
REM  Build a standalone Windows .exe of pc_stats_monitor_v4.py with PyInstaller
REM
REM  One-time setup (installs runtime deps + PyInstaller):
REM    python -m pip install -r requirements.txt
REM    python -m pip install pyinstaller
REM
REM  Then double-click this file, or run it from a terminal in this folder.
REM  Output lands in:
REM    dist\pc_stats_monitor_v4.exe
REM
REM  Notes:
REM   * --windowed  -> no console window flashes at login (this is a background
REM                    app that lives in the system tray). print() output is
REM                    redirected to %APPDATA%\PCStatsMonitor\monitor.log.
REM   * --add-data "webui;webui" bundles the web UI (index.html/portal.css/js).
REM     At runtime server.webui_dir() resolves it from sys._MEIPASS.
REM   * --collect-all webview / pythonnet pulls in the pywebview EdgeChromium
REM     (WebView2) backend, which PyInstaller can't see (loaded via clr).
REM   * The other hidden-imports cover lazily-imported wmi / pystray / PIL /
REM     pywin32 modules.
REM   * WebView2 runtime is required on the target PC (ships with Windows 11;
REM     on Windows 10 install the Evergreen runtime from Microsoft).
REM ---------------------------------------------------------------------------

setlocal
cd /d "%~dp0"

REM PyInstaller's wrapper exe is often on a per-user Scripts dir that's not on
REM PATH. Invoke it as a module instead. Try "python" first, then the "py" launcher.
set "PYI="
python -m PyInstaller --version >nul 2>&1 && set "PYI=python -m PyInstaller"
if not defined PYI py -m PyInstaller --version >nul 2>&1 && set "PYI=py -m PyInstaller"
if not defined PYI (
  echo ERROR: PyInstaller is not importable from "python" or "py".
  echo Install build dependencies first:
  echo     python -m pip install -r requirements.txt
  echo     python -m pip install pyinstaller
  pause
  exit /b 1
)
echo Using: %PYI%

REM Clean previous build artifacts so we don't ship stale bundles. The committed
REM .spec is NOT deleted - this build drives PyInstaller via the flags below.
if exist build rmdir /s /q build
if exist dist  rmdir /s /q dist

%PYI% ^
  --onefile ^
  --windowed ^
  --name pc_stats_monitor_v4 ^
  --splash splash.png ^
  --paths "..\companion-common" ^
  --add-data "..\companion-common\webui;webui" ^
  --hidden-import server ^
  --hidden-import app_window ^
  --hidden-import app_state ^
  --hidden-import layout_engine ^
  --collect-all webview ^
  --collect-all pythonnet ^
  --collect-all pystray ^
  --collect-all PIL ^
  --hidden-import clr ^
  --hidden-import wmi ^
  --hidden-import pythoncom ^
  --hidden-import pywintypes ^
  --hidden-import win32com ^
  --hidden-import win32com.client ^
  --hidden-import win32api ^
  --hidden-import win32con ^
  --hidden-import win32timezone ^
  pc_stats_monitor_v4.py

if errorlevel 1 (
  echo.
  echo BUILD FAILED.
  pause
  exit /b 1
)

echo.
echo ---------------------------------------------------------------
echo  BUILD OK
echo  Output: %CD%\dist\pc_stats_monitor_v4.exe
echo ---------------------------------------------------------------
echo.
echo Test it by double-clicking the .exe in dist\ .
pause
