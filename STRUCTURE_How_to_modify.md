# SmallOLED-PCMonitor - Code Structure

## Quick Start for Users

**To configure your hardware, edit only one file:**
```
src/config/user_config.h
```

### User-Configurable Settings

| Setting | Location | Description |
|---------|----------|-------------|
| `DEFAULT_DISPLAY_TYPE` | user_config.h:13 | 0 = SSD1306 (0.96"), 1 = SH1106 (1.3") |
| `I2C_SDA_PIN` | user_config.h:16 | I2C data pin (default: 8) |
| `I2C_SCL_PIN` | user_config.h:17 | I2C clock pin (default: 9) |
| `DISPLAY_I2C_ADDRESS` | user_config.h:24 | Display address (0x3C or 0x3D) |
| `AP_NAME` | user_config.h:28 | WiFi setup portal name |
| `AP_PASSWORD` | user_config.h:29 | WiFi setup portal password |
| `UDP_PORT` | user_config.h:36 | Port for receiving PC stats (4210) |
| `TOUCH_BUTTON_ENABLED` | user_config.h:73 | TTP223 touch sensor: 1 = enabled, 0 = disabled |
| `TOUCH_BUTTON_PIN` | user_config.h:74 | GPIO pin for TTP223 touch sensor (default: 7) |
| `TOUCH_DEBOUNCE_MS` | user_config.h:75 | Button debounce delay in milliseconds (default: 200) |
| `TOUCH_ACTIVE_LEVEL` | user_config.h:76 | TTP223 signal level: HIGH or LOW (default: HIGH) |

---

## File Structure

```
src/
├── main.cpp                 # Entry point: setup(), loop(), global state
├── config/
│   ├── user_config.h        # USER-EDITABLE: Display, WiFi, I2C settings
│   ├── config.h             # Shared structs, constants, extern declarations
│   └── settings.cpp         # loadSettings(), saveSettings() - NVS persistence
├── display/
│   └── display.h            # Display type selection & extern declaration
├── clocks/
│   ├── clocks.h             # All clock function declarations
│   ├── clock_common.cpp     # Shared: shouldShowColon(), digit bounce, Standard/Large clocks
│   ├── clock_mario.cpp      # Mario clock animation
│   ├── clock_space.cpp      # Space Invaders clock animation
│   ├── clock_pong.cpp       # Pong/Breakout clock animation
│   └── clock_pacman.cpp     # Pac-Man clock animation + digit patterns
├── metrics/
│   ├── metrics.h            # Metrics display declarations
│   └── metrics.cpp          # PC stats display functions
├── network/
│   ├── network.h            # Network declarations
│   └── network.cpp          # WiFi, UDP handling
├── web/
│   ├── web.h                # Web server declarations
│   └── web.cpp              # Web interface, settings handlers
└── utils/
    ├── utils.h              # Utility function declarations
    └── utils.cpp            # String helpers, validation
```

---

## Clock Styles

| Style | Name | File | Description |
|-------|------|------|-------------|
| 0 | Mario | clock_mario.cpp | Animated Mario jumps to change digits |
| 1 | Standard | clock_common.cpp | Simple digital clock |
| 2 | Large | clock_common.cpp | Larger digits |
| 3 | Space Invaders | clock_space.cpp | Invader/ship shoots laser at digits |
| 4 | Pong | clock_pong.cpp | Breakout-style ball physics |
| 5 | Pac-Man | clock_pacman.cpp | Pac-Man eats pellet-based digits |

---

## Key Components

### main.cpp
- **Global state variables**: `settings`, `metricData`, `display`, WiFi state
- **Entry points**: `setup()`, `loop()`
- **Helper functions**: `getTimeWithTimeout()`, `isAnimationActive()`, `getOptimalRefreshRate()`
- **Forward declarations** for all module functions

