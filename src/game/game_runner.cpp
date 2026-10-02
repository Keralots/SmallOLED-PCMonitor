/*
 * SmallOLED-PCMonitor - Runner (game mode)
 *
 * Endless runner. A small pixel runner stays at a fixed x while the ground
 * scrolls; posts of varying height must be jumped, low-flying birds ducked
 * (high ones can be run under). Holding jump gives a higher arc, down while
 * airborne drops faster. The world speeds up over time; the score is the
 * distance run.
 *
 * Controls: A / up jump (hold for height), down duck.
 */

#include "../config/user_config.h"

#if GAMEPAD_ENABLED

#include "../config/config.h"
#include "../display/display.h"
#include "game_mode.h"
#include "game_common.h"

#define GROUND_Y 58          // feet rest here
#define RUN_X 14
#define RUN_W 8
#define RUN_H 10
#define DUCK_H 6
#define RUNNER_GRAVITY 820.0f // px/s^2
#define JUMP_V -180.0f
#define HOLD_GRAVITY 0.6f    // gravity factor while jump is held and rising (20px tap, 33px held)
#define FAST_FALL 2.2f
#define SPEED_START 70.0f
#define SPEED_GAIN 2.2f      // px/s per second
#define SPEED_MAX 165.0f
#define OBS_MAX 4

// Sprites, 8px wide, bit 7 = left. The runner faces right.
static const uint8_t RUN_A[RUN_H] = {0x1C, 0x1C, 0x08, 0x3E, 0x5D, 0x1C, 0x14, 0x22, 0x41, 0x41};
static const uint8_t RUN_B[RUN_H] = {0x1C, 0x1C, 0x08, 0x3E, 0x5D, 0x1C, 0x14, 0x14, 0x24, 0x22};
static const uint8_t RUN_JUMP[RUN_H] = {0x1C, 0x1C, 0x49, 0x3E, 0x1C, 0x1C, 0x14, 0x22, 0x14, 0x00};
static const uint8_t RUN_DUCK[DUCK_H] = {0x07, 0x07, 0xFE, 0x7C, 0x48, 0x84};

enum ObsType : uint8_t { OBS_POST, OBS_BIRD_LOW, OBS_BIRD_HIGH };
struct Obstacle {
  float x;
  uint8_t w, h, type;
  bool alive;
};

static Obstacle obs[OBS_MAX];
static float runY, runVY, speed, dist, nextGap, groundPhase;
static bool ducking, airborne;
static uint32_t hiScore;
static GamePhase phase;
static bool newHi;
static unsigned long lastTick;

static uint32_t score() { return (uint32_t)(dist / 10); }

void runnerReset() {
  hiScore = gameLoadHi("runnerHi");
  for (auto &o : obs) o.alive = false;
  runY = GROUND_Y;
  runVY = 0;
  speed = SPEED_START;
  dist = 0;
  nextGap = 90;
  groundPhase = 0;
  ducking = airborne = false;
  newHi = false;
  phase = G_READY;
}

static void spawnObstacle() {
  for (auto &o : obs) {
    if (o.alive) continue;
    uint8_t roll = esp_random() % 10;
    if (speed > 95 && roll >= 7) {
      o = {SCREEN_WIDTH + 2.0f, 9, 5, (uint8_t)(roll == 9 ? OBS_BIRD_HIGH : OBS_BIRD_LOW), true};
    } else {
      o = {SCREEN_WIDTH + 2.0f, (uint8_t)(3 + esp_random() % 4), (uint8_t)(7 + esp_random() % 7), OBS_POST, true};
    }
    return;
  }
}

// Obstacle box: posts stand on the ground; low birds fly at head height
// (duck under), high birds above a standing runner (only a jump hits them).
static void obsBox(const Obstacle &o, int &x, int &y, int &w, int &h) {
  x = (int)o.x;
  w = o.w;
  h = o.h;
  if (o.type == OBS_POST) y = GROUND_Y - o.h;
  else if (o.type == OBS_BIRD_LOW) y = GROUND_Y - RUN_H - 2;
  else y = GROUND_Y - RUN_H - 12;
}

static void die() {
  phase = G_OVER;
  gameRumble(90, 60, 350);
  newHi = gameSubmitScore("runnerHi", score());
  if (newHi) hiScore = score();
}

