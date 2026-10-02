/*
 * SmallOLED-PCMonitor - Falling Blocks (game mode)
 *
 * Same look as the block clock's small corner clock mode: the well spans the
 * whole panel (32x13 cells of 4px, 3x3 blocks with a 1px gap, y = 12..63) and
 * a 10px band on top carries the held piece, score, level, next piece and a
 * small HH:MM clock in the right corner. The wide well needs ~8 pieces per line, so the
 * level steps up every 5 lines instead of 10.
 *
 * 7-bag randomiser, delayed auto shift, lock delay with a capped number of
 * resets, simple wall kicks, ghost piece (centre dots), flashing line clears,
 * hold (once per piece; drawn dotted until the next piece unlocks it), pad
 * rumble on drops, clears and game over.
 *
 * Controls: d-pad / left stick move, down soft drop, up hard drop (stick up
 * only when blocksStickDrop is on), A rotate clockwise, B / X rotate
 * counter-clockwise, LB / RB hold, Menu or View pause.
 * From pause / game over: View quits game mode.
 */

#include "../config/user_config.h"

#if GAMEPAD_ENABLED

#include "../config/config.h"
#include "../display/display.h"
#include "../clocks/clocks.h"
#include "game_mode.h"
#include <Preferences.h>

#define TW 32
#define TH 13
#define CELL 4
#define BLOCK 3
#define FIELD_Y 12
#define SPAWN_X 14
#define LINES_PER_LEVEL 5
#define DAS_MS 150
#define ARR_MS 35
#define SOFT_DROP_MS 35
#define LOCK_MS 500
#define LOCK_RESETS 15
#define CLEAR_FLASH_MS 300
#define FULL_ROW 0xFFFFFFFFu
#define NO_PIECE 0xFF
#define HOLD_X 0
#define SCORE_X 11

// 4x4 masks, bit 15 = top-left, one row per nibble. Order: I J L O S T Z.
static const uint16_t SHAPES[7][4] = {
    {0x0F00, 0x2222, 0x00F0, 0x4444},
    {0x8E00, 0x6440, 0x0E20, 0x44C0},
    {0x2E00, 0x4460, 0x0E80, 0xC440},
    {0x6600, 0x6600, 0x6600, 0x6600},
    {0x6C00, 0x4620, 0x06C0, 0x8C40},
    {0x4E00, 0x4640, 0x0E40, 0x4C40},
    {0xC600, 0x2640, 0x0C60, 0x4C80},
};
static const uint16_t GRAVITY_MS[] = {800, 717, 633, 550, 467, 383, 300, 217, 133, 100,
                                      83,  83,  83,  67,  67,  67,  50,  50,  50,  33};
static const uint16_t LINE_SCORE[] = {0, 40, 100, 300, 1200};
static const uint8_t MAX_LEVEL = sizeof(GRAVITY_MS) / sizeof(GRAVITY_MS[0]) - 1;

enum BlocksPhase { T_READY, T_PLAY, T_CLEARING, T_PAUSED, T_OVER };

static uint32_t board[TH];
static uint8_t bag[7], bagLeft;
static uint8_t cur, rot, nextPiece;
static uint8_t holdPiece = NO_PIECE;
static bool holdUsed;
static int8_t px, py;
static uint32_t score, hiScore;
static uint16_t lines;
static uint8_t level;
static BlocksPhase phase;
static bool hiLoaded, newHi;
static unsigned long lastFall, lockStart, clearStart, nextRepeat;
static bool onGround;
static uint8_t lockResets;
static int8_t dasDir;
static uint16_t clearMask;

static inline bool shapeBit(uint16_t s, int i) { return s & (0x8000 >> i); }

static bool fits(uint8_t p, uint8_t r, int x, int y) {
  uint16_t s = SHAPES[p][r];
  for (int i = 0; i < 16; i++) {
    if (!shapeBit(s, i)) continue;
    int cx = x + (i & 3), cy = y + (i >> 2);
    if (cx < 0 || cx >= TW || cy >= TH) return false;
    if (cy >= 0 && (board[cy] & (1u << cx))) return false;
  }
  return true;
}

