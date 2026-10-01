/*
 * SmallOLED-PCMonitor - Game of Life Clock (clockStyle 18)
 *
 * Conway's Game of Life on a torus behind the time. Two grids: 42x21 cells of
 * 3px or 64x32 cells of 2px. Two digit sizes: size 3 (default) or size 2
 * (small clock, always on the 2px grid). When font scale and cell size match,
 * a digit is exactly a Life pattern; otherwise it is rasterised onto the cells
 * whose centres it covers. Colony cells draw one pixel smaller than a cell and
 * digits as solid font pixels, which keeps the time readable over the noise.
 *
 * At :56 each changed digit is released into the simulation and evolves for
 * about a second. The survivors around its slot then lift out of the grid and
 * glide into the new digit, which sets pixel by pixel and fires a glider off.
 *
 * Digits, colon, date and corner indicators are dead zones the colony flows
 * around. Cells that stay alive too long erode, and the colony is topped up
 * with gliders, spaceships and R-pentominoes, so no pixel sits still for long.
 * All state is file-local; resetLifeAnimation() returns it to baseline.
 */

#include "../config/config.h"
#include "../display/display.h"
#include "clocks.h"
#include "clock_globals.h"
#include <string.h>

#define LIFE_MAX_COLS 64
#define LIFE_MAX_ROWS 32
#define LIFE_TRIGGER_SECOND 56
#define LIFE_DISSOLVE_GENS 12
#define LIFE_DISSOLVE_MS 90
#define LIFE_CONVERGE_MS 1100
#define LIFE_STAGGER_MS 300
#define LIFE_MAX_AGE 120
#define LIFE_GATHER_PX 12      // colony within this distance feeds the new digit
#define LIFE_MAX_PARTICLES 160
#define LIFE_PARTICLES_PER_DIGIT 40
#define LIFE_PHASE_TIMEOUT_MS 6000
#define LIFE_DATE_W 60
#define LIFE_DATE_H 8

// Columns of the GFX 5x7 font for '0'-'9', LSB = top row.
static const uint8_t LIFE_GLYPHS[10][5] = {
  {0x3E, 0x51, 0x49, 0x45, 0x3E}, {0x00, 0x42, 0x7F, 0x40, 0x00},
  {0x72, 0x49, 0x49, 0x49, 0x46}, {0x21, 0x41, 0x49, 0x4D, 0x33},
  {0x18, 0x14, 0x12, 0x7F, 0x10}, {0x27, 0x45, 0x45, 0x45, 0x39},
  {0x3C, 0x4A, 0x49, 0x49, 0x31}, {0x41, 0x21, 0x11, 0x09, 0x07},
  {0x36, 0x49, 0x49, 0x49, 0x36}, {0x46, 0x49, 0x49, 0x29, 0x1E},
};

static const char LIFE_GLIDER[] = ".X...XXXX";             // 3x3, heads down-right
static const char LIFE_LWSS[] = ".X..XX....X...XXXXX.";    // 5x4, heads left
static const char LIFE_RPENTOMINO[] = ".XXXX..X.";         // 3x3

enum LifePhase { LIFE_IDLE, LIFE_DISSOLVE, LIFE_CONVERGE };

struct LifeLayout {
  int cell, cols, rows, ox;    // Life grid
  int fs;                      // digit font scale (pixels per font pixel)
  int digitX[5], digitY;       // digit slots, in pixels
  int dateX, dateY;
  int merX, merY;
  uint8_t key;                 // changes whenever the geometry does
};

struct LifeParticle {
  float sx, sy, tx, ty;
  uint16_t delay;
};

static LifeLayout L;
static uint8_t life_cells[LIFE_MAX_ROWS][LIFE_MAX_COLS];
static uint8_t life_next[LIFE_MAX_ROWS][LIFE_MAX_COLS];
static uint8_t life_age[LIFE_MAX_ROWS][LIFE_MAX_COLS];
static uint8_t life_blocked[LIFE_MAX_ROWS][LIFE_MAX_COLS];
static LifeParticle life_particles[LIFE_MAX_PARTICLES];
static int life_particle_count = 0;
static int life_population = 0;

