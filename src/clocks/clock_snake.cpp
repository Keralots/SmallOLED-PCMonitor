/*
 * SmallOLED-PCMonitor - Snake Clock (clockStyle 7)
 *
 * A Nokia-style snake lives on a 4px grid. It moves one cell at a time in the
 * four cardinal directions, never reverses onto its neck, and steers around
 * both its own body and the clock digits (treated as solid obstacles), so it
 * never crashes. While idle it chases food and grows as it eats, up to a safe
 * cap, then snaps back to its base length.
 *
 * At the top of each minute each changed digit (one at a time) drops a few
 * pellets where its glyph was. The snake hunts them down and eats them one by
 * one; once they are gone it slithers clear of the spot and only then does the
 * new digit drop in. (The snake's movement/navigation is unchanged.)
 *
 * The date row is optional; with it off the digits are centred and the snake
 * gets the whole screen.
 *
 * All state is file-local. resetSnakeAnimation() (called from
 * resetClockAnimationState) returns everything to a clean baseline.
 */

#include "../config/config.h"
#include "../display/display.h"
#include "clocks.h"
#include "clock_globals.h"

// ========== Layout / tuning ==========
#define SCELL 4                      // grid cell size in pixels
#define SGRID_W 32                   // 128 / 4
#define SGRID_H 16                   // 64 / 4
#define SNAKE_CELLS (SGRID_W * SGRID_H)  // 512 cells - flow-field work area
#define SNAKE_MAX_LEN 24             // hard ceiling on body cells
#define SNAKE_DIGIT_W 16             // size-3 digit obstacle width
#define SNAKE_DIGIT_H 21             // size-3 digit obstacle height
#define SNAKE_TIME_Y_TOP 16          // digit top when the date is shown
#define SNAKE_TIME_Y_CENTER 21       // digit top when centred (date off)
#define SNAKE_TRIGGER_SECOND 56
#define SNAKE_PELLET_PITCH 3         // pellet grid pitch (matches size-3 glyph)
#define SNAKE_PELLETS_PER_DIGIT 5    // how many pellets a digit leaves behind
#define SNAKE_MAX_PELLETS 35         // 5x7 glyph cells
#define SNAKE_LEAVE_MAX_STEPS 40     // safety cap waiting for the snake to clear
#define SNAKE_EAT_MAX_STEPS 80       // safety cap chasing a digit's pellets

enum SnakePhase { SNAKE_ROAM, SNAKE_EAT, SNAKE_LEAVE };

struct SnakeCell { int8_t cx, cy; };
struct SnakePellet { int8_t px, py; bool active; };

static const int SNAKE_DIGIT_IDX[4] = {0, 1, 3, 4};  // digit positions (skip colon)

// 5x7 glyphs (same numerals as the Pac-Man / Tetris pellet font) - used to
// place the pellets a digit leaves behind.
static const uint8_t snakeDigitGlyph[10][7] = {
  {0b01110, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01110},
  {0b00100, 0b01100, 0b00100, 0b00100, 0b00100, 0b00100, 0b01110},
  {0b01110, 0b10001, 0b00001, 0b00010, 0b00100, 0b01000, 0b11111},
  {0b01110, 0b10001, 0b00001, 0b00110, 0b00001, 0b10001, 0b01110},
  {0b00010, 0b00110, 0b01010, 0b10010, 0b11111, 0b00010, 0b00010},
  {0b11111, 0b10000, 0b11110, 0b00001, 0b00001, 0b10001, 0b01110},
  {0b00110, 0b01000, 0b10000, 0b10110, 0b10001, 0b10001, 0b01110},
  {0b11111, 0b00001, 0b00010, 0b00100, 0b01000, 0b01000, 0b01000},
  {0b01110, 0b10001, 0b10001, 0b01110, 0b10001, 0b10001, 0b01110},
  {0b01110, 0b10001, 0b10001, 0b01111, 0b00001, 0b00010, 0b01100}
};

