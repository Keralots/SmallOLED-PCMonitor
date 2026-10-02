/*
 * SmallOLED-PCMonitor - Defenders (game mode)
 *
 * Fixed-shooter in the field under the top band. A 7x4 formation marches
 * side to side, drops a step at each edge and speeds up as it thins out;
 * the bottom alien of a column drops bombs, now and then aimed at the
 * cannon. Four bunkers soak up shots from both sides and get chewed away
 * pixel by pixel. A saucer crosses the top for a bonus. Only one cannon
 * shot is in the air at a time. Clearing a wave starts a lower, faster one
 * with fresh bunkers; the formation reaching the cannon ends the game.
 *
 * Controls: left stick (analog speed) or d-pad move, A / RB / RT fire.
 */

#include "../config/user_config.h"

#if GAMEPAD_ENABLED

#include "../config/config.h"
#include "../display/display.h"
#include "game_mode.h"
#include "game_common.h"

#define DF_COLS 7
#define DF_ROWS 4
#define AL_W 7
#define AL_H 5
#define AL_PITCH_X 12
#define AL_PITCH_Y 7
#define FORM_Y0 18
#define FORM_STEP_X 2
#define FORM_DROP 3
#define BK_COUNT 4
#define BK_W 12
#define BK_H 5
#define BK_Y 48
#define CAN_W 9
#define CAN_Y 59
#define CAN_SPEED 70.0f
#define SHOT_SPEED 120.0f
#define BOMBS 3
#define BOMB_SPEED 36.0f
#define UFO_Y 12
#define UFO_W 8
#define UFO_SPEED 28.0f
#define POPS 4
#define LIVES 3
#define DEATH_S 1.4f
#define BANNER_S 1.2f

// Two animation frames per alien type, 7 bits per row (bit 6 = left).
static const uint8_t ALIEN[3][2][AL_H] = {
    {{0x1C, 0x3E, 0x6B, 0x3E, 0x2A}, {0x1C, 0x3E, 0x6B, 0x3E, 0x55}},
    {{0x22, 0x3E, 0x6B, 0x7F, 0x41}, {0x22, 0x3E, 0x6B, 0x7F, 0x22}},
    {{0x3E, 0x7F, 0x49, 0x7F, 0x2A}, {0x3E, 0x7F, 0x49, 0x7F, 0x55}},
};
static const uint8_t ROW_TYPE[DF_ROWS] = {0, 1, 2, 2};
static const uint8_t ROW_SCORE[DF_ROWS] = {30, 20, 10, 10};
static const uint16_t UFO_SCORES[4] = {50, 100, 150, 300};
static const char *const BUNKER_SHAPE[BK_H] = {
    "..########..", ".##########.", "############", "####....####", "###......###",
};

struct Bomb {
  float x, y;
  bool live;
};
struct Pop {
  int16_t x, y;
  float until;
  uint16_t value;  // non-zero: show the score instead of a burst
};

static uint8_t alive[DF_ROWS];  // bit c = column c alive
static float formX, formY;
static int8_t formDir;
static uint8_t animFrame;
static float nextStep;
static uint16_t bunker[BK_COUNT][BK_H];  // bit c = column c of the bunker solid
static float canX;
static bool shotLive;
static float shotX, shotY;
static Bomb bombs[BOMBS];
static float nextBomb;
static bool ufoLive;
static float ufoX, ufoDir, nextUfo;
static Pop pops[POPS];
static float gt;  // game time, only runs while playing
static float deadUntil, bannerUntil;
static uint8_t lives, wave;
static uint32_t score, hiScore;
static unsigned long lastTick;
static GamePhase phase;
static bool newHi;

static int bunkerX(int b) { return 16 + b * 32 - BK_W / 2; }

static uint8_t aliveCount() {
  uint8_t n = 0;
  for (uint8_t r : alive)
    for (; r; r &= r - 1) n++;
  return n;
}

