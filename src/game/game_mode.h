/*
 * SmallOLED-PCMonitor - Game Mode
 *
 * Forced display mode that pairs a BLE gamepad and runs games on the OLED.
 * Entered via /api/game/start, the web UI or a touch triple tap; after the
 * pad connects a menu picks the game. Left via View in the menu, a touch
 * tap, /api/game/stop, or the idle / no-pad timeouts. Leaving stops BLE
 * scanning and drops the pad.
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

// Game registry for the web UI: name and Preferences key of each game's best score.
uint8_t gameCount();
const char *gameName(uint8_t i);
const char *gameHiKey(uint8_t i);
// Clears one game's best score, or every game's with -1.
void gameResetHi(int i);

// Each game: reset to its READY screen; run one frame, false = back to the menu.
void blocksReset();
bool blocksFrame(const GamepadState &in, bool padLost);
void snakeReset();
bool snakeFrame(const GamepadState &in, bool padLost);
void bricksReset();
bool bricksFrame(const GamepadState &in, bool padLost);
void rocksReset();
bool rocksFrame(const GamepadState &in, bool padLost);
void runnerReset();
bool runnerFrame(const GamepadState &in, bool padLost);
void defendersReset();
bool defendersFrame(const GamepadState &in, bool padLost);

#endif // GAME_MODE_H
