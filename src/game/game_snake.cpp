/*
 * SmallOLED-PCMonitor - Snake (game mode)
 *
 * Same grid as Falling Blocks: 32x13 cells of 4px (3x3 blocks) from y=12,
 * with a line under the top band marking the upper wall. Walls and the
 * snake's own body are fatal. Each food grows the snake by two and speeds
 * it up a little; the food blinks so it never reads as a body segment.
 * Up to two turns are buffered, so a quick corner is never swallowed.
 *
 * Controls: d-pad / left stick steer.
 */

#include "../config/user_config.h"

#if GAMEPAD_ENABLED

#include "../config/config.h"
#include "../display/display.h"
#include "game_mode.h"
#include "game_common.h"

#define SN_W 32
#define SN_H 13
#define SN_CELL 4
#define SN_Y 12
#define SN_MAX (SN_W * SN_H)
#define SN_START_MS 170
#define SN_MIN_MS 70
#define SN_SPEEDUP_MS 4
#define SN_GROW 2

static uint8_t segX[SN_MAX], segY[SN_MAX];  // ring buffer, head at `head`
static uint16_t head, len, grow;
static uint32_t occ[SN_H];
static int8_t dirX, dirY;
static int8_t queueX[2], queueY[2];
static uint8_t queued;
static uint8_t foodX, foodY;
static uint16_t stepMs;
static unsigned long lastStep;
static uint32_t score, hiScore;
static GamePhase phase;
static bool newHi;

static bool occupied(int x, int y) { return occ[y] & (1u << x); }

static void placeFood() {
  uint16_t free = SN_MAX - len;
  if (!free) return;
  uint16_t pick = esp_random() % free;
  for (int y = 0; y < SN_H; y++)
    for (int x = 0; x < SN_W; x++)
      if (!occupied(x, y) && pick-- == 0) {
        foodX = x;
        foodY = y;
        return;
      }
}

void snakeReset() {
  hiScore = gameLoadHi("snakeHi");
  memset(occ, 0, sizeof(occ));
  len = 4;
  head = len - 1;
  for (uint16_t i = 0; i < len; i++) {
    segX[i] = 6 + i;
    segY[i] = SN_H / 2;
    occ[SN_H / 2] |= 1u << segX[i];
  }
  dirX = 1;
  dirY = 0;
  queued = 0;
  grow = 0;
  stepMs = SN_START_MS;
  score = 0;
  newHi = false;
  placeFood();
  phase = G_READY;
}

static void die() {
  phase = G_OVER;
  gameRumble(80, 60, 400);
  newHi = gameSubmitScore("snakeHi", score);
  if (newHi) hiScore = score;
}

static void queueTurn(int8_t dx, int8_t dy) {
  int8_t lx = queued ? queueX[queued - 1] : dirX;
  int8_t ly = queued ? queueY[queued - 1] : dirY;
  if ((dx == lx && dy == ly) || (dx == -lx && dy == -ly) || queued >= 2) return;
  queueX[queued] = dx;
  queueY[queued] = dy;
  queued++;
}

static void step() {
  if (queued) {
    dirX = queueX[0];
    dirY = queueY[0];
    queueX[0] = queueX[1];
    queueY[0] = queueY[1];
    queued--;
  }
  int nx = segX[head] + dirX, ny = segY[head] + dirY;
  if (nx < 0 || nx >= SN_W || ny < 0 || ny >= SN_H) return die();

  if (grow) {
    grow--;
  } else {
    uint16_t tail = (head + SN_MAX - len + 1) % SN_MAX;
    occ[segY[tail]] &= ~(1u << segX[tail]);
    len--;
  }
  if (occupied(nx, ny)) return die();

  head = (head + 1) % SN_MAX;
  segX[head] = nx;
  segY[head] = ny;
  occ[ny] |= 1u << nx;
  len++;

  if (nx == foodX && ny == foodY) {
    score += 10;
    grow += SN_GROW;
    if (stepMs > SN_MIN_MS) stepMs -= SN_SPEEDUP_MS;
    gameRumble(0, 30, 50);
    if (len >= SN_MAX) return die();  // filled the board - nowhere left to go
    placeFood();
  }
}

static void draw(unsigned long now) {
  display.drawFastHLine(0, SN_Y - 2, SCREEN_WIDTH, DISPLAY_WHITE);
  for (uint16_t i = 0; i < len; i++) {
    uint16_t k = (head + SN_MAX - i) % SN_MAX;
    display.fillRect(segX[k] * SN_CELL, SN_Y + segY[k] * SN_CELL, 3, 3, DISPLAY_WHITE);
  }
  if ((now / 200) % 4) display.drawRect(foodX * SN_CELL, SN_Y + foodY * SN_CELL, 3, 3, DISPLAY_WHITE);
  gameDrawScore(0, score);
  gameDrawClock();
}

bool snakeFrame(const GamepadState &in, bool padLost) {
  unsigned long now = millis();
  switch (gameFlow(phase, in, padLost)) {
    case STEP_QUIT:
      return false;
    case STEP_RESTART:
      snakeReset();
      phase = G_PLAY;
      lastStep = now;
      break;
    case STEP_RESUMED:
      lastStep = now;
      break;
    case STEP_PLAY: {
      uint16_t p = in.pressed;
      if (p & GP_UP) queueTurn(0, -1);
      if (p & GP_DOWN) queueTurn(0, 1);
      if (p & GP_LEFT) queueTurn(-1, 0);
      if (p & GP_RIGHT) queueTurn(1, 0);
      while (phase == G_PLAY && now - lastStep >= stepMs) {
        lastStep += stepMs;
        step();
      }
      break;
    }
    case STEP_HOLD:
      break;
  }

  draw(now);
  char info[16];
  snprintf(info, sizeof(info), phase == G_READY ? "HI %lu" : "%lu pts",
           (unsigned long)(phase == G_READY ? hiScore : score));
  gameDrawOverlay(phase, padLost, "SNAKE", phase == G_PAUSED ? nullptr : info, newHi,
                  "d-pad/stick: steer\nfood grows+speeds");
  return true;
}

#endif // GAMEPAD_ENABLED
