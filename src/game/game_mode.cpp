/*
 * SmallOLED-PCMonitor - Game Mode
 *
 * Owns the pad lifecycle around the game: a pairing screen until a pad
 * connects, the game while it is connected, and a pause overlay while it is
 * gone. Gives up (and frees the radio) if no pad shows up, a lost pad
 * does not come back, or nobody touches the pad for gameIdleExitMin minutes.
 * The panel is held at normal brightness for the whole time (no night
 * schedule) and handed back to the schedule on exit.
 */

#include "../config/user_config.h"

#if GAMEPAD_ENABLED

#include "../config/config.h"
#include "../display/display.h"
#include "game_mode.h"

#define GAME_PAIR_TIMEOUT_MS 120000UL
#define GAME_LOST_TIMEOUT_MS 60000UL
#define GAME_STICK_ACTIVITY 8000   // deflection that counts as input for the idle timer
#define GAME_TRIGGER_ACTIVITY 100

static bool active = false;
static bool everConnected = false;
static unsigned long enteredAt = 0;
static unsigned long lostAt = 0;
static unsigned long lastInputAt = 0;
static uint16_t lastButtons = 0;

void gameModeStart() {
  if (active) return;
  active = true;
  everConnected = false;
  lostAt = 0;
  enteredAt = millis();
  lastInputAt = enteredAt;
  lastButtons = 0;
  setDisplayGameOverride(true);
  blocksReset();
  gamepadStart();
  Serial.println("Game mode: started");
}

void gameModeStop() {
  if (!active) return;
  active = false;
  gamepadStop();
  setDisplayGameOverride(false);
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
    if (!everConnected || lostAt) lastInputAt = now;
    everConnected = true;
    lostAt = 0;
    if (in.pressed || in.buttons != lastButtons || abs(in.rx) > GAME_STICK_ACTIVITY ||
        abs(in.ry) > GAME_STICK_ACTIVITY || in.lt > GAME_TRIGGER_ACTIVITY ||
        in.rt > GAME_TRIGGER_ACTIVITY)
      lastInputAt = now;
    lastButtons = in.buttons;
    if (!blocksFrame(in, false)) {
      gameModeStop();
    } else if (settings.gameIdleExitMin &&
               now - lastInputAt >= settings.gameIdleExitMin * 60000UL) {
      Serial.println("Game mode: idle timeout");
      gameModeStop();
    }
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