static uint8_t drawFromBag() {
  if (!bagLeft) {
    for (uint8_t i = 0; i < 7; i++) bag[i] = i;
    for (uint8_t i = 6; i > 0; i--) {
      uint8_t j = esp_random() % (i + 1);
      uint8_t t = bag[i];
      bag[i] = bag[j];
      bag[j] = t;
    }
    bagLeft = 7;
  }
  return bag[--bagLeft];
}

static void loadHiScore() {
  if (hiLoaded) return;
  Preferences p;
  p.begin("game", true);
  hiScore = p.getUInt("blocksHi", 0);
  p.end();
  hiLoaded = true;
}

static void rumble(uint8_t strong, uint8_t weak, uint16_t ms) {
  if (settings.gameRumble) gamepadRumble(strong, weak, ms);
}

static uint8_t startLevel() { return constrain(settings.blocksStartLevel, 1, 10) - 1; }

static void gameOver() {
  phase = T_OVER;
  rumble(80, 80, 450);
  if (score > hiScore) {
    hiScore = score;
    newHi = true;
    Preferences p;
    p.begin("game", false);
    p.putUInt("blocksHi", hiScore);
    p.end();
  }
}

static void enterPiece(uint8_t piece, unsigned long now) {
  cur = piece;
  rot = 0;
  px = SPAWN_X;
  py = cur == 0 ? -1 : 0;
  onGround = false;
  lockResets = 0;
  lastFall = now;
  if (!fits(cur, rot, px, py)) gameOver();
}

static void spawn(unsigned long now) {
  holdUsed = false;
  uint8_t piece = nextPiece;
  nextPiece = drawFromBag();
  enterPiece(piece, now);
}

static void holdCurrent(unsigned long now) {
  if (holdUsed) return;
  uint8_t was = holdPiece;
  holdPiece = cur;
  if (was == NO_PIECE) spawn(now);
  else enterPiece(was, now);
  holdUsed = true;
}

static void lockPiece(unsigned long now) {
  uint16_t s = SHAPES[cur][rot];
  for (int i = 0; i < 16; i++) {
    if (!shapeBit(s, i)) continue;
    int cy = py + (i >> 2);
    if (cy < 0) {
      gameOver();
      return;
    }
    board[cy] |= 1u << (px + (i & 3));
  }
  clearMask = 0;
  for (int y = 0; y < TH; y++)
    if (board[y] == FULL_ROW) clearMask |= 1 << y;
  if (clearMask) {
    uint8_t n = __builtin_popcount(clearMask);
    rumble(n >= 4 ? 100 : 25 + n * 15, n >= 4 ? 60 : 0, n >= 4 ? 300 : 120);
    phase = T_CLEARING;
    clearStart = now;
  } else {
    spawn(now);
  }
}

static void collapseRows(unsigned long now) {
  uint8_t n = 0;
  int dst = TH - 1;
  for (int y = TH - 1; y >= 0; y--) {
    if (clearMask & (1 << y)) {
      n++;
      continue;
    }
    board[dst--] = board[y];
  }
  while (dst >= 0) board[dst--] = 0;
  score += (uint32_t)LINE_SCORE[n] * (level + 1);
  lines += n;
  level = min(max(lines / LINES_PER_LEVEL, (int)startLevel()), (int)MAX_LEVEL);
  phase = T_PLAY;
  spawn(now);
}

static void touchedPiece(unsigned long now) {
  if (onGround && lockResets < LOCK_RESETS) {
    lockStart = now;
    lockResets++;
  }
}

static bool tryMove(int dx, int dy, unsigned long now) {
  if (!fits(cur, rot, px + dx, py + dy)) return false;
  px += dx;
  py += dy;
  touchedPiece(now);
  return true;
}

