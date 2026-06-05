@echo off
REM ---------------------------------------------------------------------------
REM  Build a standalone Windows .exe of pc_stats_monitor_v3.py with PyInstaller
REM
REM  One-time setup (installs runtime deps + PyInstaller):
REM    python -m pip install -r requirements.txt
REM    python -m pip install pyinstaller
REM
REM  Then double-click this file, or run it from a terminal in this folder.
REM  Output lands in:
REM    dist\pc_stats_monitor_v3.exe
REM
REM  Notes:
REM   * --windowed  -> no console window flashes at login (this is a background
REM                    app that lives in the system tray). print() output is
REM                    redirected to %APPDATA%\PCStatsMonitor\monitor.log.
REM   * The hidden-imports / collect-all flags cover modules PyInstaller can't
REM     see because they're imported lazily (wmi, pystray, PIL, pywin32).
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

REM Clean previous build artifacts so we don't ship stale bundles
if exist build rmdir /s /q build
if exist dist  rmdir /s /q dist
if exist pc_stats_monitor_v3.spec del /q pc_stats_monitor_v3.spec

%PYI% ^
  --onefile ^
  --windowed ^
  --name pc_stats_monitor_v3 ^
  --splash splash.png ^
  --collect-all pystray ^
  --collect-all PIL ^
  --hidden-import wmi ^
  --hidden-import pythoncom ^
  --hidden-import pywintypes ^
  --hidden-import win32com ^
  --hidden-import win32com.client ^
  --hidden-import win32api ^
  --hidden-import win32con ^
  --hidden-import win32timezone ^
  pc_stats_monitor_v3.py

if errorlevel 1 (
  echo.
  echo BUILD FAILED.
  pause
  exit /b 1
)

echo.
echo ---------------------------------------------------------------
echo  BUILD OK
echo  Output: %CD%\dist\pc_stats_monitor_v3.exe
echo ---------------------------------------------------------------
echo.
echo Test it by double-clicking the .exe in dist\ .
pause
