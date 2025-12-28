# ESP32 OLED Configurator

Professional PyQt6 GUI for SmallOLED-PCMonitor ESP32 displays

## Features

- ✅ **Drag-and-drop sensor assignment** - Intuitive UX for selecting metrics
- ✅ **Live OLED preview (128×64)** - Pixel-accurate simulation with Adafruit GFX font
- ✅ **Real-time sensor updates** - Background thread updating values every 1 second
- ✅ **200+ sensor support** - HWiNFO64 or LibreHardwareMonitor
- ✅ **Separate configuration** - Uses `esp32_config.json` (won't conflict with existing script)

## Installation

### 1. Install Python Dependencies

```bash
cd esp32-configurator
pip install -r requirements.txt
```

This will install:
- PyQt6 (GUI framework)
- psutil (system metrics)
- pywin32 (Windows API for HWiNFO access)
- wmi (LibreHardwareMonitor fallback)

### 2. Run the Application

```bash
python esp32_configurator.py
```

## Quick Start

1. **Launch the application**
   ```bash
   python esp32_configurator.py
   ```

2. **Configure ESP32 settings**
   - Enter your ESP32 IP address
   - Set UDP port (default: 4210)
   - Set update interval (default: 3 seconds)
   - Click "Test Connection" to verify

3. **Select sensors** (Phase 2+)
   - Browse available sensors in the left panel
   - Drag sensors to the right panel (max 20)
   - Reorder by dragging
   - Edit custom labels

4. **Preview OLED display** (Phase 4+)
   - See pixel-accurate 128×64 simulation
   - Verify names are truncated to 10 characters
   - Check layout matches ESP32

5. **Save configuration**
   - Click "Save Configuration"
   - Configuration saved to `esp32_config.json`
   - Run `pc_stats_monitor_hwinfo.py` to start monitoring

## Project Status

### Phase 1: Core GUI Framework ✅ COMPLETE (10 hours)

**Deliverables:**
- ✅ Project directory structure
- ✅ Dark theme QSS stylesheet
- ✅ Main window with menu bar
- ✅ Settings panel with validation
- ✅ Dual-panel layout (placeholders)

**How to test:**
```bash
python esp32_configurator.py
```

Expected: Application launches with dark theme, menu bar works, settings can be configured.

### Phase 2: Sensor Integration ✅ COMPLETE (8 hours)

**Deliverables:**
- ✅ Imported sensor discovery from `pc_stats_monitor_hwinfo.py`
- ✅ Created hierarchical sensor model (Device → Category → Sensor)
- ✅ Created sensor tree widget with QTreeWidget
- ✅ Implemented search/filter functionality
- ✅ Integrated sensor tree into main window
- ✅ Auto-discovery on startup (HWiNFO64/LibreHardwareMonitor)

**How to test:**
```bash
python esp32_configurator.py
```

Expected: Application launches and discovers sensors. Left panel shows hierarchical sensor tree. Search bar filters sensors in real-time. Double-clicking a sensor shows a preview dialog.

**Test Results:**
- ✅ Successfully discovered 253 sensors from HWiNFO64
- ✅ Hierarchical tree displays: 13 devices, 7 categories, 249 hardware + 4 system sensors
- ✅ Search functionality working
- ✅ Status bar shows sensor count and source

### Phase 3: Drag-and-Drop ✅ COMPLETE (8 hours)

**Deliverables:**
- ✅ Created metrics data model (20-metric limit, validation)
- ✅ Created metrics list widget with drag-drop support
- ✅ Enabled dragging from sensor tree
- ✅ Enabled dropping on metrics list
- ✅ Implemented metric reordering (drag-drop within list)
- ✅ Added delete functionality (Delete key, right-click, Clear All button)
- ✅ Integrated into main window with working signal handlers

**How to test:**
```bash
python esp32_configurator.py
```

Expected:
- Drag sensors from left tree to right metrics list
- Double-click sensors to add them to metrics list
- Reorder metrics by dragging within the list
- Delete metrics with Delete key or right-click menu
- 20-metric limit enforced with counter display

**Test Results:**
- ✅ Drag-drop from sensor tree to metrics list working
- ✅ Double-click to add sensor working
- ✅ Metric reordering working
- ✅ Delete functionality working (Delete key, right-click, Clear All)
- ✅ 20-metric limit properly enforced
- ✅ Duplicate detection working
- ✅ Counter updates correctly (0/20, 5/20, etc.)

### Phase 4: OLED Preview ✅ COMPLETE (10 hours)

**Deliverables:**
- ✅ Created Adafruit GFX 5×7 bitmap font data (96 characters, ASCII 32-126)
- ✅ Created GFX font renderer with pixel-accurate rendering
- ✅ Created OLED preview widget (128×64 with 4× zoom)
- ✅ Implemented metric display layout (left-aligned labels, right-aligned values)
- ✅ Added automatic scroll animation for >8 metrics
- ✅ Integrated into main window with real-time updates
- ✅ Connected to metrics model for automatic refresh

**How to test:**
```bash
python esp32_configurator.py
```

Expected:
- OLED preview displays in right panel below metrics list
- Preview shows "No metrics selected" when empty
- Adding metrics updates preview in real-time
- Metrics display with left-aligned labels and right-aligned values
- Auto-scrolling when >8 metrics (64 pixels / 8 pixels per line)
- Pixel-accurate 5×7 font rendering (matches ESP32 Adafruit_GFX)

**Test Results:**
- ✅ OLED preview displays correctly (128×64 with border)
- ✅ Font rendering pixel-accurate (5×7 Adafruit GFX bitmap)
- ✅ Metrics update in real-time when added/removed
- ✅ Text truncation working (ellipsis for long names)
- ✅ Value formatting correct (integers for %, °C; decimals for others)
- ✅ Scroll animation working for long metric lists
- ✅ 4× zoom makes preview clearly visible

### Phase 5: Live Updates (6 hours) - PENDING

**Goals:**
- Background QThread for sensor polling
- Update sensor tree every 1 second
- Update OLED preview with new values
- Thread safety with QMutex

### Phase 6: Configuration & Polish (8-10 hours) - PENDING

**Goals:**
- Save/load `esp32_config.json`
- Input validation and network testing
- User documentation
- GitHub release preparation

## File Structure

```
esp32-configurator/
├── esp32_configurator.py          # Main entry point
├── ui/
│   ├── __init__.py
│   ├── main_window.py              # Main window class
│   ├── settings_panel.py           # ESP32 settings widget
│   ├── sensor_tree.py              # Sensor tree widget (Phase 2)
│   ├── metric_list.py              # Metrics list widget (Phase 3)
│   └── oled_preview.py             # OLED simulation (Phase 4)
├── core/
│   ├── __init__.py
│   ├── sensor_discovery.py         # Sensor discovery (Phase 2)
│   ├── sensor_reader.py            # Background updater (Phase 5)
│   ├── config_manager.py           # JSON I/O (Phase 6)
│   └── esp32_protocol.py           # JSON V2.0 encoding (Phase 6)
├── models/
│   ├── __init__.py
│   ├── sensor_model.py             # Sensor data model (Phase 2)
│   └── metrics_model.py            # Metrics model (Phase 3)
├── resources/
│   ├── styles/
│   │   └── dark_theme.qss          # Qt stylesheet
│   └── fonts/
│       └── adafruit_gfx_font.py    # 5×7 bitmap font (Phase 4)
├── requirements.txt                # Python dependencies
├── README.md                       # This file
└── LICENSE                         # MIT License (Phase 6)
```

## Dependencies

### Required
- Python 3.8+
- PyQt6 >= 6.5.0
- psutil >= 5.9.0
- pywin32 >= 305
- wmi >= 1.5.1

### Optional
- HWiNFO64 (for sensor access) - Download from https://www.hwinfo.com/
- LibreHardwareMonitor (fallback) - Download from https://github.com/LibreHardwareMonitor/LibreHardwareMonitor

## Configuration

The application saves configuration to `esp32_config.json` in the same directory.

**Example configuration:**
```json
{
  "version": "2.0",
  "esp32_ip": "192.168.0.163",
  "udp_port": 4210,
  "update_interval": 3,
  "metrics": [
    {
      "id": 1,
      "name": "CPU_C0",
      "display_name": "Core 0 Temperature [CPU [#0]: Intel i9]",
      "source": "hwinfo",
      "type": "temperature",
      "unit": "C",
      "hwinfo_reading_id": 305419896,
      "custom_label": "",
      "current_value": 65
    }
  ]
}
```

This format is **identical** to `monitor_config_hwinfo.json` used by `pc_stats_monitor_hwinfo.py`.

## Compatibility

- **ESP32 Firmware:** No changes required (uses JSON V2.0 protocol)
- **Existing Script:** Coexists with `pc_stats_monitor_hwinfo.py` (separate config files)
- **Operating System:** Windows 7 or later (64-bit)

## Troubleshooting

### Application won't start
- **Error:** `ModuleNotFoundError: No module named 'PyQt6'`
- **Fix:** Run `pip install -r requirements.txt`

### Dark theme not loading
- **Symptom:** Application appears with default Qt theme
- **Fix:** Check that `resources/styles/dark_theme.qss` exists

### Settings panel validation fails
- **Symptom:** "Invalid IP address" error
- **Fix:** Enter a valid IPv4 address (e.g., 192.168.0.163)

## Development

### Running from source
```bash
python esp32_configurator.py
```

### Testing individual widgets
```python
from PyQt6.QtWidgets import QApplication
from ui.settings_panel import SettingsPanelWidget

app = QApplication([])
widget = SettingsPanelWidget()
widget.show()
app.exec()
```

## License

MIT License (to be added in Phase 6)

## Contributing

See CONTRIBUTING.md (to be added in Phase 6)

## Support

- **Issues:** https://github.com/Keralots/SmallOLED-PCMonitor/issues
- **Documentation:** See docs/ folder (Phase 6)
- **Plan:** C:\Users\rafal\.claude\plans\compressed-yawning-plum.md

---

**Version:** 1.0.0 (Phase 1 - Skeleton)
**Last Updated:** 2025-12-26
