# Pong Clock Configuration Guide

This document explains all customizable parameters for the Breakout-style Pong Clock (Clock Style 5).

## 📍 Where to Find Settings

All configuration variables are located in **[src/PCMonitor_WifiPortal.cpp](src/PCMonitor_WifiPortal.cpp)** starting at **line 411**.

## ⚙️ Configuration Variables

### Loop Prevention & Paddle Behavior

#### `PADDLE_STICK_MIN_DELAY` (line 415)
- **Default:** `0` ms
- **Range:** 0-500 ms
- **Purpose:** Minimum time ball sticks to paddle after bounce
- **Effect:**
  - `0` = Sometimes instant release (most chaotic)
  - Higher = Always some delay (more predictable)
- **Prevents:** Vertical loop patterns

#### `PADDLE_STICK_MAX_DELAY` (line 416)
- **Default:** `30` ms
- **Range:** 0-500 ms
- **Purpose:** Maximum time ball sticks to paddle after bounce
- **Effect:**
  - `30` = Quick, responsive gameplay (recommended)
  - `300` = Slower, more varied trajectories
  - `500` = Very slow, maximum randomness
- **Prevents:** Vertical loop patterns
- **Note:** Each paddle bounce gets random delay between MIN and MAX

#### `PADDLE_WRONG_DIRECTION_CHANCE` (line 417)
- **Default:** `0` % (DISABLED)
- **Range:** 0-100 %
- **Purpose:** Probability paddle moves opposite direction (adds unpredictability)
- **Effect:**
  - `0` = Perfect AI tracking, smooth paddle (recommended) ✅
  - `10` = Occasional mistakes (more chaos, paddle may shake)
  - `20` = Frequent mistakes (very chaotic)
  - `50` = Random AI behavior
- **Note:** Paddle stick delay already prevents loops, so this is typically not needed
- **Warning:** Non-zero values may cause paddle shaking

#### `PADDLE_MOMENTUM_MULTIPLIER` (line 418)
- **Default:** `3`
- **Range:** 1-5
- **Purpose:** How much paddle movement affects ball direction (classic Breakout mechanic)
- **Effect:**
  - `1` = Subtle momentum transfer
  - `3` = Strong effect (recommended) ✅
  - `5` = Wild, extreme bounces
- **Note:** Paddle moving left makes ball go left, moving right makes ball go right

---

### Ball Physics & Collision

#### `BALL_COLLISION_ANGLE_VARIATION` (line 421)
- **Default:** `6` units
- **Range:** 0-15
- **Purpose:** Random angle change when ball hits digit
- **Effect:**
  - `0` = Perfectly predictable bounces
  - `6` = Natural variation (recommended) ✅
  - `15` = Chaotic, unpredictable bounces
- **Applied:** Both horizontal and vertical velocity on digit collision

#### `BALL_RELEASE_RANDOM_VARIATION` (line 422)
- **Default:** `4` units
- **Range:** 0-10
- **Purpose:** Random velocity change when ball releases from paddle
- **Effect:**
  - `0` = Consistent paddle releases
  - `4` = Natural variation (recommended) ✅
  - `10` = Very chaotic releases
- **Note:** Adds to both vx and vy on release

#### `DIGIT_TRANSITION_TIMEOUT` (line 425) **[CRITICAL FIX]**
- **Default:** `5000` ms (5 seconds)
- **Range:** 3000-10000 ms
- **Purpose:** Auto-complete digit change if ball doesn't hit it in time
- **Effect:**
  - `3000` = Quick auto-change (may interrupt animations)
  - `5000` = Balanced (recommended) ✅
  - `10000` = Patient waiting (may cause time delays)
- **Prevents:** Digits getting stuck and falling behind actual time
- **How it works:** If minute changes but ball is hitting other digits, this ensures the change completes automatically

---

### Game Constants (Rarely Changed)

#### Speed & Timing

| Variable | Line | Default | Purpose |
|----------|------|---------|---------|
| `PONG_BALL_SPEED_NORMAL` | 431 | `18` | Base ball speed (1.125 px/frame at 50 FPS) |
| `PONG_BALL_SPEED_BOOST` | 432 | `32` | Ball speed during multiball (2.0 px/frame) |
| `MULTIBALL_ACTIVATE_SECOND` | 437 | `55` | When 2nd ball spawns (at :55 seconds) |
| `BALL_HIT_THRESHOLD` | 438 | `1` | Hits required to break digit (1 = instant) |
| `BALL_SPAWN_DELAY` | 435 | `400` ms | Delay before respawning missed ball |

#### Display Layout

| Variable | Line | Default | Purpose |
|----------|------|---------|---------|
| `PONG_TIME_Y` | 436 | `16` | Y position of time digits |
| `BREAKOUT_PADDLE_Y` | 427 | `60` | Y position of paddle (bottom) |
| `PONG_PLAY_AREA_TOP` | 429 | `10` | Top boundary (above digits) |
| `PONG_PLAY_AREA_BOTTOM` | 430 | `58` | Bottom boundary (above paddle) |