static LifePhase life_phase = LIFE_IDLE;
static int life_change_idx[4];
static uint8_t life_change_val[4];
static int life_num_changes = 0;
static int life_dissolve_gens = 0;
static unsigned long life_phase_start = 0;
static unsigned long life_last_step = 0;
static unsigned long life_last_update = 0;
static float life_inject_timer = 0.0f;
static int last_minute_life = -1;
static bool life_triggered = false;
static bool life_init_done = false;

// ========== Layout ==========
static void lifeComputeLayout() {
  bool smallCells = settings.lifeSmallClock || settings.lifeSmallCells;
  if (smallCells) { L.cell = 2; L.cols = 64; L.rows = 32; L.ox = 0; }
  else            { L.cell = 3; L.cols = 42; L.rows = 21; L.ox = 1; }
  L.dateX = (SCREEN_WIDTH - LIFE_DATE_W) / 2;

  if (!settings.lifeSmallClock) {
    L.fs = 3;
    for (int i = 0; i < 5; i++) L.digitX[i] = DIGIT_X[i];
    L.digitY = settings.lifeShowDate ? 18 : 21;
    L.dateY = 4;
    L.merX = 110;
    L.merY = 4;
    L.key = smallCells * 2 + settings.lifeShowDate;
    return;
  }

  L.fs = 2;
  for (int i = 0; i < 5; i++) L.digitX[i] = 34 + 12 * i;
  const int digitH = 7 * L.fs, gap = 3;
  switch (settings.lifeClockPos) {
    case 1:  // top, date underneath
      L.digitY = 2;
      L.dateY = L.digitY + digitH + gap;
      break;
    case 2:  // bottom, date above
      L.digitY = SCREEN_HEIGHT - 2 - digitH;
      L.dateY = L.digitY - gap - LIFE_DATE_H;
      break;
    default: {  // centre the time + date block
      int block = settings.lifeShowDate ? digitH + gap + LIFE_DATE_H : digitH;
      L.digitY = ((SCREEN_HEIGHT - block) / 2) & ~1;
      L.dateY = L.digitY + digitH + gap;
      break;
    }
  }
  L.merX = L.digitX[4] + 5 * L.fs + 3;
  L.merY = L.digitY + 3;
  L.key = 4 + 2 * settings.lifeClockPos + settings.lifeShowDate;
}

// ========== Helpers ==========
static bool lifeGlyphPixel(uint8_t value, int c, int r) {
  return (LIFE_GLYPHS[value][c] >> r) & 1;
}

static int lifeCellX(int c) { return L.ox + c * L.cell; }
static int lifeCellY(int r) { return r * L.cell; }
static int lifeColAt(int x) { return (x - L.ox) / L.cell; }
static int lifeRowAt(int y) { return y / L.cell; }

static float lifeRandf(float lo, float hi) {
  return lo + (hi - lo) * (random(0, 1001) / 1000.0f);
}

static void lifeDensityParams(float &seed, int &floorPop, float &lo, float &hi) {
  switch (settings.lifeDensity) {
    case 0:  seed = 0.12f; floorPop = 12; lo = 10.0f; hi = 18.0f; break;
    case 2:  seed = 0.32f; floorPop = 45; lo = 3.0f;  hi = 7.0f;  break;
    default: seed = 0.22f; floorPop = 28; lo = 6.0f;  hi = 12.0f; break;
  }
  // Floors are tuned for the large grid; scale them with the cell count.
  floorPop = floorPop * (L.cols * L.rows) / (42 * 21);
}

static bool lifeIsChangeSlot(int idx) {
  if (life_phase == LIFE_IDLE) return false;
  for (int k = 0; k < life_num_changes; k++)
    if (life_change_idx[k] == idx) return true;
  return false;
}

static void lifeBlockRect(int c0, int r0, int c1, int r1) {
  if (c0 < 0) c0 = 0;
  if (r0 < 0) r0 = 0;
  if (c1 >= L.cols) c1 = L.cols - 1;
  if (r1 >= L.rows) r1 = L.rows - 1;
  for (int r = r0; r <= r1; r++)
    for (int c = c0; c <= c1; c++) life_blocked[r][c] = 1;
}

// Blocks the cells under a pixel rectangle, plus a one-cell margin.
static void lifeBlockPixels(int x0, int y0, int w, int h) {
  lifeBlockRect(lifeColAt(x0) - 1, lifeRowAt(y0) - 1,
                lifeColAt(x0 + w - 1) + 1, lifeRowAt(y0 + h - 1) + 1);
}

