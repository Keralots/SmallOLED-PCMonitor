/*
 * SmallOLED-PCMonitor - Game mode shared plumbing (see game_common.h)
 */

#include "../config/user_config.h"

#if GAMEPAD_ENABLED

#include "../config/config.h"
#include "../display/display.h"
#include "../clocks/clocks.h"
#include "game_common.h"
#include <Preferences.h>

GameStep gameFlow(GamePhase &phase, const GamepadState &in, bool padLost) {
  if (padLost) {
    if (phase == G_PLAY) phase = G_PAUSED;
    return STEP_HOLD;
  }
  uint16_t p = in.pressed;
  switch (phase) {
    case G_READY:
    case G_PAUSED:
      if (p & GP_VIEW) return STEP_QUIT;
      if (p & (GP_A | GP_MENU)) {
        phase = G_PLAY;
        return STEP_RESUMED;
      }
      return STEP_HOLD;
    case G_PLAY:
      if (p & (GP_MENU | GP_VIEW)) {
        phase = G_PAUSED;
        return STEP_HOLD;
      }
      return STEP_PLAY;
    case G_OVER:
      if (p & GP_VIEW) return STEP_QUIT;
      if (p & (GP_A | GP_MENU)) {
        phase = G_PLAY;
        return STEP_RESTART;
      }
      return STEP_HOLD;
  }
  return STEP_HOLD;
}

void gamePrintCentered(int y, const char *s) {
  display.setCursor(64 - strlen(s) * 3, y);
  display.print(s);
}

// READY with controls: a taller box over the whole field.
static void drawReadyHelp(const char *title, const char *info, const char *help) {
  char l1[21] = "", l2[21] = "";
  const char *nl = strchr(help, '\n');
  size_t n = nl ? (size_t)(nl - help) : strlen(help);
  snprintf(l1, sizeof(l1), "%.*s", (int)n, help);
  if (nl) snprintf(l2, sizeof(l2), "%s", nl + 1);
  display.fillRect(4, GAME_TOP + 1, 120, SCREEN_HEIGHT - GAME_TOP - 1, DISPLAY_BLACK);
  display.drawRect(4, GAME_TOP + 1, 120, SCREEN_HEIGHT - GAME_TOP - 1, DISPLAY_WHITE);
  gamePrintCentered(15, title);
  if (info) gamePrintCentered(25, info);
  display.drawFastHLine(10, 34, 108, DISPLAY_WHITE);
  gamePrintCentered(37, l1);
  gamePrintCentered(46, l2);
  if ((millis() / 500) % 3) gamePrintCentered(55, "A: start");
}

void gameDrawOverlay(GamePhase phase, bool padLost, const char *title, const char *info,
                     bool newHi, const char *help) {
  const char *l1, *l2 = info, *l3;
  if (padLost) {
    l1 = "PAD LOST";
    l2 = "reconnecting";
    l3 = (millis() / 400) % 2 ? "..." : nullptr;
  } else {
    switch (phase) {
      case G_READY:
        if (help) {
          display.setTextSize(1);
          display.setTextColor(DISPLAY_WHITE);
          drawReadyHelp(title, info, help);
          return;
        }
        l1 = title;
        l3 = "A: start";
        break;
      case G_PAUSED:
        l1 = "PAUSED";
        if (!l2) l2 = "MENU: resume";
        l3 = "VIEW: menu";
        break;
      case G_OVER:
        l1 = newHi ? "NEW HI SCORE" : "GAME OVER";
        l3 = "A: again";
        break;
      default:
        return;
    }
  }
  display.setTextSize(1);
  display.setTextColor(DISPLAY_WHITE);
  display.fillRect(18, 18, 92, 34, DISPLAY_BLACK);
  display.drawRect(18, 18, 92, 34, DISPLAY_WHITE);
  const char *ls[] = {l1, l2, l3};
  for (int i = 0; i < 3; i++)
    if (ls[i]) gamePrintCentered(22 + i * 10, ls[i]);
  if (phase == G_PAUSED && !padLost) gameDrawBattery(92, 21);
}

void gameDrawScore(int x, uint32_t score) {
  display.setTextSize(1);
  display.setTextColor(DISPLAY_WHITE);
  display.setCursor(x, 1);
  display.print(score);
}

void gameDrawBattery(int x, int y) {
  uint8_t level = gamepadBattery();
  if (!level) return;
  display.fillRect(x - 1, y - 1, 15, 8, DISPLAY_BLACK);
  display.drawRect(x, y, 12, 6, DISPLAY_WHITE);
  display.fillRect(x + 12, y + 2, 1, 2, DISPLAY_WHITE);
  int w = (level * 8 + 50) / 100;
  if (w < 1) w = 1;
  display.fillRect(x + 2, y + 2, w, 2, DISPLAY_WHITE);
}

void gameDrawClock() {
  uint8_t level = gamepadBattery();
  if (level && level <= 15 && (millis() / 700) % 3 == 0) {
    if ((millis() / 350) % 2) gameDrawBattery(SCREEN_WIDTH - 22, 2);
    return;
  }
  struct tm t;
  if (!peekLocalTime(&t)) return;
  int h, m;
  bool pm;
  formatTimeForDisplay(t.tm_hour, t.tm_min, h, m, pm);
  char buf[6];
  snprintf(buf, sizeof(buf), "%02d%c%02d", h, shouldShowColon() ? ':' : ' ', m);
  display.setTextSize(1);
  display.setTextColor(DISPLAY_WHITE);
  display.setCursor(SCREEN_WIDTH - 31, 1);
  display.print(buf);
}

void gameRumble(uint8_t strong, uint8_t weak, uint16_t ms) {
  if (settings.gameRumble) gamepadRumble(strong, weak, ms);
}

uint32_t gameLoadHi(const char *key) {
  Preferences p;
  if (!p.begin("game", true)) return 0;
  uint32_t v = p.getUInt(key, 0);
  p.end();
  return v;
}

void gameStoreHi(const char *key, uint32_t score) {
  Preferences p;
  if (!p.begin("game", false)) return;
  if (score) p.putUInt(key, score);
  else if (p.isKey(key)) p.remove(key);
  p.end();
}

bool gameSubmitScore(const char *key, uint32_t score) {
  if (score <= gameLoadHi(key)) return false;
  gameStoreHi(key, score);
  return true;
}

float gameDt(unsigned long &last, unsigned long now) {
  float dt = (now - last) / 1000.0f;
  last = now;
  return dt > 0.1f ? 0.1f : dt;
}

float gameRandf(float lo, float hi) { return lo + (hi - lo) * (esp_random() % 10001) / 10000.0f; }

#endif // GAMEPAD_ENABLED