static SnakePhase snake_phase = SNAKE_ROAM;
static int snake_dir_x = 1, snake_dir_y = 0;
static SnakeCell snake_body[SNAKE_MAX_LEN];  // body[0] is the head
static int snake_body_len = 0;
static int snake_base_len = 8;
static int snake_target_len = 8;

static int snake_food_cx = 0, snake_food_cy = 0;
static bool snake_food_active = false;

static int snake_change_idx[4];
static uint8_t snake_change_val[4];
static int snake_num_changes = 0;
static int snake_cur_change = 0;

static SnakePellet snake_pellets[SNAKE_MAX_PELLETS];
static int snake_pellet_count = 0;
static int snake_pellets_left = 0;
static int snake_eating_idx = -1;    // digit currently dropped into pellets
static uint8_t snake_eat_val = 0;    // its new value
static int snake_eat_steps = 0;      // steps spent chasing the current digit's pellets

static int snake_leaving_idx = -1;   // digit cleared, waiting for snake to exit
static uint8_t snake_leaving_val = 0;
static int snake_leave_steps = 0;

static int last_minute_snake = -1;
static bool snake_triggered = false;
static unsigned long last_snake_update = 0;
static bool snake_init_done = false;

// ========== Geometry helpers ==========
static int snakeTimeY() {
  // Date shown: digits sit just under the top date row. Date off: centre them.
  return settings.snakeShowDate ? SNAKE_TIME_Y_TOP : SNAKE_TIME_Y_CENTER;
}

static void snakeBounds(int &minx, int &maxx, int &miny, int &maxy) {
  int inset = settings.snakeWallBorder ? 1 : 0;
  minx = inset;
  maxx = SGRID_W - 1 - inset;
  miny = (settings.snakeShowDate ? 3 : 0) + inset;  // keep clear of a top date
  maxy = SGRID_H - 1 - inset;
}

// True if (px,py) falls inside digit idx's box (matches the obstacle margin).
static bool snakePointInDigit(int px, int py, int idx) {
  int gy = snakeTimeY();
  int bx = DIGIT_X[idx] - 1;
  return px >= bx && px < bx + SNAKE_DIGIT_W + 1 &&
         py >= gy - 1 && py < gy + SNAKE_DIGIT_H + 1;
}

// True if the cell sits on a clock digit. The digit being eaten or vacated is
// excluded so the snake can move freely through that spot.
static bool snakeCellOnDigit(int cx, int cy) {
  int pcx = cx * SCELL + 1;
  int pcy = cy * SCELL + 1;
  for (int k = 0; k < 4; k++) {
    int idx = SNAKE_DIGIT_IDX[k];
    if (idx == snake_eating_idx || idx == snake_leaving_idx) continue;
    if (snakePointInDigit(pcx, pcy, idx)) return true;
  }
  return false;
}

// True if (cx,cy) overlaps the body. The tail cell is treated as free because
// it vacates on the next step.
static bool snakeCellOnBody(int cx, int cy) {
  for (int i = 0; i < snake_body_len - 1; i++) {
    if (snake_body[i].cx == cx && snake_body[i].cy == cy) return true;
  }
  return false;
}

static bool snakeCellFree(int cx, int cy, int minx, int maxx, int miny, int maxy) {
  if (cx < minx || cx > maxx || cy < miny || cy > maxy) return false;
  if (snakeCellOnDigit(cx, cy)) return false;
  if (snakeCellOnBody(cx, cy)) return false;
  return true;
}

// True once no body cell overlaps digit idx's box (snake has slithered clear).
static bool snakeBodyClearOfDigit(int idx) {
  for (int i = 0; i < snake_body_len; i++) {
    if (snakePointInDigit(snake_body[i].cx * SCELL + 1,
                          snake_body[i].cy * SCELL + 1, idx)) return false;
  }
  return true;
}