static void lifeBuildMask() {
  memset(life_blocked, 0, sizeof(life_blocked));
  for (int i = 0; i < 5; i++) {
    // A dissolving digit's slot is open so it can mix with the colony.
    if (life_phase == LIFE_DISSOLVE && lifeIsChangeSlot(i)) continue;
    lifeBlockPixels(L.digitX[i], L.digitY, 5 * L.fs, 7 * L.fs);
  }
  if (settings.lifeShowDate) lifeBlockPixels(L.dateX, L.dateY, LIFE_DATE_W, LIFE_DATE_H);
  if (!settings.use24Hour) lifeBlockPixels(L.merX, L.merY, 12, 8);
  if (!wifiConnected) lifeBlockPixels(0, 0, 8, 8);
}

static int lifeStep() {
  lifeBuildMask();
  int pop = 0;
  for (int r = 0; r < L.rows; r++) {
    int up = (r + L.rows - 1) % L.rows, down = (r + 1) % L.rows;
    for (int c = 0; c < L.cols; c++) {
      int left = (c + L.cols - 1) % L.cols, right = (c + 1) % L.cols;
      int n = life_cells[up][left] + life_cells[up][c] + life_cells[up][right] +
              life_cells[r][left] + life_cells[r][right] +
              life_cells[down][left] + life_cells[down][c] + life_cells[down][right];
      uint8_t alive = life_cells[r][c];
      uint8_t next = (n == 3 || (alive && n == 2)) ? 1 : 0;
      if (life_blocked[r][c]) next = 0;
      if (next && alive) {
        if (life_age[r][c] < 255) life_age[r][c]++;
        // Erode still lifes and oscillators before they burn in.
        if (life_age[r][c] > LIFE_MAX_AGE && random(0, 6) == 0) next = 0;
      }
      if (!next || !alive) life_age[r][c] = 0;
      life_next[r][c] = next;
      pop += next;
    }
  }
  memcpy(life_cells, life_next, sizeof(life_cells));
  return pop;
}

// Places a pattern if it and a one-cell border clear every dead zone.
static bool lifeStamp(const char *pat, int w, int h, int c0, int r0,
                      bool flipX, bool flipY) {
  for (int y = -1; y <= h; y++) {
    for (int x = -1; x <= w; x++) {
      int r = ((r0 + y) % L.rows + L.rows) % L.rows;
      int c = ((c0 + x) % L.cols + L.cols) % L.cols;
      if (life_blocked[r][c]) return false;
    }
  }
  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      if (pat[y * w + x] != 'X') continue;
      int r = ((r0 + (flipY ? h - 1 - y : y)) % L.rows + L.rows) % L.rows;
      int c = ((c0 + (flipX ? w - 1 - x : x)) % L.cols + L.cols) % L.cols;
      life_cells[r][c] = 1;
      life_age[r][c] = 0;
    }
  }
  return true;
}

static void lifeInject() {
  lifeBuildMask();
  for (int attempt = 0; attempt < 12; attempt++) {
    int kind = random(0, 10);
    int c = random(0, L.cols), r = random(0, L.rows);
    bool fx = random(0, 2), fy = random(0, 2);
    bool placed;
    if (kind < 6) placed = lifeStamp(LIFE_GLIDER, 3, 3, c, r, fx, fy);
    else if (kind < 8) placed = lifeStamp(LIFE_LWSS, 5, 4, c, r, fx, false);
    else placed = lifeStamp(LIFE_RPENTOMINO, 3, 3, c, r, fx, fy);
    if (placed) return;
  }
}

static void lifeSeed() {
  float seed, lo, hi;
  int floorPop;
  lifeDensityParams(seed, floorPop, lo, hi);
  memset(life_cells, 0, sizeof(life_cells));
  memset(life_age, 0, sizeof(life_age));
  lifeBuildMask();
  int threshold = (int)(seed * 1000);
  life_population = 0;
  for (int r = 0; r < L.rows; r++) {
    for (int c = 0; c < L.cols; c++) {
      life_cells[r][c] = (!life_blocked[r][c] && random(0, 1000) < threshold) ? 1 : 0;
      life_population += life_cells[r][c];
    }
  }
}