#### Fragment Animation

| Variable | Line | Default | Purpose |
|----------|------|---------|---------|
| `PONG_FRAG_SPEED` | 433 | `2.0` | Initial fragment velocity |
| `PONG_FRAG_GRAVITY` | 434 | `0.4` | Fragment falling speed |
| `DIGIT_ASSEMBLY_DURATION` | 439 | `500` ms | Time for new digit fragments to assemble |

---

## 🎮 Common Configurations

### Default (Balanced) ✅ RECOMMENDED
```cpp
PADDLE_STICK_MIN_DELAY = 0
PADDLE_STICK_MAX_DELAY = 30
PADDLE_WRONG_DIRECTION_CHANCE = 0          // Disabled - stick delay prevents loops
PADDLE_MOMENTUM_MULTIPLIER = 3
BALL_COLLISION_ANGLE_VARIATION = 6
BALL_RELEASE_RANDOM_VARIATION = 4
DIGIT_TRANSITION_TIMEOUT = 5000
```
**Best for:** Smooth paddle, natural ball movement, reliable time display

---

### Maximum Chaos
```cpp
PADDLE_STICK_MIN_DELAY = 0
PADDLE_STICK_MAX_DELAY = 100
PADDLE_WRONG_DIRECTION_CHANCE = 20
PADDLE_MOMENTUM_MULTIPLIER = 5
BALL_COLLISION_ANGLE_VARIATION = 15
BALL_RELEASE_RANDOM_VARIATION = 10
```
**Best for:** Unpredictable, wild gameplay

---

### Minimal Randomness
```cpp
PADDLE_STICK_MIN_DELAY = 50
PADDLE_STICK_MAX_DELAY = 50
PADDLE_WRONG_DIRECTION_CHANCE = 5
PADDLE_MOMENTUM_MULTIPLIER = 2
BALL_COLLISION_ANGLE_VARIATION = 3
BALL_RELEASE_RANDOM_VARIATION = 2
```
**Best for:** More predictable, consistent patterns

---

### Slow & Varied
```cpp
PADDLE_STICK_MIN_DELAY = 100
PADDLE_STICK_MAX_DELAY = 300
PADDLE_WRONG_DIRECTION_CHANCE = 10
PADDLE_MOMENTUM_MULTIPLIER = 4
BALL_COLLISION_ANGLE_VARIATION = 8
BALL_RELEASE_RANDOM_VARIATION = 6
```
**Best for:** Watching the ball "think" before each move

---

## 🔧 How to Modify

1. Open **[src/PCMonitor_WifiPortal.cpp](src/PCMonitor_WifiPortal.cpp)**
2. Navigate to **line 411** (Configuration section)
3. Change the desired values
4. Build firmware: `"C:\Users\rafal\.platformio\penv\Scripts\platformio.exe" run`
5. Flash to ESP32

---

## ❓ Troubleshooting

### Digits not changing / Time falling behind ⚠️ CRITICAL
- **Symptom:** Minute or hour digits stuck, clock falls 1-2 minutes behind
- **Cause:** Ball hitting other digits, not hitting the digit that needs to change
- **Fix:** `DIGIT_TRANSITION_TIMEOUT` auto-completes after 5 seconds (default) ✅
- **Already fixed in current version!**

### Ball gets stuck in vertical loop
- ✅ Increase `PADDLE_STICK_MAX_DELAY` (try 50-100ms)
- ✅ Default settings (stick delay 0-30ms) should already prevent this

### Ball doesn't reach all areas
- ✅ Increase `PADDLE_MOMENTUM_MULTIPLIER` (try 4-5)
- ✅ Increase `BALL_COLLISION_ANGLE_VARIATION` (try 10-15)
- ⚠️ Can enable `PADDLE_WRONG_DIRECTION_CHANCE` (try 10-15%) but may cause paddle shaking

### Paddle shaking violently
- **Cause:** `PADDLE_WRONG_DIRECTION_CHANCE` > 0
- **Fix:** Set `PADDLE_WRONG_DIRECTION_CHANCE = 0` (default) ✅
- ❌ Do NOT add paddle sway (causes continuous shaking)

### Gameplay too slow
- ✅ Decrease `PADDLE_STICK_MAX_DELAY` (try 10-20ms)
- ✅ Increase `PONG_BALL_SPEED_NORMAL` (try 24)

### Too chaotic/unpredictable
- ✅ Decrease `BALL_COLLISION_ANGLE_VARIATION` (try 3)
- ✅ Decrease `PADDLE_MOMENTUM_MULTIPLIER` (try 2)
- ✅ Increase `PADDLE_STICK_MIN_DELAY` (try 20-50ms for consistency)

---

## 📝 Notes

- All changes require recompiling and flashing firmware
- Test changes incrementally (change one value at a time)
- Flash/RAM usage: ~77% / ~14% with current settings
- Refresh rate: 50 FPS (20ms update interval)

---

**Last Updated:** 2025-12-25
**Firmware Version:** 1.3.x
**Author:** Breakout Clock Implementation