// ========== Setup helpers ==========
static void snakeSpawnFood() {
  int minx, maxx, miny, maxy;
  snakeBounds(minx, maxx, miny, maxy);
  for (int tries = 0; tries < 80; tries++) {
    int cx = random(minx, maxx + 1);
    int cy = random(miny, maxy + 1);
    if (snakeCellOnDigit(cx, cy)) continue;
    bool onBody = false;
    for (int i = 0; i < snake_body_len; i++)
      if (snake_body[i].cx == cx && snake_body[i].cy == cy) { onBody = true; break; }
    if (onBody) continue;
    snake_food_cx = cx;
    snake_food_cy = cy;
    snake_food_active = true;
    return;
  }
  snake_food_active = false;
}

void resetSnakeAnimation() {
  snake_phase = SNAKE_ROAM;
  snake_dir_x = 1;
  snake_dir_y = 0;

  snake_base_len = constrain((int)settings.snakeLength, 4, 12);
  snake_target_len = snake_base_len;

  int minx, maxx, miny, maxy;
  snakeBounds(minx, maxx, miny, maxy);
  int hx = minx + 4;
  if (hx > maxx) hx = maxx;
  int hy = maxy;  // start along the bottom lane
  snake_body_len = 0;
  for (int i = 0; i < snake_base_len && i < SNAKE_MAX_LEN; i++) {
    int cx = hx - i;
    if (cx < minx) cx = minx;
    snake_body[i].cx = cx;
    snake_body[i].cy = hy;
    snake_body_len++;
  }

  snake_num_changes = 0;
  snake_cur_change = 0;
  snake_eating_idx = -1;
  snake_eat_steps = 0;
  snake_leaving_idx = -1;
  snake_leave_steps = 0;
  snake_pellet_count = 0;
  snake_pellets_left = 0;
  snake_food_active = false;
  snake_triggered = false;
  last_minute_snake = -1;
  snakeSpawnFood();
  snake_init_done = true;
}

// ========== Movement ==========
static int snakeStepIntervalMs() {
  float s = settings.snakeSpeed / 10.0f;
  if (s < 0.3f) s = 0.3f;
  int ms = (int)(150.0f / s);  // 1.2 -> ~125ms/cell
  if (ms < 45) ms = 45;
  if (ms > 320) ms = 320;
  return ms;
}

// Greedy steer: pick the safe cardinal move (no reverse, no wall, no digit, no
// body) that gets closest to (tcx,tcy). Random tie-break avoids getting stuck.
// Used only as a fallback when the flow-field cannot reach the target (e.g. the
// food is momentarily walled off by the snake's own body). Returns true if a
// move was chosen.
static bool snakeChooseDir(int tcx, int tcy) {
  int minx, maxx, miny, maxy;
  snakeBounds(minx, maxx, miny, maxy);
  const int dirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
  int hx = snake_body[0].cx;
  int hy = snake_body[0].cy;
  int bestDx = snake_dir_x, bestDy = snake_dir_y, bestScore = 0x7fffffff;
  bool found = false;

  for (int d = 0; d < 4; d++) {
    int dx = dirs[d][0], dy = dirs[d][1];
    if (dx == -snake_dir_x && dy == -snake_dir_y) continue;  // no reversing
    int nx = hx + dx, ny = hy + dy;
    if (!snakeCellFree(nx, ny, minx, maxx, miny, maxy)) continue;
    int score = abs(nx - tcx) + abs(ny - tcy);
    if (score < bestScore || (score == bestScore && random(2))) {
      bestScore = score;
      bestDx = dx;
      bestDy = dy;
      found = true;
    }
  }
  if (found) {
    snake_dir_x = bestDx;
    snake_dir_y = bestDy;
  }
  return found;
}