static void tryRotate(int dir, unsigned long now) {
  static const int8_t KICKS[][2] = {{0, 0}, {-1, 0}, {1, 0}, {-2, 0}, {2, 0}, {0, -1}};
  uint8_t nr = (rot + dir) & 3;
  for (auto &k : KICKS) {
    if (fits(cur, nr, px + k[0], py + k[1])) {
      rot = nr;
      px += k[0];
      py += k[1];
      touchedPiece(now);
      return;
    }
  }
}

void blocksReset() {
  loadHiScore();
  memset(board, 0, sizeof(board));
  bagLeft = 0;
  score = 0;
  lines = 0;
  level = startLevel();
  newHi = false;
  holdPiece = NO_PIECE;
  dasDir = 0;
  nextPiece = drawFromBag();
  spawn(millis());
  phase = T_READY;
}

static void resumeTimers(unsigned long now) {
  lastFall = now;
  lockStart = now;
}

static void updatePlay(const GamepadState &in, unsigned long now) {
  uint16_t b = in.buttons, p = in.pressed;

  if ((p & GP_UP) && (settings.blocksStickDrop || gamepadHatUp(in.hat))) {
    while (fits(cur, rot, px, py + 1)) {
      py++;
      score += 2;
    }
    rumble(0, 30, 40);
    lockPiece(now);
    return;
  }
  if (p & (GP_LB | GP_RB)) {
    holdCurrent(now);
    if (phase != T_PLAY) return;
  }
  if (p & GP_A) tryRotate(1, now);
  if (p & (GP_B | GP_X)) tryRotate(-1, now);

  int8_t dir = (b & GP_LEFT) && !(b & GP_RIGHT) ? -1 : (b & GP_RIGHT) && !(b & GP_LEFT) ? 1 : 0;
  if (dir != dasDir) {
    dasDir = dir;
    if (dir) {
      tryMove(dir, 0, now);
      nextRepeat = now + DAS_MS;
    }
  } else if (dir) {
    // Auto-repeat can outpace the frame rate, so catch up on every step due.
    while (now >= nextRepeat && tryMove(dir, 0, now)) nextRepeat += ARR_MS;
    if (now >= nextRepeat) nextRepeat = now + ARR_MS;
  }

  bool soft = b & GP_DOWN;
  uint16_t interval = GRAVITY_MS[level];
  if (soft && interval > SOFT_DROP_MS) interval = SOFT_DROP_MS;
  if (now - lastFall >= interval) {
    lastFall = now;
    if (fits(cur, rot, px, py + 1)) {
      py++;
      if (soft) score++;
    }
  }

  bool grounded = !fits(cur, rot, px, py + 1);
  if (grounded && !onGround) lockStart = now;
  onGround = grounded;
  if (onGround && now - lockStart >= LOCK_MS) lockPiece(now);
}

// ---------- drawing ----------

static void drawBlock(int x, int y) {
  display.fillRect(x * CELL, FIELD_Y + y * CELL, BLOCK, BLOCK, DISPLAY_WHITE);
}

static void drawPiece(uint8_t p, uint8_t r, int x, int y, bool ghost) {
  uint16_t s = SHAPES[p][r];
  for (int i = 0; i < 16; i++) {
    int cx = x + (i & 3), cy = y + (i >> 2);
    if (!shapeBit(s, i) || cy < 0) continue;
    if (ghost) display.drawPixel(cx * CELL + 1, FIELD_Y + cy * CELL + 1, DISPLAY_WHITE);
    else drawBlock(cx, cy);
  }
}

static void drawMini(uint8_t piece, int x, bool dotted) {
  uint16_t s = SHAPES[piece][0];
  for (int i = 0; i < 16; i++) {
    if (!shapeBit(s, i)) continue;
    int mx = x + (i & 3) * 2, my = 3 + (i >> 2) * 2;
    if (dotted) display.drawPixel(mx, my, DISPLAY_WHITE);
    else display.fillRect(mx, my, 2, 2, DISPLAY_WHITE);
  }
}