// Waves past the first, capped: how much harder this wave is.
static int waveLevel(int cap) { return wave - 1 < cap ? wave - 1 : cap; }

static void addPop(int x, int y, float life, uint16_t value) {
  Pop *slot = &pops[0];
  for (auto &p : pops)
    if (p.until < slot->until) slot = &p;
  *slot = {(int16_t)x, (int16_t)y, gt + life, value};
}

static void buildBunkers() {
  for (int b = 0; b < BK_COUNT; b++)
    for (int r = 0; r < BK_H; r++) {
      bunker[b][r] = 0;
      for (int c = 0; c < BK_W; c++)
        if (BUNKER_SHAPE[r][c] == '#') bunker[b][r] |= 1u << c;
    }
}

static void startWave() {
  for (auto &r : alive) r = (1u << DF_COLS) - 1;
  formX = 10;
  formY = FORM_Y0 + waveLevel(4) * 2;
  formDir = 1;
  animFrame = 0;
  nextStep = gt + BANNER_S;
  bannerUntil = gt + BANNER_S;
  nextBomb = gt + BANNER_S + gameRandf(0.8f, 1.6f);
  for (auto &b : bombs) b.live = false;
  shotLive = false;
  ufoLive = false;
  nextUfo = gt + gameRandf(14, 24);
  buildBunkers();
}

void defendersReset() {
  hiScore = gameLoadHi("defendersHi");
  gt = 0;
  score = 0;
  lives = LIVES;
  wave = 1;
  deadUntil = 0;
  canX = SCREEN_WIDTH / 2;
  for (auto &p : pops) p.until = 0;
  newHi = false;
  startWave();
  phase = G_READY;
}

static void gameOver() {
  phase = G_OVER;
  gameRumble(90, 70, 500);
  newHi = gameSubmitScore("defendersHi", score);
  if (newHi) hiScore = score;
}

// Chews a small crater around a hit pixel; false if the pixel was empty.
static bool hitBunker(int x, int y, int dirY) {
  if (y < BK_Y || y >= BK_Y + BK_H) return false;
  for (int b = 0; b < BK_COUNT; b++) {
    int c = x - bunkerX(b);
    if (c < 0 || c >= BK_W) continue;
    int r = y - BK_Y;
    if (!(bunker[b][r] & (1u << c))) return false;
    for (int dr = 0; dr < 2; dr++) {
      int rr = r + dr * dirY;
      if (rr < 0 || rr >= BK_H) continue;
      for (int dc = -1; dc <= 1; dc++) {
        int cc = c + dc;
        if (cc < 0 || cc >= BK_W) continue;
        if (dr == 0 && dc == 0) bunker[b][rr] &= ~(1u << cc);
        else if (esp_random() % 3) bunker[b][rr] &= ~(1u << cc);
      }
    }
    return true;
  }
  return false;
}

static void alienEdges(int &left, int &right, int &bottom) {
  left = DF_COLS;
  right = -1;
  bottom = -1;
  for (int r = 0; r < DF_ROWS; r++)
    for (int c = 0; c < DF_COLS; c++)
      if (alive[r] & (1u << c)) {
        left = min(left, c);
        right = max(right, c);
        bottom = max(bottom, r);
      }
}

// Aliens walking through a bunker erase it.
static void eraseUnderAliens() {
  for (int r = 0; r < DF_ROWS; r++) {
    int ay = (int)formY + r * AL_PITCH_Y;
    if (ay + AL_H <= BK_Y || ay >= BK_Y + BK_H) continue;
    for (int c = 0; c < DF_COLS; c++) {
      if (!(alive[r] & (1u << c))) continue;
      int ax = (int)formX + c * AL_PITCH_X;
      for (int b = 0; b < BK_COUNT; b++)
        for (int y = max(ay, BK_Y); y < min(ay + AL_H, BK_Y + BK_H); y++)
          for (int x = ax; x < ax + AL_W; x++) {
            int cc = x - bunkerX(b);
            if (cc >= 0 && cc < BK_W) bunker[b][y - BK_Y] &= ~(1u << cc);
          }
    }
  }
}