// ========== Reset ==========
void resetLifeAnimation() {
  lifeComputeLayout();
  life_phase = LIFE_IDLE;
  life_num_changes = 0;
  life_particle_count = 0;
  life_triggered = false;
  last_minute_life = -1;
  life_last_step = millis();
  life_last_update = 0;
  float seed, lo, hi;
  int floorPop;
  lifeDensityParams(seed, floorPop, lo, hi);
  life_inject_timer = lifeRandf(lo, hi);
  lifeSeed();
  life_init_done = true;
}

bool lifeIsAnimating() {
  return life_phase != LIFE_IDLE;
}

// ========== Minute change ==========
// Brings a digit to life: every cell whose centre lies under one of its font
// pixels is born. With matching scales that is the glyph cell for cell.
static void lifeStartDissolve(unsigned long now) {
  life_phase = LIFE_DISSOLVE;
  for (int k = 0; k < life_num_changes; k++) {
    int idx = life_change_idx[k];
    uint8_t value = getDisplayedDigitValue(idx);
    int x0 = L.digitX[idx], y0 = L.digitY;
    for (int r = lifeRowAt(y0); r <= lifeRowAt(y0 + 7 * L.fs - 1); r++) {
      for (int c = lifeColAt(x0); c <= lifeColAt(x0 + 5 * L.fs - 1); c++) {
        int px = lifeCellX(c) + L.cell / 2 - x0, py = lifeCellY(r) + L.cell / 2 - y0;
        if (px < 0 || py < 0) continue;
        int gx = px / L.fs, gy = py / L.fs;
        if (gx > 4 || gy > 6 || !lifeGlyphPixel(value, gx, gy)) continue;
        life_cells[r][c] = 1;
        life_age[r][c] = 0;
      }
    }
  }
  life_dissolve_gens = 0;
  life_phase_start = now;
  life_last_step = now;
}

static void lifeAddParticle(int sx, int sy, int tx, int ty) {
  if (life_particle_count >= LIFE_MAX_PARTICLES) return;
  LifeParticle &p = life_particles[life_particle_count++];
  p.sx = sx;
  p.sy = sy;
  p.tx = tx;
  p.ty = ty;
  p.delay = random(0, LIFE_STAGGER_MS + 1);
}

// The colony around each changed slot lifts out of the grid and flies into the
// new digit. Every font pixel gets a flyer; spare flyers merge into the
// nearest one, so the whole local cloud condenses rather than vanishing.
static void lifeStartConverge(unsigned long now) {
  life_phase = LIFE_CONVERGE;
  life_particle_count = 0;

  for (int k = 0; k < life_num_changes; k++) {
    int x0 = L.digitX[life_change_idx[k]], y0 = L.digitY;
    uint8_t value = life_change_val[k];

    int16_t srcX[LIFE_PARTICLES_PER_DIGIT], srcY[LIFE_PARTICLES_PER_DIGIT];
    bool used[LIFE_PARTICLES_PER_DIGIT];
    int ns = 0;
    int c0 = lifeColAt(x0 - LIFE_GATHER_PX), c1 = lifeColAt(x0 + 5 * L.fs + LIFE_GATHER_PX);
    int r0 = lifeRowAt(y0 - LIFE_GATHER_PX), r1 = lifeRowAt(y0 + 7 * L.fs + LIFE_GATHER_PX);
    for (int r = r0; r <= r1; r++) {
      for (int c = c0; c <= c1; c++) {
        if (r < 0 || r >= L.rows || c < 0 || c >= L.cols) continue;
        if (!life_cells[r][c]) continue;
        life_cells[r][c] = 0;
        life_age[r][c] = 0;
        if (ns < LIFE_PARTICLES_PER_DIGIT) {
          srcX[ns] = lifeCellX(c);
          srcY[ns] = lifeCellY(r);
          used[ns] = false;
          ns++;
        }
      }
    }

    int16_t tgtX[35], tgtY[35];
    int nt = 0;
    for (int c = 0; c < 5; c++)
      for (int r = 0; r < 7; r++)
        if (lifeGlyphPixel(value, c, r)) {
          tgtX[nt] = x0 + c * L.fs;
          tgtY[nt] = y0 + r * L.fs;
          nt++;
        }

    for (int t = 0; t < nt; t++) {
      int best = -1;
      long bestD = 1L << 30;
      for (int s = 0; s < ns; s++) {
        if (used[s]) continue;
        long dx = srcX[s] - tgtX[t], dy = srcY[s] - tgtY[t];
        if (dx * dx + dy * dy < bestD) { bestD = dx * dx + dy * dy; best = s; }
      }
      int sx, sy;
      if (best >= 0) {
        used[best] = true;
        sx = srcX[best];
        sy = srcY[best];
      } else if (ns > 0) {
        int s = random(0, ns);
        sx = srcX[s];
        sy = srcY[s];
      } else {
        // constrain() is a macro, so the random draw has to happen first.
        sx = tgtX[t] + (int)random(-LIFE_GATHER_PX, LIFE_GATHER_PX + 1);
        sy = tgtY[t] + (int)random(-LIFE_GATHER_PX, LIFE_GATHER_PX + 1);
        sx = constrain(sx, 0, SCREEN_WIDTH - L.cell);
        sy = constrain(sy, 0, SCREEN_HEIGHT - L.cell);
      }
      lifeAddParticle(sx, sy, tgtX[t], tgtY[t]);
    }

    for (int s = 0; s < ns; s++) {
      if (used[s] || nt == 0) continue;
      int best = 0;
      long bestD = 1L << 30;
      for (int t = 0; t < nt; t++) {
        long dx = srcX[s] - tgtX[t], dy = srcY[s] - tgtY[t];
        if (dx * dx + dy * dy < bestD) { bestD = dx * dx + dy * dy; best = t; }
      }
      lifeAddParticle(srcX[s], srcY[s], tgtX[best], tgtY[best]);
    }
  }
  life_phase_start = now;
}

