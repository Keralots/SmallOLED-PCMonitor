# Implementation Notes - Remaining Tasks

## Issue 1: ✅ Fixed Arrow Symbols
- Added UTF-8 charset meta tag
- Changed arrows to HTML entities (&#8592; and &#8594;)

## Issue 2: Column Selection (In Progress)
Need to complete:

### Already Done:
- Added `column` field to Metric struct
- Added `metricColumns` array to Settings
- Added load/save for column settings
- Default: alternating left/right (i % 2)

### TODO:
1. Update `handleMetricsAPI()` to include column info (line ~763)
2. Update `handleSave()` to process column selections (line ~1088)
3. Add column dropdown to web interface JavaScript
4. Update display rendering to respect column choices (line ~988)
5. Apply column settings when parsing metrics (line ~1507, ~1395)

## Issue 3: Python GUI Improvements

### Required Changes:
1. **Bigger Window**: Change from 1000x750 to 1200x850
2. **Autostart Buttons**:
   - Add "Enable Autostart" button
   - Add "Disable Autostart" button
   - Show current autostart status
3. **Install Dependencies Button**:
   - Check for missing packages (psutil, wmi, pywin32, pystray, pillow)
   - Button to run `pip install` for missing packages
4. **Install LibreHardwareMonitor**:
   - Download from GitHub releases
   - Extract to user folder
   - Create shortcut or batch file to launch

## Issue 4: FPS Counter

### Investigation:
FPS metrics are NOT available through LibreHardwareMonitor. Options:

1. **PresentMon** (Microsoft tool):
   - Requires separate installation
   - Complex to integrate
   - Not a simple Python library

2. **NVIDIA/AMD SDKs**:
   - GPU-specific
   - Requires native libraries
   - Complex integration

3. **HWiNFO** (where user sees FPS):
   - Has shared memory interface
   - Possible to read, but complex
   - Would need to parse HWiNFO's shared memory structure

### Recommendation:
**Not feasible for "if easy" implementation**. Would require significant additional work. Suggest skipping unless user wants a full integration with HWiNFO's shared memory.

Alternative: User can use companion metrics to pair GPU Load% with GPU Temperature to see gaming performance indirectly.
