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

void gameDrawOverlay(GamePhase phase, bool padLost, const char *title, const char *info,
                     bool newHi) {
  const char *l1, *l2 = info, *l3;
  if (padLost) {
    l1 = "PAD LOST";
    l2 = "reconnecting";
    l3 = (millis() / 400) % 2 ? "..." : nullptr;
  } else {
    switch (phase) {
      case G_READY:
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
}

void gameDrawScore(int x, uint32_t score) {
  display.setTextSize(1);
  display.setTextColor(DISPLAY_WHITE);
  display.setCursor(x, 1);
  display.print(score);
}

void gameDrawClock() {
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

bool gameSubmitScore(const char *key, uint32_t score) {
  if (score <= gameLoadHi(key)) return false;
  Preferences p;
  p.begin("game", false);
  p.putUInt(key, score);
  p.end();
  return true;
}

float gameDt(unsigned long &last, unsigned long now) {
  float dt = (now - last) / 1000.0f;
  last = now;
  return dt > 0.1f ? 0.1f : dt;
}

float gameRandf(float lo, float hi) { return lo + (hi - lo) * (esp_random() % 10001) / 10000.0f; }

#endif // GAMEPAD_ENABLED