static void stepFormation() {
  int left, right, bottom;
  alienEdges(left, right, bottom);
  if (bottom < 0) return;
  float nx = formX + formDir * FORM_STEP_X;
  if (nx + left * AL_PITCH_X < 1 || nx + right * AL_PITCH_X + AL_W > SCREEN_WIDTH - 1) {
    formY += FORM_DROP;
    formDir = -formDir;
  } else {
    formX = nx;
  }
  animFrame ^= 1;
  eraseUnderAliens();
  if (formY + bottom * AL_PITCH_Y + AL_H >= CAN_Y) {
    lives = 0;
    deadUntil = gt + DEATH_S;
    addPop((int)canX, CAN_Y + 1, DEATH_S, 0);
    gameRumble(90, 70, 400);
  }
}

static float stepInterval() {
  float t = (0.05f + 0.55f * aliveCount() / (DF_ROWS * DF_COLS)) * powf(0.88f, waveLevel(6));
  return max(t, 0.025f);
}

static void dropBomb() {
  int cols[DF_COLS], n = 0;
  for (int c = 0; c < DF_COLS; c++)
    for (int r = 0; r < DF_ROWS; r++)
      if (alive[r] & (1u << c)) {
        cols[n++] = c;
        break;
      }
  if (!n) return;
  int col = cols[esp_random() % n];
  if (esp_random() % 2) {  // aimed: the live column closest to the cannon
    float best = 1e9;
    for (int i = 0; i < n; i++) {
      float d = fabsf(formX + cols[i] * AL_PITCH_X + AL_W / 2 - canX);
      if (d < best) {
        best = d;
        col = cols[i];
      }
    }
  }
  int row = DF_ROWS - 1;
  while (!(alive[row] & (1u << col))) row--;
  for (auto &b : bombs)
    if (!b.live) {
      b = {formX + col * AL_PITCH_X + AL_W / 2, formY + row * AL_PITCH_Y + AL_H, true};
      return;
    }
}

static bool shotHitsAlien() {
  int sx = (int)shotX, sy = (int)shotY;
  for (int r = 0; r < DF_ROWS; r++) {
    int ay = (int)formY + r * AL_PITCH_Y;
    if (sy < ay || sy >= ay + AL_H) continue;
    for (int c = 0; c < DF_COLS; c++) {
      if (!(alive[r] & (1u << c))) continue;
      int ax = (int)formX + c * AL_PITCH_X;
      if (sx < ax || sx >= ax + AL_W) continue;
      alive[r] &= ~(1u << c);
      score += ROW_SCORE[r] * (1 + (wave - 1) / 3);
      addPop(ax + AL_W / 2, ay + AL_H / 2, 0.18f, 0);
      gameRumble(0, 25, 40);
      return true;
    }
  }
  return false;
}

static void moveShot(float dt) {
  float dist = SHOT_SPEED * dt;
  for (; dist > 0 && shotLive; dist -= 1) {
    shotY -= min(dist, 1.0f);
    if (shotY < GAME_TOP) {
      shotLive = false;
    } else if (hitBunker((int)shotX, (int)shotY, -1) || shotHitsAlien()) {
      shotLive = false;
    } else if (ufoLive && shotY < UFO_Y + 4 && shotX >= ufoX && shotX < ufoX + UFO_W) {
      uint16_t v = UFO_SCORES[esp_random() % 4];
      score += v;
      addPop((int)ufoX + UFO_W / 2, UFO_Y, 1.0f, v);
      ufoLive = false;
      shotLive = false;
      gameRumble(50, 40, 150);
    } else {
      for (auto &b : bombs)
        if (b.live && fabsf(b.x - shotX) < 1.5f && fabsf(b.y - shotY) < 3) {
          b.live = false;
          shotLive = false;
          addPop((int)shotX, (int)shotY, 0.12f, 0);
        }
    }
  }
}