// Flow-field pathfinder. A breadth-first search seeded from the target floods
// the open grid (cells that are in-bounds and clear of digits/body) with the
// exact shortest-path distance back to (tcx,tcy). The head then follows the
// gradient downhill: it steps to the non-reverse neighbour with the smallest
// distance. Because BFS sees the whole board, the snake routes *around* the
// digits through the corridors above/below/between them instead of stalling
// against a wall - it heads to the food the way a person steering it would.
// Returns true if a reachable heading toward the target was found.
static bool snakeFlowDir(int tcx, int tcy) {
  if (tcx < 0 || tcx >= SGRID_W || tcy < 0 || tcy >= SGRID_H) return false;

  int minx, maxx, miny, maxy;
  snakeBounds(minx, maxx, miny, maxy);

  static uint8_t dist[SNAKE_CELLS];
  static uint16_t queue[SNAKE_CELLS];
  for (int i = 0; i < SNAKE_CELLS; i++) dist[i] = 255;

  const int dirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
  int qhead = 0, qtail = 0;

  // Seed the target. It may sit on an obstacle (e.g. the next digit during the
  // LEAVE phase); we still flood outward from it so the snake heads to the
  // nearest free cell beside it.
  int tidx = tcy * SGRID_W + tcx;
  dist[tidx] = 0;
  queue[qtail++] = tidx;

  while (qhead < qtail) {
    int cur = queue[qhead++];
    int cx = cur % SGRID_W, cy = cur / SGRID_W;
    uint8_t nd = dist[cur] + 1;
    for (int k = 0; k < 4; k++) {
      int nx = cx + dirs[k][0], ny = cy + dirs[k][1];
      if (nx < minx || nx > maxx || ny < miny || ny > maxy) continue;
      int nidx = ny * SGRID_W + nx;
      if (dist[nidx] != 255) continue;            // already reached
      if (snakeCellOnDigit(nx, ny)) continue;     // flood through open cells only
      if (snakeCellOnBody(nx, ny)) continue;
      dist[nidx] = nd;
      queue[qtail++] = nidx;
    }
  }

  // Follow the gradient: smallest-distance free neighbour, never reversing.
  // On a tie prefer continuing straight so the path looks smooth, not jittery.
  int hx = snake_body[0].cx, hy = snake_body[0].cy;
  int bestDx = 0, bestDy = 0, bestDist = 256;
  bool found = false;
  for (int k = 0; k < 4; k++) {
    int dx = dirs[k][0], dy = dirs[k][1];
    if (dx == -snake_dir_x && dy == -snake_dir_y) continue;  // no reversing
    int nx = hx + dx, ny = hy + dy;
    if (!snakeCellFree(nx, ny, minx, maxx, miny, maxy)) continue;
    int nd = dist[ny * SGRID_W + nx];
    if (nd == 255) continue;                      // unreachable from target
    bool straight = (dx == snake_dir_x && dy == snake_dir_y);
    if (nd < bestDist || (nd == bestDist && straight)) {
      bestDist = nd;
      bestDx = dx;
      bestDy = dy;
      found = true;
    }
  }
  if (found) {
    snake_dir_x = bestDx;
    snake_dir_y = bestDy;
  }
  return found;
}

// Top-level steering toward (tcx,tcy): try the flow-field first, fall back to
// greedy if the target is temporarily unreachable, and as a last resort take
// any free cell (or reverse) so the snake can never march off the screen.
static void snakeSteer(int tcx, int tcy) {
  if (snakeFlowDir(tcx, tcy)) return;
  if (snakeChooseDir(tcx, tcy)) return;

  int minx, maxx, miny, maxy;
  snakeBounds(minx, maxx, miny, maxy);
  const int dirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
  for (int k = 0; k < 4; k++) {
    int nx = snake_body[0].cx + dirs[k][0], ny = snake_body[0].cy + dirs[k][1];
    if (snakeCellFree(nx, ny, minx, maxx, miny, maxy)) {
      snake_dir_x = dirs[k][0];
      snake_dir_y = dirs[k][1];
      return;
    }
  }
  // Fully enclosed (effectively impossible on this open grid): turn back rather
  // than keep driving into a wall and vanish off the edge.
  snake_dir_x = -snake_dir_x;
  snake_dir_y = -snake_dir_y;
}

