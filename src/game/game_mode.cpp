/*
 * SmallOLED-PCMonitor - Game Mode
 *
 * Owns the pad lifecycle around the game: a pairing screen until a pad
 * connects, the game while it is connected, and a pause overlay while it is
 * gone. Gives up (and frees the radio) if no pad shows up or a lost pad
 * does not come back.
 */

#include "../config/user_config.h"

#if GAMEPAD_ENABLED

#include "../config/config.h"
#include "../display/display.h"
#include "game_mode.h"

#define GAME_PAIR_TIMEOUT_MS 120000UL
#define GAME_LOST_TIMEOUT_MS 60000UL

static bool active = false;
static bool everConnected = false;
static unsigned long enteredAt = 0;
static unsigned long lostAt = 0;

void gameModeStart() {
  if (active) return;
  active = true;
  everConnected = false;
  lostAt = 0;
  enteredAt = millis();
  blocksReset();
  gamepadStart();
  Serial.println("Game mode: started");
}

void gameModeStop() {
  if (!active) return;
  active = false;
  gamepadStop();
  Serial.println("Game mode: stopped");
}

bool gameModeActive() { return active; }

static void printCentered(int y, const char *s) {
  display.setCursor(64 - strlen(s) * 3, y);
  display.print(s);
}

static void drawPairingScreen(GamepadLink link, unsigned long now) {
  display.setTextSize(1);
  display.setTextColor(DISPLAY_WHITE);
  printCentered(0, "GAME MODE");
  display.drawFastHLine(0, 10, 128, DISPLAY_WHITE);
  printCentered(14, "New pad: hold pair");
  printCentered(24, "button for 3 s");
  printCentered(34, "Paired: press Xbox");

  char status[22];
  uint8_t dots = (now / 400) % 4;
  const char *what = link == GP_LINK_CONNECTING ? "Pairing" : "Searching";
  snprintf(status, sizeof(status), "%s%.*s", what, dots, "...");
  display.setCursor(0, 54);
  display.print(status);
  unsigned long left = (GAME_PAIR_TIMEOUT_MS - (now - enteredAt)) / 1000;
  display.setCursor(104, 54);
  display.print(left);
  display.print("s");
}

void displayGameMode() {
  unsigned long now = millis();
  GamepadLink link = gamepadLink();
  GamepadState in;
  gamepadRead(&in);

  if (link == GP_LINK_CONNECTED) {
    everConnected = true;
    lostAt = 0;
    if (!blocksFrame(in, false)) gameModeStop();
    return;
  }

  if (!everConnected) {
    if (now - enteredAt >= GAME_PAIR_TIMEOUT_MS) {
      gameModeStop();
      return;
    }
    drawPairingScreen(link, now);
    return;
  }

  if (!lostAt) lostAt = now;
  blocksFrame(in, true);
  if (now - lostAt >= GAME_LOST_TIMEOUT_MS) gameModeStop();
}

#endif // GAMEPAD_ENABLED
