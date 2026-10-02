/*
 * SmallOLED-PCMonitor - Game mode shared plumbing
 *
 * Every game follows the same flow: READY (A starts) -> PLAY -> PAUSED
 * (Menu/View pause, Menu/A resume) -> OVER (A restarts). View from READY,
 * PAUSED or OVER goes back to the game menu. Losing the pad pauses.
 * The top 10px band carries the score on the left and HH:MM on the right.
 */

#ifndef GAME_COMMON_H
#define GAME_COMMON_H

#include <Arduino.h>
#include "gamepad.h"

#define GAME_TOP 11  // first pixel row below the top band

enum GamePhase : uint8_t { G_READY, G_PLAY, G_PAUSED, G_OVER };

enum GameStep : uint8_t {
  STEP_HOLD,     // not playing this frame - just draw
  STEP_PLAY,     // run the game with this frame's input
  STEP_RESUMED,  // play (re)starts now: reset frame timers, ignore this input
  STEP_RESTART,  // start a fresh round
  STEP_QUIT,     // back to the game menu
};

GameStep gameFlow(GamePhase &phase, const GamepadState &in, bool padLost);
// Box over the field for every phase but PLAY. `info` is an optional second line.
void gameDrawOverlay(GamePhase phase, bool padLost, const char *title, const char *info,
                     bool newHi);
void gameDrawScore(int x, uint32_t score);
void gameDrawClock();
void gamePrintCentered(int y, const char *s);
void gameRumble(uint8_t strong, uint8_t weak, uint16_t ms);
uint32_t gameLoadHi(const char *key);
// Saves the score when it beats the stored best; returns true on a new best.
bool gameSubmitScore(const char *key, uint32_t score);
// Seconds since `last` (capped so a stall cannot teleport anything), updates `last`.
float gameDt(unsigned long &last, unsigned long now);
float gameRandf(float lo, float hi);

#endif // GAME_COMMON_H