// Advance the head one cell and drag the body along, growing toward target_len.
static void snakeAdvance() {
  int newLen = snake_body_len;
  if (newLen < snake_target_len && newLen < SNAKE_MAX_LEN) newLen++;
  for (int i = newLen - 1; i > 0; i--) snake_body[i] = snake_body[i - 1];
  snake_body[0].cx += snake_dir_x;
  snake_body[0].cy += snake_dir_y;
  snake_body_len = newLen;
}

// ========== Eating: digit leaves a few pellets, snake eats them one by one ==========
static void snakeStartEatDigit(int changeIndex) {
  snake_eating_idx = snake_change_idx[changeIndex];
  snake_eat_val = snake_change_val[changeIndex];
  snake_eat_steps = 0;
  snake_phase = SNAKE_EAT;

  // Collect the lit glyph pixels of the OLD digit, then keep only a few of
  // them (evenly spread) so the snake can chase each one down individually.
  uint8_t oldVal = getDisplayedDigitValue(snake_eating_idx);
  int gy = snakeTimeY();
  int litX[SNAKE_MAX_PELLETS], litY[SNAKE_MAX_PELLETS], litN = 0;
  for (int row = 0; row < 7; row++) {
    uint8_t bits = snakeDigitGlyph[oldVal][row];
    for (int col = 0; col < 5; col++) {
      if ((bits >> (4 - col)) & 1) {
        litX[litN] = DIGIT_X[snake_eating_idx] + col * SNAKE_PELLET_PITCH;
        litY[litN] = gy + row * SNAKE_PELLET_PITCH;
        litN++;
      }
    }
  }
  int target = SNAKE_PELLETS_PER_DIGIT;
  if (target > litN) target = litN;
  snake_pellet_count = 0;
  for (int t = 0; t < target; t++) {
    int s = (t * litN) / target;  // even spread across the glyph
    snake_pellets[snake_pellet_count].px = litX[s];
    snake_pellets[snake_pellet_count].py = litY[s];
    snake_pellets[snake_pellet_count].active = true;
    snake_pellet_count++;
  }
  snake_pellets_left = snake_pellet_count;
}

// Reveal the new digit and move on once the snake has cleared the spot.
static void snakeRevealAndAdvance() {
  updateDisplayedTimeDigit(snake_leaving_idx, snake_leaving_val);
  triggerDigitBounce(snake_leaving_idx);
  snake_leaving_idx = -1;
  snake_cur_change++;
  if (snake_cur_change < snake_num_changes) {
    snakeStartEatDigit(snake_cur_change);
  } else {
    snake_phase = SNAKE_ROAM;
    snakeSpawnFood();
  }
}