static void killCannon() {
  lives--;
  deadUntil = gt + DEATH_S;
  addPop((int)canX, CAN_Y + 1, DEATH_S, 0);
  for (auto &b : bombs) b.live = false;
  shotLive = false;
  gameRumble(100, 80, 450);
}

static void moveBombs(float dt) {
  float speed = BOMB_SPEED + waveLevel(6) * 3;
  for (auto &b : bombs) {
    if (!b.live) continue;
    for (float dist = speed * dt; dist > 0 && b.live; dist -= 1) {
      b.y += min(dist, 1.0f);
      if (b.y >= SCREEN_HEIGHT) {
        b.live = false;
      } else if (hitBunker((int)b.x, (int)b.y + 2, 1)) {
        b.live = false;
      } else if (b.y + 3 >= CAN_Y && b.y < CAN_Y + 4 && fabsf(b.x - canX) <= CAN_W / 2) {
        killCannon();
        return;
      }
    }
  }
}

static void update(const GamepadState &in, float dt) {
  gt += dt;

  if (deadUntil) {
    if (gt < deadUntil) return;
    deadUntil = 0;
    if (!lives) return gameOver();
    canX = SCREEN_WIDTH / 2;
    nextBomb = gt + 1.0f;
  }

  float v = 0;
  if (abs(in.lx) > 4000) v = in.lx / 32768.0f;
  if (in.buttons & GP_LEFT) v = min(v, -1.0f);
  if (in.buttons & GP_RIGHT) v = max(v, 1.0f);
  canX = constrain(canX + v * CAN_SPEED * dt, CAN_W / 2.0f + 1, SCREEN_WIDTH - CAN_W / 2.0f - 1);

  if (!shotLive && ((in.buttons & (GP_A | GP_RB)) || in.rt > 400)) {
    shotLive = true;
    shotX = (int)canX;
    shotY = CAN_Y - 1;
  }
  moveShot(dt);

  if (gt < bannerUntil) return;

  if (!aliveCount()) {
    wave++;
    score += 100;
    gameRumble(60, 60, 250);
    startWave();
    return;
  }

  if (gt >= nextStep) {
    stepFormation();
    nextStep = gt + stepInterval();
    if (deadUntil) return;
  }

  if (gt >= nextBomb) {
    dropBomb();
    nextBomb = gt + gameRandf(0.5f, 1.5f) / (1 + 0.15f * waveLevel(6));
  }
  moveBombs(dt);

  if (ufoLive) {
    ufoX += ufoDir * UFO_SPEED * dt;
    if (ufoX < -UFO_W || ufoX > SCREEN_WIDTH) ufoLive = false;
  } else if (gt >= nextUfo && formY > FORM_Y0) {
    ufoLive = true;
    ufoDir = esp_random() % 2 ? 1 : -1;
    ufoX = ufoDir > 0 ? -UFO_W : SCREEN_WIDTH;
    nextUfo = gt + gameRandf(15, 25);
  }
}

static void drawRows(int x, int y, const uint8_t *rows, int h, int w) {
  for (int r = 0; r < h; r++)
    for (int c = 0; c < w; c++)
      if (rows[r] & (1u << (w - 1 - c))) display.drawPixel(x + c, y + r, DISPLAY_WHITE);
}

static void drawCannon(int cx, int y) {
  display.drawPixel(cx, y, DISPLAY_WHITE);
  display.drawFastHLine(cx - 1, y + 1, 3, DISPLAY_WHITE);
  display.fillRect(cx - CAN_W / 2, y + 2, CAN_W, 2, DISPLAY_WHITE);
}

