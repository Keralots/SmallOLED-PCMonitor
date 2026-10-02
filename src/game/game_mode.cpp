/*
 * SmallOLED-PCMonitor - Game Mode
 *
 * Owns the pad lifecycle around the games: a pairing screen until a pad
 * connects, then the game menu (last pick remembered) and the chosen game,
 * with a pause overlay while the pad is gone. Gives up (and frees the radio) if no pad shows up, a lost pad
 * does not come back, or nobody touches the pad for gameIdleExitMin minutes.
 * The panel is held at normal brightness for the whole time (no night
 * schedule) and handed back to the schedule on exit.
 */

#include "../config/user_config.h"

#if GAMEPAD_ENABLED

#include "../config/config.h"
#include "../display/display.h"
#include "game_mode.h"
#include "game_common.h"
#include <Preferences.h>

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

struct GameDef {
  const char *name;
  const char *hiKey;
  void (*reset)();
  bool (*frame)(const GamepadState &, bool);
};
static const GameDef GAMES[] = {
    {"Falling Blocks", "blocksHi", blocksReset, blocksFrame},
    {"Snake", "snakeHi", snakeReset, snakeFrame},
    {"Bricks", "bricksHi", bricksReset, bricksFrame},
    {"Space Rocks", "rocksHi", rocksReset, rocksFrame},
    {"Runner", "runnerHi", runnerReset, runnerFrame},
    {"Defenders", "defendersHi", defendersReset, defendersFrame},
};
static const uint8_t GAME_COUNT = sizeof(GAMES) / sizeof(GAMES[0]);
#define MENU_ROWS 5

static int8_t current = -1;  // running game, -1 = menu
static uint8_t selected = 0;
static uint32_t menuHi[GAME_COUNT];

static void openMenu() {
  current = -1;
  for (uint8_t i = 0; i < GAME_COUNT; i++) menuHi[i] = gameLoadHi(GAMES[i].hiKey);
}

uint8_t gameCount() { return GAME_COUNT; }
const char *gameName(uint8_t i) { return i < GAME_COUNT ? GAMES[i].name : ""; }
const char *gameHiKey(uint8_t i) { return i < GAME_COUNT ? GAMES[i].hiKey : ""; }

void gameResetHi(int i) {
  for (uint8_t k = 0; k < GAME_COUNT; k++) {
    if (i >= 0 && k != i) continue;
    gameStoreHi(GAMES[k].hiKey, 0);
    menuHi[k] = 0;
  }
}

static void launchGame(uint8_t i) {
  current = i;
  GAMES[i].reset();
  Preferences p;
  p.begin("game", false);
  p.putUChar("lastGame", i);
  p.end();
}

void gameModeStart() {
  if (active) return;
  active = true;
  everConnected = false;
  lostAt = 0;
  enteredAt = millis();
  lastInputAt = enteredAt;
  lastButtons = 0;
  setDisplayGameOverride(true);
  Preferences p;
  selected = p.begin("game", true) ? p.getUChar("lastGame", 0) : 0;
  p.end();
  if (selected >= GAME_COUNT) selected = 0;
  openMenu();
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

static void drawPairingScreen(GamepadLink link, unsigned long now) {
  display.setTextSize(1);
  display.setTextColor(DISPLAY_WHITE);
  gamePrintCentered(0, "GAME MODE");
  display.drawFastHLine(0, 10, 128, DISPLAY_WHITE);
  gamePrintCentered(14, "New pad: hold pair");
  gamePrintCentered(24, "button for 3 s");
  gamePrintCentered(34, "Paired: press Xbox");

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

// Game list under the band; d-pad picks, A starts, View leaves game mode.
static bool menuFrame(const GamepadState &in, bool padLost) {
  if (!padLost) {
    if (in.pressed & GP_UP) selected = (selected + GAME_COUNT - 1) % GAME_COUNT;
    if (in.pressed & GP_DOWN) selected = (selected + 1) % GAME_COUNT;
    if (in.pressed & GP_VIEW) return false;
    if (in.pressed & (GP_A | GP_MENU)) {
      launchGame(selected);
      return true;
    }
  }

  display.setTextSize(1);
  display.setTextColor(DISPLAY_WHITE);
  display.setCursor(0, 1);
  display.print("GAMES");
  gameDrawBattery(SCREEN_WIDTH - 50, 2);
  gameDrawClock();
  display.drawFastHLine(0, 10, SCREEN_WIDTH, DISPLAY_WHITE);
  uint8_t first = selected < MENU_ROWS ? 0 : selected - MENU_ROWS + 1;
  for (uint8_t r = 0; r < MENU_ROWS && first + r < GAME_COUNT; r++) {
    uint8_t i = first + r;
    int y = 12 + r * 10;
    bool sel = i == selected;
    if (sel) display.fillRect(0, y, SCREEN_WIDTH - 3, 10, DISPLAY_WHITE);
    display.setTextColor(sel ? DISPLAY_BLACK : DISPLAY_WHITE);
    display.setCursor(3, y + 1);
    display.print(GAMES[i].name);
    if (menuHi[i]) {
      char hi[11];
      snprintf(hi, sizeof(hi), "%lu", (unsigned long)menuHi[i]);
      display.setCursor(SCREEN_WIDTH - 6 - strlen(hi) * 6, y + 1);
      display.print(hi);
    }
  }
  if (GAME_COUNT > MENU_ROWS) {
    int h = 50 * MENU_ROWS / GAME_COUNT;
    int y = 12 + (50 - h) * first / (GAME_COUNT - MENU_ROWS);
    display.fillRect(SCREEN_WIDTH - 2, y, 2, h, DISPLAY_WHITE);
  }
  display.setTextColor(DISPLAY_WHITE);
  if (padLost) gameDrawOverlay(G_PAUSED, true, nullptr, nullptr, false);
  return true;
}

// Runs the menu or the current game; a game handing back false returns to the menu.
static bool runFrame(const GamepadState &in, bool padLost) {
  if (current < 0) return menuFrame(in, padLost);
  if (!GAMES[current].frame(in, padLost)) openMenu();
  return true;
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
    if (!runFrame(in, false)) {
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
  runFrame(in, true);
  if (now - lostAt >= GAME_LOST_TIMEOUT_MS) gameModeStop();
}

#endif // GAMEPAD_ENABLED