static void updateSnakeAnimation(struct tm *timeinfo) {
  unsigned long now = millis();
  updateDigitBounce();
  if (now - last_snake_update < (unsigned long)snakeStepIntervalMs()) return;
  last_snake_update = now;

  int seconds = timeinfo->tm_sec;
  int minute = timeinfo->tm_min;
  if (minute != last_minute_snake) {
    last_minute_snake = minute;
    snake_triggered = false;
  }

  if (seconds >= SNAKE_TRIGGER_SECOND && !snake_triggered &&
      snake_phase == SNAKE_ROAM) {
    snake_triggered = true;
    time_overridden = true;
    time_override_start = millis();
    calculateTargetDigits(displayed_hour, displayed_min, displayed_is_pm);

    snake_num_changes = 0;
    for (int i = 0; i < num_targets; i++) {
      if (target_digit_index[i] != 2) {  // skip the colon
        snake_change_idx[snake_num_changes] = target_digit_index[i];
        snake_change_val[snake_num_changes] = target_digit_values[i];
        snake_num_changes++;
      }
    }
    snake_cur_change = 0;
    if (snake_num_changes > 0) {
      snake_food_active = false;
      snakeStartEatDigit(0);
    } else {
      time_overridden = false;  // only the colon changed
    }
  }

  if (snake_phase == SNAKE_EAT) {
    // Chase the nearest remaining pellet.
    int hx = snake_body[0].cx, hy = snake_body[0].cy;
    int bestD = 0x7fffffff, tcx = hx, tcy = hy;
    bool found = false;
    for (int i = 0; i < snake_pellet_count; i++) {
      if (!snake_pellets[i].active) continue;
      int pcx = snake_pellets[i].px / SCELL;
      int pcy = snake_pellets[i].py / SCELL;
      int d = abs(pcx - hx) + abs(pcy - hy);
      if (d < bestD) { bestD = d; tcx = pcx; tcy = pcy; found = true; }
    }
    // Done when every pellet is eaten, or give up if the snake has spent too
    // long chasing one it has accidentally walled off with its own body. The
    // EAT phase has no other exit, so without this cap rare trap geometry could
    // hang it indefinitely - freezing the clock on the pellet frame until the
    // 60s time-override safety net fired. Mirrors SNAKE_LEAVE_MAX_STEPS.
    if (!found || snake_eat_steps >= SNAKE_EAT_MAX_STEPS) {
      snake_leaving_idx = snake_eating_idx;
      snake_leaving_val = snake_eat_val;
      snake_eating_idx = -1;
      snake_pellet_count = 0;
      snake_leave_steps = 0;
      snake_phase = SNAKE_LEAVE;
      return;
    }
    snakeSteer(tcx, tcy);
    snakeAdvance();
    snake_eat_steps++;
    // Eat only the pellet the head lands exactly on (one at a time).
    hx = snake_body[0].cx;
    hy = snake_body[0].cy;
    for (int i = 0; i < snake_pellet_count; i++) {
      if (snake_pellets[i].active &&
          snake_pellets[i].px / SCELL == hx && snake_pellets[i].py / SCELL == hy) {
        snake_pellets[i].active = false;
        snake_pellets_left--;
      }
    }
    return;
  }

  if (snake_phase == SNAKE_LEAVE) {
    // Head away from the vacated spot (toward the next digit or the food).
    int tcx, tcy;
    if (snake_cur_change + 1 < snake_num_changes) {
      int nidx = snake_change_idx[snake_cur_change + 1];
      tcx = (DIGIT_X[nidx] + SNAKE_DIGIT_W / 2) / SCELL;
      tcy = (snakeTimeY() + SNAKE_DIGIT_H + 2) / SCELL;
    } else {
      if (!snake_food_active) snakeSpawnFood();
      tcx = snake_food_cx;
      tcy = snake_food_cy;
    }
    snakeSteer(tcx, tcy);
    snakeAdvance();
    snake_leave_steps++;
    if (snakeBodyClearOfDigit(snake_leaving_idx) ||
        snake_leave_steps > SNAKE_LEAVE_MAX_STEPS) {
      snakeRevealAndAdvance();
    }
    return;
  }

  // ROAM
  if (!snake_food_active) snakeSpawnFood();
  snakeSteer(snake_food_cx, snake_food_cy);
  snakeAdvance();
  int hx = snake_body[0].cx, hy = snake_body[0].cy;
  if (snake_food_active && hx == snake_food_cx && hy == snake_food_cy) {
    // Eat food: grow up to a safe cap, then snap back to the base length.
    int growCap = snake_base_len + 8;
    if (growCap > SNAKE_MAX_LEN) growCap = SNAKE_MAX_LEN;
    if (snake_target_len < growCap) snake_target_len++;
    else snake_target_len = snake_base_len;
    snakeSpawnFood();
  }
}