static void drawBurst(int x, int y) {
  static const uint8_t BURST[5] = {0x49, 0x2A, 0x00, 0x2A, 0x49};
  drawRows(x - 3, y - 2, BURST, 5, 7);
}

static void draw() {
  static const uint8_t UFO[4] = {0x38, 0xFE, 0xD6, 0xFE};
  for (int r = 0; r < DF_ROWS; r++)
    for (int c = 0; c < DF_COLS; c++)
      if (alive[r] & (1u << c))
        drawRows((int)formX + c * AL_PITCH_X, (int)formY + r * AL_PITCH_Y,
                 ALIEN[ROW_TYPE[r]][animFrame], AL_H, AL_W);

  for (int b = 0; b < BK_COUNT; b++)
    for (int r = 0; r < BK_H; r++)
      for (int c = 0; c < BK_W; c++)
        if (bunker[b][r] & (1u << c)) display.drawPixel(bunkerX(b) + c, BK_Y + r, DISPLAY_WHITE);

  if (ufoLive) drawRows((int)ufoX, UFO_Y, UFO, 4, 8);
  if (shotLive) display.drawFastVLine((int)shotX, (int)shotY, 3, DISPLAY_WHITE);
  for (const auto &b : bombs) {
    if (!b.live) continue;
    int x = (int)b.x, y = (int)b.y, k = ((int)(gt * 12)) % 2;
    display.drawPixel(x + k, y, DISPLAY_WHITE);
    display.drawPixel(x + 1 - k, y + 1, DISPLAY_WHITE);
    display.drawPixel(x + k, y + 2, DISPLAY_WHITE);
  }

  if (!deadUntil) drawCannon((int)canX, CAN_Y);
  for (const auto &p : pops) {
    if (gt >= p.until) continue;
    if (p.value) {
      char buf[6];
      snprintf(buf, sizeof(buf), "%u", p.value);
      int x = constrain(p.x - (int)strlen(buf) * 3, 0, SCREEN_WIDTH - (int)strlen(buf) * 6);
      display.setCursor(x, UFO_Y - 1);
      display.print(buf);
    } else if (p.y > CAN_Y - 2) {
      if (((int)(gt * 10)) % 2) drawBurst(p.x, p.y);
      else drawCannon(p.x, CAN_Y);
    } else {
      drawBurst(p.x, p.y);
    }
  }
  if (gt < bannerUntil && phase == G_PLAY) {
    char buf[10];
    snprintf(buf, sizeof(buf), "WAVE %u", wave);
    display.fillRect(64 - strlen(buf) * 3 - 2, 37, strlen(buf) * 6 + 3, 10, DISPLAY_BLACK);
    gamePrintCentered(38, buf);
  }

  display.fillRect(0, 0, SCREEN_WIDTH, GAME_TOP, DISPLAY_BLACK);
  gameDrawScore(0, score);
  for (int i = 0; i < lives; i++) {
    int x = 52 + i * 6;
    display.drawPixel(x + 2, 4, DISPLAY_WHITE);
    display.fillRect(x, 5, 5, 2, DISPLAY_WHITE);
  }
  display.setCursor(74, 1);
  display.print("W");
  display.print(wave);
  gameDrawClock();
}

bool defendersFrame(const GamepadState &in, bool padLost) {
  unsigned long now = millis();
  switch (gameFlow(phase, in, padLost)) {
    case STEP_QUIT:
      return false;
    case STEP_RESTART:
      defendersReset();
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

  draw();
  char info[16];
  snprintf(info, sizeof(info), phase == G_READY ? "HI %lu" : "%lu pts",
           (unsigned long)(phase == G_READY ? hiScore : score));
  gameDrawOverlay(phase, padLost, "DEFENDERS", phase == G_PAUSED ? nullptr : info, newHi,
                  "stick/d-pad: move\nA, RB or RT: fire");
  return true;
}

#endif // GAMEPAD_ENABLED