// Top band: held piece, score, level, next piece, HH:MM in the right corner.
static void drawBand() {
  display.setTextSize(1);
  display.setTextColor(DISPLAY_WHITE);
  if (holdPiece != NO_PIECE) drawMini(holdPiece, HOLD_X, holdUsed);
  display.setCursor(SCORE_X, 1);
  display.print(score);

  display.setCursor(50, 1);
  display.print("L");
  display.print(level + 1);

  drawMini(nextPiece, 70, false);

  struct tm t;
  if (peekLocalTime(&t)) {
    int h, m;
    bool pm;
    formatTimeForDisplay(t.tm_hour, t.tm_min, h, m, pm);
    char buf[6];
    snprintf(buf, sizeof(buf), "%02d%c%02d", h, shouldShowColon() ? ':' : ' ', m);
    display.setCursor(SCREEN_WIDTH - 31, 1);
    display.print(buf);
  }
}

static void drawOverlay(const char *l1, const char *l2, const char *l3) {
  display.fillRect(18, 18, 92, 34, DISPLAY_BLACK);
  display.drawRect(18, 18, 92, 34, DISPLAY_WHITE);
  const char *ls[] = {l1, l2, l3};
  for (int i = 0; i < 3; i++) {
    if (!ls[i]) continue;
    display.setCursor(64 - strlen(ls[i]) * 3, 22 + i * 10);
    display.print(ls[i]);
  }
}

static void drawScene(unsigned long now) {
  bool flashOn = (now / 60) % 2;
  for (int y = 0; y < TH; y++) {
    if (phase == T_CLEARING && (clearMask & (1 << y)) && !flashOn) continue;
    uint32_t row = board[y];
    if (!row) continue;
    for (int x = 0; x < TW; x++)
      if (row & (1u << x)) drawBlock(x, y);
  }

  if (phase == T_PLAY || phase == T_PAUSED || phase == T_READY) {
    int gy = py;
    while (fits(cur, rot, px, gy + 1)) gy++;
    if (gy != py) drawPiece(cur, rot, px, gy, true);
    drawPiece(cur, rot, px, py, false);
  }

  drawBand();
}

bool blocksFrame(const GamepadState &in, bool padLost) {
  unsigned long now = millis();

  if (padLost) {
    if (phase == T_PLAY || phase == T_CLEARING) phase = T_PAUSED;
    drawScene(now);
    drawOverlay("PAD LOST", "reconnecting", (now / 400) % 2 ? "..." : nullptr);
    return true;
  }

  switch (phase) {
    case T_READY:
      if (in.pressed & GP_VIEW) return false;
      if (in.pressed & (GP_A | GP_MENU)) {
        phase = T_PLAY;
        resumeTimers(now);
      }
      break;
    case T_PLAY:
      if (in.pressed & (GP_MENU | GP_VIEW)) {
        phase = T_PAUSED;
        break;
      }
      updatePlay(in, now);
      break;
    case T_CLEARING:
      if (now - clearStart >= CLEAR_FLASH_MS) collapseRows(now);
      break;
    case T_PAUSED:
      if (in.pressed & GP_VIEW) return false;
      if (in.pressed & (GP_MENU | GP_A)) {
        phase = T_PLAY;
        dasDir = 0;
        resumeTimers(now);
      }
      break;
    case T_OVER:
      if (in.pressed & GP_VIEW) return false;
      if (in.pressed & (GP_A | GP_MENU)) {
        blocksReset();
        phase = T_PLAY;
      }
      break;
  }

  drawScene(now);
  char info[22];
  if (phase == T_READY) {
    snprintf(info, sizeof(info), "HI %lu", (unsigned long)hiScore);
    drawOverlay("A: start", info, "VIEW: quit");
  } else if (phase == T_PAUSED) {
    uint8_t bat = gamepadBattery();
    snprintf(info, sizeof(info), "%u ln  pad %u%%", lines, bat);
    drawOverlay("PAUSED", info, "VIEW: quit");
  } else if (phase == T_OVER) {
    snprintf(info, sizeof(info), "%u lines", lines);
    drawOverlay(newHi ? "NEW HI SCORE" : "GAME OVER", info, "A: again");
  }
  return true;
}

#endif // GAMEPAD_ENABLED