// ========== Display ==========
void displayClockWithSnake() {
  if (!snake_init_done) resetSnakeAnimation();

  struct tm timeinfo;
  if (!getTimeWithTimeout(&timeinfo)) {
    display.setTextSize(1);
    display.setCursor(20, 28);
    display.print(ntpSynced ? "Time Error" : "Syncing time...");
    return;
  }

  updateSnakeAnimation(&timeinfo);

  if (!time_overridden) syncDisplayedTime(&timeinfo);
  maintainTimeOverride(&timeinfo, snake_phase == SNAKE_ROAM);

  int gy = snakeTimeY();

  // Optional date row (top)
  if (settings.snakeShowDate) {
    display.setTextSize(1);
    char dateStr[12];
    switch (settings.dateFormat) {
      case 0: sprintf(dateStr, "%02d/%02d/%04d", timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900); break;
      case 1: sprintf(dateStr, "%02d/%02d/%04d", timeinfo.tm_mon + 1, timeinfo.tm_mday, timeinfo.tm_year + 1900); break;
      case 2: sprintf(dateStr, "%04d-%02d-%02d", timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday); break;
      case 3: sprintf(dateStr, "%02d.%02d.%04d", timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900); break;
    }
    display.setCursor((SCREEN_WIDTH - 60) / 2, 4);
    display.print(dateStr);
  }
  drawMeridiemIndicator(110, 4, displayed_is_pm);

  // Optional Nokia arena frame
  if (settings.snakeWallBorder) {
    int top = settings.snakeShowDate ? 12 : 0;
    display.drawRect(0, top, SCREEN_WIDTH, SCREEN_HEIGHT - top, DISPLAY_WHITE);
  }

  // Time digits (size 3). The digit being eaten is shown as its leftover
  // pellets; the digit being vacated is left blank until the snake clears it.
  display.setTextSize(3);
  char dch[5];
  dch[0] = '0' + displayed_hour / 10;
  dch[1] = '0' + displayed_hour % 10;
  dch[2] = shouldShowColon() ? ':' : ' ';
  dch[3] = '0' + displayed_min / 10;
  dch[4] = '0' + displayed_min % 10;

  for (int i = 0; i < 5; i++) {
    if ((snake_phase == SNAKE_EAT && i == snake_eating_idx) ||
        (snake_phase == SNAKE_LEAVE && i == snake_leaving_idx)) {
      continue;  // pellets / blank handled separately
    }
    if (i == 2) {
      display.setCursor(DIGIT_X[i], gy);
      display.print(dch[i]);
      continue;
    }
    display.setCursor(DIGIT_X[i], gy + (int)digit_offset_y[i]);
    display.print(dch[i]);
  }

  // Pellets left from the digit being eaten - drawn just like the food the
  // snake normally chases (same 3px size, same blink).
  if (snake_phase == SNAKE_EAT && (millis() / 300) % 2 == 0) {
    for (int i = 0; i < snake_pellet_count; i++) {
      if (snake_pellets[i].active)
        display.fillRect(snake_pellets[i].px, snake_pellets[i].py, 3, 3, DISPLAY_WHITE);
    }
  }

  // Food (blinking) while roaming
  if (snake_food_active && snake_phase == SNAKE_ROAM && (millis() / 300) % 2 == 0) {
    display.fillRect(snake_food_cx * SCELL, snake_food_cy * SCELL, 3, 3, DISPLAY_WHITE);
  }

  // Body (tail first so the head sits on top)
  for (int i = snake_body_len - 1; i >= 1; i--) {
    display.fillRect(snake_body[i].cx * SCELL, snake_body[i].cy * SCELL, 3, 3, DISPLAY_WHITE);
  }

  // Head + eye
  int hx = snake_body[0].cx * SCELL;
  int hy = snake_body[0].cy * SCELL;
  display.fillRect(hx, hy, 3, 3, DISPLAY_WHITE);
  display.drawPixel(hx + 1 + snake_dir_x, hy + 1 + snake_dir_y, DISPLAY_BLACK);

  if (!wifiConnected) drawNoWiFiIcon(0, 0);
}