// The freshly set digit fires a glider away from the digit row.
static void lifeLaunchGlider(int idx) {
  lifeBuildMask();
  int c = lifeColAt(L.digitX[idx]) + random(0, 3);
  int below = lifeRowAt(L.digitY + 7 * L.fs - 1) + 3;
  int above = lifeRowAt(L.digitY) - 5;
  bool fx = random(0, 2);
  bool downFirst = random(0, 2);
  for (int i = 0; i < 2; i++) {
    bool down = (i == 0) == downFirst;
    if (down ? lifeStamp(LIFE_GLIDER, 3, 3, c, below, fx, false)
             : lifeStamp(LIFE_GLIDER, 3, 3, c, above, fx, true))
      return;
  }
}

static void lifeFinish() {
  for (int k = 0; k < life_num_changes; k++)
    updateDisplayedTimeDigit(life_change_idx[k], life_change_val[k]);
  int launcher = life_change_idx[random(0, life_num_changes)];
  life_particle_count = 0;
  life_phase = LIFE_IDLE;
  lifeLaunchGlider(launcher);
}

// ========== Update ==========
static void updateLifeAnimation(struct tm *timeinfo) {
  unsigned long now = millis();
  float dt = (now - life_last_update) / 1000.0f;
  if (dt > 0.1f || life_last_update == 0) dt = 0.025f;
  life_last_update = now;

  int seconds = timeinfo->tm_sec;
  int minute = timeinfo->tm_min;
  if (minute != last_minute_life) {
    last_minute_life = minute;
    life_triggered = false;
  }
  if (seconds >= LIFE_TRIGGER_SECOND && !life_triggered && life_phase == LIFE_IDLE) {
    life_triggered = true;
    time_overridden = true;
    time_override_start = millis();
    calculateTargetDigits(displayed_hour, displayed_min, displayed_is_pm);

    life_num_changes = 0;
    for (int i = 0; i < num_targets; i++) {
      if (target_digit_index[i] == 2) continue;  // colon
      life_change_idx[life_num_changes] = target_digit_index[i];
      life_change_val[life_num_changes] = target_digit_values[i];
      life_num_changes++;
    }
    if (life_num_changes > 0) lifeStartDissolve(now);
    else time_overridden = false;
  }

  if (life_phase == LIFE_DISSOLVE) {
    if (now - life_last_step >= LIFE_DISSOLVE_MS) {
      life_last_step = now;
      life_population = lifeStep();
      life_dissolve_gens++;
    }
    if (life_dissolve_gens >= LIFE_DISSOLVE_GENS ||
        now - life_phase_start > LIFE_PHASE_TIMEOUT_MS) {
      lifeStartConverge(now);
    }
    return;
  }

  float seed, lo, hi;
  int floorPop;
  lifeDensityParams(seed, floorPop, lo, hi);

  unsigned long interval = 1000 / max(1, (int)settings.lifeSpeed);
  if (now - life_last_step >= interval) {
    life_last_step = (now - life_last_step > 2 * interval) ? now : life_last_step + interval;
    life_population = lifeStep();
    if (life_phase == LIFE_IDLE && life_population < floorPop && random(0, 4) == 0)
      lifeInject();
  }

  if (life_phase == LIFE_CONVERGE) {
    if (now - life_phase_start >= LIFE_CONVERGE_MS + LIFE_STAGGER_MS) lifeFinish();
  } else {
    life_inject_timer -= dt;
    if (life_inject_timer <= 0) {
      life_inject_timer = lifeRandf(lo, hi);
      lifeInject();
    }
  }
}