### config/config.h
- **Structs**: `Settings`, `Metric`, `MetricData`, animation state structs
- **Enums**: `MarioState`, `SpaceState`, `PacmanState`, `PongState`
- **Constants**: Screen dimensions, animation parameters
- **Extern declarations**: All shared global variables

### config/settings.cpp
- `loadSettings()`: Load from ESP32 NVS (Preferences)
- `saveSettings()`: Save to ESP32 NVS

### web/web.cpp
- `setupWebServer()`: Register HTTP routes
- `handleRoot()`: Main HTML interface
- `handleSave()`: Save settings from web form
- `handleMetricsAPI()`: JSON API for metrics
- `handleExportConfig()` / `handleImportConfig()`: Backup/restore

### network/network.cpp
- `parseStats()`: Parse incoming UDP JSON
- `parseStatsV2()`: Version 2 protocol handler
- WiFiUDP instance (`udp`)

---

## Display Type Selection

The display type is selected at **compile time**:

1. Set `DEFAULT_DISPLAY_TYPE` in `src/config/user_config.h`
2. Build and upload

The preprocessor selects the correct library and display object:
- Type 0: `Adafruit_SSD1306` (0.96" OLED)
- Type 1: `Adafruit_SH1106G` (1.3" OLED)

---

## Adding a New Clock Style

1. Create `src/clocks/clock_newstyle.cpp`
2. Include the required headers:
   ```cpp
   #include "../config/config.h"
   #include "../display/display.h"
   #include "clocks.h"
   ```
3. Implement `displayClockWithNewStyle()` function
4. Add declaration to `src/clocks/clocks.h`
5. Add case in `main.cpp` loop to call your function
6. Update web interface in `web/web.cpp` to add option

---

## Build Commands

```bash
# Build
pio run

# Upload
pio run --target upload

# Clean
pio run --target clean

# Monitor serial
pio device monitor
```

---

## Web Interface Settings

All runtime settings are configurable via web interface at `http://<device-ip>/`:

- Clock style selection
- Date format (DD/MM, MM/DD, ISO)
- Timezone offset
- Colon blink mode/rate
- Refresh rate (auto/manual)
- Animation speeds (per clock type)
- PC metrics display options

Settings persist in ESP32 flash memory (NVS).

---

## Touch Button Feature (TTP223 Sensor)

### Overview
Optional TTP223 capacitive touch sensor allows toggling between PC monitoring mode and clock mode via a physical button press.

### Hardware Connection
```
TTP223 Module          ESP32-C3
-----------          ----------
  VCC      ------>   3.3V
  GND      ------>   GND
  SIG      ------>   GPIO 7
```

**Note:** The TTP223 sensor is optional. Set `TOUCH_BUTTON_ENABLED = 0` in `user_config.h` if not using it.

### Button Behavior

**When PC is Online** (sending stats):
- Press button → Toggle between PC metrics display and clock display
- Press again → Return to previous mode
- Animation runs at smooth 40 Hz when in clock mode

**When PC is Offline** (no stats/timeout):
- Press button → Cycle through clock styles
- Cycle order: Mario → Standard → Large → Space Invaders → Space Ship → Pong → Pac-Man → repeat
- Changes are temporary (not saved to flash)

### Configuration

Edit `src/config/user_config.h`:

| Setting | Default | Description |
|---------|---------|-------------|
| `TOUCH_BUTTON_ENABLED` | 0 | Set to 1 to enable touch button support |
| `TOUCH_BUTTON_PIN` | 7 | GPIO pin connected to TTP223 signal |
| `TOUCH_DEBOUNCE_MS` | 200 | Button debounce delay (milliseconds) |
| `TOUCH_ACTIVE_LEVEL` | HIGH | Signal level when touched (HIGH or LOW) |

**To enable:** Set `TOUCH_BUTTON_ENABLED 1` and rebuild/upload.

**To disable:** Set `TOUCH_BUTTON_ENABLED 0` and rebuild (no performance overhead when disabled).
