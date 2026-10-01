/*
 * SmallOLED-PCMonitor - Game Mode
 *
 * Forced display mode that pairs a BLE gamepad and runs a game on the OLED.
 * Entered via /api/game/start (or the web UI), left via the pad's View
 * button from the pause / game-over screen, a touch-button tap, or
 * /api/game/stop. Leaving stops BLE scanning and drops the pad.
 */

#ifndef GAME_MODE_H
#define GAME_MODE_H

#include <Arduino.h>
#include "gamepad.h"

#define GAME_REFRESH_HZ 30

void gameModeStart();
void gameModeStop();
bool gameModeActive();
// Draws one frame into the display buffer (caller clears and pushes it).
void displayGameMode();

// Falling Blocks
void blocksReset();
// Advances the game by the input snapshot. Returns false once the player asked to quit.
bool blocksFrame(const GamepadState &in, bool padLost);

#endif // GAME_MODE_H