// ========== Drawing ==========
static void drawLifeDigit(int x, int y, uint8_t value) {
  for (int c = 0; c < 5; c++)
    for (int r = 0; r < 7; r++)
      if (lifeGlyphPixel(value, c, r))
        display.fillRect(x + c * L.fs, y + r * L.fs, L.fs, L.fs, DISPLAY_WHITE);
}

void displayClockWithLife() {
  if (!life_init_done) resetLifeAnimation();

  // Switching size or position from the web UI changes the grid geometry, so
  // the colony and any in-flight change have to start over.
  uint8_t prevKey = L.key;
  lifeComputeLayout();
  if (L.key != prevKey) {
    if (life_phase != LIFE_IDLE) time_overridden = false;
    resetLifeAnimation();
  }

  struct tm timeinfo;
  if (!getTimeWithTimeout(&timeinfo)) {
    display.setTextSize(1);
    display.setCursor(20, 28);
    display.print(ntpSynced ? "Time Error" : "Syncing time...");
    return;
  }

  updateLifeAnimation(&timeinfo);

  if (!time_overridden) syncDisplayedTime(&timeinfo);
  maintainTimeOverride(&timeinfo, life_phase == LIFE_IDLE);

  int dot = L.cell - 1;
  for (int r = 0; r < L.rows; r++)
    for (int c = 0; c < L.cols; c++)
      if (life_cells[r][c])
        display.fillRect(lifeCellX(c), lifeCellY(r), dot, dot, DISPLAY_WHITE);

  for (int i = 0; i < 5; i++) {
    if (i == 2 || lifeIsChangeSlot(i)) continue;
    drawLifeDigit(L.digitX[i], L.digitY, getDisplayedDigitValue(i));
  }
  display.setTextSize(L.fs);
  display.setCursor(L.digitX[2], L.digitY);
  display.print(shouldShowColon() ? ':' : ' ');

  if (life_phase == LIFE_CONVERGE) {
    long elapsed = (long)(millis() - life_phase_start);
    for (int i = 0; i < life_particle_count; i++) {
      const LifeParticle &p = life_particles[i];
      float t = (elapsed - (long)p.delay) / (float)LIFE_CONVERGE_MS;
      if (t >= 1.0f) {
        display.fillRect((int)p.tx, (int)p.ty, L.fs, L.fs, DISPLAY_WHITE);
        continue;
      }
      if (t < 0.0f) t = 0.0f;
      float e = t * t * (3.0f - 2.0f * t);
      display.fillRect((int)(p.sx + (p.tx - p.sx) * e), (int)(p.sy + (p.ty - p.sy) * e),
                       dot, dot, DISPLAY_WHITE);
    }
  }

  if (settings.lifeShowDate) {
    display.setTextSize(1);
    char dateStr[12];
    switch (settings.dateFormat) {
      case 0: sprintf(dateStr, "%02d/%02d/%04d", timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900); break;
      case 1: sprintf(dateStr, "%02d/%02d/%04d", timeinfo.tm_mon + 1, timeinfo.tm_mday, timeinfo.tm_year + 1900); break;
      case 2: sprintf(dateStr, "%04d-%02d-%02d", timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday); break;
      case 3: sprintf(dateStr, "%02d.%02d.%04d", timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900); break;
    }
    display.setCursor(L.dateX, L.dateY);
    display.print(dateStr);
  }
  drawMeridiemIndicator(L.merX, L.merY, displayed_is_pm);

  if (!wifiConnected) drawNoWiFiIcon(0, 0);
}
