# Changes Summary - Row-Based Layout & Python GUI Improvements

## ESP32 Web Interface Changes

### ✅ Row-Based Visual Layout
The web interface now shows a visual preview that matches the OLED screen:

**Before:**
- Single vertical list of metrics
- Unclear which metrics appear on left vs right columns

**After:**
- "OLED Display Preview" header
- Shows rows (Row 1, Row 2, etc.)
- Each row has two slots: **[Left Metric | Right Metric]**
- Empty slots display "← Empty" or "→ Empty"
- Users can assign each metric to left or right column using dropdown

**How it works:**
1. Each metric has a "Column Position" dropdown (← Left / Right →)
2. Changing the dropdown instantly moves the metric to the correct column
3. Metrics are grouped by rows - Row 1 shows first left + first right metric
4. If right column is empty for a row, the OLED screen will show blank space there

**Files Changed:**
- [src/PCMonitor_WifiPortal.cpp:938-1070](src/PCMonitor_WifiPortal.cpp#L938-L1070) - JavaScript rendering logic

### ✅ Display Rendering Updated
**Before:**
- Metrics rendered in alternating pattern (left, right, left, right)
- No way to have empty right column slots

**After:**
- Metrics separated into leftMetrics[] and rightMetrics[] arrays
- Renders row by row:
  - Row 0: leftMetrics[0] at COL1_X, rightMetrics[0] at COL2_X
  - Row 1: leftMetrics[1] at COL1_X, rightMetrics[1] at COL2_X
  - etc.
- Empty slots remain blank on OLED

**Example:**
```
Row 1: [CPU: 45%]     [GPU: 72C]
Row 2: [RAM: 8GB]     [      ]      <- Right column empty
Row 3: [FAN: 1.2K]    [PWR: 65W]
```

**Files Changed:**
- [src/PCMonitor_WifiPortal.cpp:1800-1950](src/PCMonitor_WifiPortal.cpp#L1800-L1950) - Display rendering logic
- [src/PCMonitor_WifiPortal.cpp:320](src/PCMonitor_WifiPortal.cpp#L320) - Function declaration

---

## Python Script Improvements

### ✅ 1. Bigger Configuration Window
**Before:** 1000x750 pixels
**After:** 1200x850 pixels

More space for metrics selection and settings.

**Files Changed:**
- [pc_stats_monitor_v2.py:358](pc_stats_monitor_v2.py#L358)

### ✅ 2. Autostart Buttons in GUI
Added autostart controls directly in the configuration window:

**Features:**
- **Status indicator**: Shows "✓ Enabled" (green) or "✗ Disabled" (red)
- **Enable button**: Enables Windows autostart with one click
- **Disable button**: Disables Windows autostart
- **Automatic status refresh**: Updates after enabling/disabling

**Location:** Second row of settings frame, below ESP IP/Port/Interval

**How it works:**
- Creates shortcut in Windows Startup folder
- Uses `pythonw.exe` to run without console window
- Automatically adds `--minimized` flag to run in system tray
- Requires `pywin32` package (shows error message if not installed)

**Files Changed:**
- [pc_stats_monitor_v2.py:411-450](pc_stats_monitor_v2.py#L411-L450) - UI elements
- [pc_stats_monitor_v2.py:697-749](pc_stats_monitor_v2.py#L697-L749) - Helper methods

---

## What Was NOT Implemented

### ❌ Companion Metrics (Removed)
The "Pair" dropdown was removed as it conflicted with the row-based layout. Users can now control exactly which metrics appear on each row using the column assignment.

### ❌ Dependency Installer Button
Not implemented in this update. Users can manually install dependencies:
```bash
pip install psutil wmi pywin32 pystray pillow
```

### ❌ LibreHardwareMonitor Auto-Installer
Not implemented. Users need to manually install LibreHardwareMonitor.

---

## How to Use the New Features

### ESP32 Web Interface:

1. **Upload the new firmware** to your ESP32
2. **Open the web interface** (http://YOUR_ESP_IP)
3. **Configure metrics:**
   - Each metric shows checkbox, label, and "Column Position" dropdown
   - Use dropdown to select "← Left" or "Right →"
   - Metrics are automatically grouped into rows
4. **Example configuration:**
   ```
   Metric 1: CPU% → Column: Left
   Metric 2: GPU → Column: Right
   Metric 3: RAM → Column: Left
   Metric 4: (empty right slot)

   OLED will show:
   Row 1: CPU%: 45    GPU: 72C
   Row 2: RAM: 8GB    [empty]
   ```

### Python Script Autostart:

**Method 1: Use GUI buttons**
1. Run `python pc_stats_monitor_v2.py --edit`
2. Click **"Enable"** button in the "Windows Autostart" section
3. Done! Script will run on Windows startup

**Method 2: Command line**
```bash
# Enable autostart
python pc_stats_monitor_v2.py --autostart enable

# Disable autostart
python pc_stats_monitor_v2.py --autostart disable
```

---

## Testing Checklist

- [x] ESP32 firmware compiles without errors
- [x] Python script runs without syntax errors
- [x] Web interface shows row-based layout
- [x] Autostart buttons appear in GUI
- [ ] Upload firmware to ESP32 and test web interface
- [ ] Test autostart enable/disable functionality
- [ ] Test OLED display with various row configurations
- [ ] Test empty right column slots

---

## File Changes Summary

### Modified Files:
1. **src/PCMonitor_WifiPortal.cpp** - ESP32 firmware
   - Lines 320: Added function declaration
   - Lines 938-1070: Web interface JavaScript (row-based rendering)
   - Lines 1800-1950: Display rendering (row-based logic)

2. **pc_stats_monitor_v2.py** - Python monitoring script
   - Line 358: Window size increased to 1200x850
   - Lines 411-450: Autostart UI elements
   - Lines 697-749: Autostart helper methods

### New Files:
- **CHANGES_SUMMARY.md** - This file

---

## Build Info

**ESP32 Firmware:**
- RAM Usage: 13.1% (43,012 / 327,680 bytes)
- Flash Usage: 72.9% (956,038 / 1,310,720 bytes)
- Compilation: ✅ Success
- Binary: `.pio/build/esp32-c3-devkitm-1/firmware.bin`

**Upload Command:**
```bash
"C:\Users\rafal\.platformio\penv\Scripts\platformio.exe" run --target upload
```
