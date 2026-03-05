# v1.5.2 - Changelog

## New Features

### Mario Clock - Idle Enemy Encounters
- Between minute changes, Mario encounters enemies during the ~56-second idle gap
- **Enemy types:**
  - **Goomba** — Mario jumps on top to squash (classic stomp)
  - **Spiny** — Mario stops and shoots a fireball (can't stomp spiny!)
  - **Koopa Troopa** — Mario jumps on it, shell slides off-screen
- **Power-ups (rare):**
  - **Star** — bounces out of a digit block, Mario catches it, runs off-screen with speed boost and flicker effect (+5 coins)
  - **Mushroom** — slides out of an hour digit, Mario chases and catches it, becomes Big Mario until off-screen (+3 coins)
- **Other encounter types:**
  - **Coin Blocks** — Mario jumps to hit digit blocks from below, coins pop out
  - **Multi-Enemy** — two Goombas appear at once
  - **Pass-By** — enemy walks past without interaction (ambient life)
- **Coin counter** displayed in top-left corner (2-digit, persists across encounters)
- All encounters auto-abort at second :56 to ensure minute-change animation always has priority
- Procedural NES-style sprites (Goomba, Spiny, Koopa, Star, Mushroom, Big Mario) — no bitmap storage needed
- Animations run at ~60fps for smooth movement (encounter framerate boost)
- Disabled by default — enable via web interface: Mario Clock Settings > Idle Encounters

### Encounter Configuration (Web Interface)
- **Idle Encounters** checkbox to enable/disable
- **Encounter Frequency** dropdown:
  - Rare (25-35s)
  - Normal (15-25s)
  - Frequent (8-15s)
  - Chaotic (2-5s)
- **Encounter Speed** dropdown: Slow / Normal / Fast

### Timezone Database Additions
- Added **Pakistan (Karachi, Islamabad)** timezone (UTC+5, no DST)
- Added **Central Asia (Tashkent, Uzbekistan)** timezone (UTC+5, no DST)
- Added UTC+5 default mapping for automatic migration from old GMT offset format

## Bug Fixes

- Fixed UTC+5 timezone migration — devices with gmtOffset=300 now correctly auto-migrate to Pakistan timezone

## Documentation

- Updated README with optional wiring details
- Updated README with assembly video links and tips

## Configuration Changes

- `config.h`: Added `marioIdleEncounters` (bool), `marioEncounterFreq` (uint8_t 0-3), `marioEncounterSpeed` (uint8_t 0-2) to Settings struct
- `config.h`: Added `EnemyType`, `EnemyState` enums, `MarioEnemy`, `MarioFireball` structs
- `config.h`: Added `ENCOUNTER_ANIM_SPEED` (16ms) and `ENCOUNTER_TIME_SCALE` constants
- `settings.cpp`: Added persistence for encounter settings (NVS keys: `marioEnctr`, `marioEncFrq`, `marioEncSpd`)
- `clock_mario.cpp`: ~1000+ lines of encounter logic, state machine, sprite rendering
- `clock_globals.cpp/h`: Added encounter global variables
- `animation_detection.cpp`: Encounter states trigger refresh rate boost