static void update(const GamepadState &in, float dt) {
  bool jumpHeld = in.buttons & (GP_A | GP_UP);
  ducking = (in.buttons & GP_DOWN) && !airborne;

  if (!airborne && (in.pressed & (GP_A | GP_UP))) {
    airborne = true;
    runVY = JUMP_V;
    gameRumble(0, 15, 25);
  }
  if (airborne) {
    float g = RUNNER_GRAVITY;
    if (runVY < 0 && jumpHeld) g *= HOLD_GRAVITY;
    if (in.buttons & GP_DOWN) g *= FAST_FALL;
    runVY += g * dt;
    runY += runVY * dt;
    if (runY >= GROUND_Y) {
      runY = GROUND_Y;
      runVY = 0;
      airborne = false;
    }
  }

  speed = min(speed + SPEED_GAIN * dt, SPEED_MAX);
  float dx = speed * dt;
  dist += dx;
  groundPhase = fmodf(groundPhase + dx, 8.0f);

  nextGap -= dx;
  if (nextGap <= 0) {
    spawnObstacle();
    nextGap = gameRandf(55, 120) + speed * 0.35f;
  }

  int rh = ducking ? DUCK_H : RUN_H;
  int rx = RUN_X + 1, ry = (int)runY - rh + 1, rw = RUN_W - 2;
  for (auto &o : obs) {
    if (!o.alive) continue;
    o.x -= dx;
    if (o.x + o.w < 0) {
      o.alive = false;
      continue;
    }
    int ox, oy, ow, oh;
    obsBox(o, ox, oy, ow, oh);
    if (rx < ox + ow && rx + rw > ox && ry < oy + oh && ry + rh - 1 > oy) {
      die();
      return;
    }
  }
}

static void drawSprite(const uint8_t *rows, int h, int x, int yBottom) {
  for (int r = 0; r < h; r++)
    for (int c = 0; c < 8; c++)
      if (rows[r] & (0x80 >> c)) display.drawPixel(x + c, yBottom - h + 1 + r, DISPLAY_WHITE);
}

static void draw(unsigned long now) {
  // Ground: solid line with scrolling dashes below for a sense of speed.
  display.drawFastHLine(0, GROUND_Y + 1, SCREEN_WIDTH, DISPLAY_WHITE);
  for (int x = -(int)groundPhase; x < SCREEN_WIDTH; x += 8) display.drawFastHLine(x, GROUND_Y + 3, 3, DISPLAY_WHITE);

  for (const auto &o : obs) {
    if (!o.alive) continue;
    int x, y, w, h;
    obsBox(o, x, y, w, h);
    if (o.type == OBS_POST) {
      display.fillRect(x, y, w, h, DISPLAY_WHITE);
    } else {
      bool up = (now / 150) % 2;  // flapping
      display.drawFastHLine(x + 2, y + 2, 5, DISPLAY_WHITE);
      display.drawPixel(x + 8, y + 2, DISPLAY_WHITE);
      display.drawLine(x, up ? y : y + 4, x + 3, y + 2, DISPLAY_WHITE);
      display.drawLine(x + 4, y + 2, x + 6, up ? y : y + 4, DISPLAY_WHITE);
    }
  }

  const uint8_t *spr = RUN_JUMP;
  int h = RUN_H;
  if (ducking) {
    spr = RUN_DUCK;
    h = DUCK_H;
  } else if (!airborne) {
    spr = (phase == G_PLAY && (int)(dist / 9) % 2) ? RUN_B : RUN_A;
  }
  drawSprite(spr, h, RUN_X, (int)runY);

  gameDrawScore(0, score());
  gameDrawClock();
}

bool runnerFrame(const GamepadState &in, bool padLost) {
  unsigned long now = millis();
  switch (gameFlow(phase, in, padLost)) {
    case STEP_QUIT:
      return false;
    case STEP_RESTART:
      runnerReset();
      phase = G_PLAY;
      lastTick = now;
      break;
    case STEP_RESUMED:
      lastTick = now;
      break;
    case STEP_PLAY:
      update(in, gameDt(lastTick, now));
      break;
    case STEP_HOLD:
      break;
  }

  draw(now);
  char info[16];
  snprintf(info, sizeof(info), phase == G_READY ? "HI %lu" : "%lu m",
           (unsigned long)(phase == G_READY ? hiScore : score()));
  gameDrawOverlay(phase, padLost, "RUNNER", phase == G_PAUSED ? nullptr : info, newHi,
                  "A/up: jump (hold)\ndown: duck");
  return true;
}

#endif // GAMEPAD_ENABLED
