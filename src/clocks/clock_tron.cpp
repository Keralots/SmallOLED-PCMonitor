/*
 * SmallOLED-PCMonitor - TRON Clock (style 16)
 *
 * Light cycles duel in an arena; on a time change one retraces the changed
 * digit stroke by stroke.
 *
 * Ported from AnimatedPixelClock, whose colour work becomes geometry: the two
 * cycles differ by trail pattern (solid vs dotted) instead of hue, the erase
 * phase retracts trails from the tail instead of fading them, and the neon
 * halo is dropped. The sprite ink map becomes a threshold - level 1 (wheel
 * hubs) stays dark, which keeps the wheels readable.
 */

#include "clocks.h"
#include "clock_globals.h"
#include "../display/display.h"
#include <math.h>
#include <string.h>

namespace {

const int GX = 64, GY = 32, TRAIL = 96;
const int BIKE_RADIUS = 3;
const uint32_t STEP_MS = 80;
const int digitX[4] = {14, 38, 74, 98};
const int digitY = 20;
const int dx[4] = {1, 0, -1, 0}, dy[4] = {0, 1, 0, -1};
// a,b,c,d,e,f,g segments, with shared vertices for a continuous tracing route.
const uint8_t masks[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};
const uint8_t ends[7][2] = {{0, 1}, {1, 3}, {3, 5}, {4, 5}, {2, 4}, {0, 2}, {2, 3}};
const int vx[6] = {0, 12, 0, 12, 0, 12}, vy[6] = {0, 0, 12, 12, 24, 24};

struct Point { uint8_t x, y; };
struct Bike {
  int x, y, px, py, dir, first, count;
  Point trail[TRAIL];
  uint32_t stepped, crashed;
  bool dead;
};

Bike bikes[2];
uint8_t occupied[GY][GX];
enum Phase { DUEL, ERASE, APPROACH, TRACE, RETURN };
Phase phase;
int shown[4], target[4], activeDigit, builder, nextBuilder, traceValue;
uint8_t traceNodes[15], traceCount, traceIndex, visited, built;
float buildX, buildY;
int buildDir;
Point approach[4];
int approachCount, approachIndex;
uint32_t lastFrame, phaseStart;
bool initialized;

bool blocked(int x, int y) {
  if (x < 0 || x >= GX || y < 0 || y >= GY) return true;
  int px = x * 2, py = y * 2;
  for (int i = 0; i < 4; i++)
    if (px >= digitX[i] - 2 && px <= digitX[i] + 14 && py >= 18 && py <= 46) return true;
  // Keep the colon readable in the middle of the arena.
  return px >= 62 && px <= 68 && py >= 26 && py <= 38;
}

void clearTrail(int n) {
  Bike& b = bikes[n];
  for (int i = 0; i < b.count; i++) {
    Point p = b.trail[(b.first + i) % TRAIL];
    if (occupied[p.y][p.x] == n + 1) occupied[p.y][p.x] = 0;
  }
  b.first = b.count = 0;
}

// Used by both the ring buffer and the erase phase.
void dropOldest(int n) {
  Bike& b = bikes[n];
  if (!b.count) return;
  Point p = b.trail[b.first];
  if (occupied[p.y][p.x] == n + 1) occupied[p.y][p.x] = 0;
  b.first = (b.first + 1) % TRAIL;
  b.count--;
}

void addTrail(int n) {
  Bike& b = bikes[n];
  if (b.count == TRAIL) dropOldest(n);
  b.trail[(b.first + b.count) % TRAIL] = {(uint8_t)b.x, (uint8_t)b.y};
  b.count++;
  occupied[b.y][b.x] = n + 1;
}

int clearance(int x, int y, int d) {
  int s = 0;
  for (int i = 1; i <= 8; i++) {
    int xx = x + dx[d] * i, yy = y + dy[d] * i;
    if (blocked(xx, yy) || occupied[yy][xx]) break;
    s++;
  }
  return s;
}

void spawn(int n, uint32_t now) {
  Bike& b = bikes[n];
  clearTrail(n);
  for (int attempt = 0; attempt < 128; attempt++) {
    int x = 2 + random(60), y = random(2) ? 6 : 26;
    if (blocked(x, y) || occupied[y][x]) continue;
    b.x = b.px = x; b.y = b.py = y; b.dir = n == 0 ? 0 : 2;
    b.dead = false; b.stepped = now; addTrail(n); return;
  }
  // Retry after the other cycle has moved; never place a bike inside a wall.
  b.dead = true; b.crashed = now;
}

void updateBike(int n, uint32_t now) {
  Bike& b = bikes[n];
  if (phase != DUEL && builder == n) return;
  if (b.dead) { if (now - b.crashed >= 650) spawn(n, now); return; }
  // Advance on the real time grid. Snapping every step to the frame that
  // noticed it turned the 80ms cadence into an alternating 80/96ms one, which
  // reads as short speed changes. Keep the remainder; bound long catch-ups.
  if (now - b.stepped > 400) b.stepped = now - STEP_MS - (now - b.stepped) % STEP_MS;
  while (now - b.stepped >= STEP_MS) {
    b.stepped += STEP_MS;
    int choices[3] = {b.dir, (b.dir + 1) % 4, (b.dir + 3) % 4};
    int selected = -1, best = -100;
    bool turn = random(11) == 0;
    for (int j = 0; j < 3; j++) {
      int free = clearance(b.x, b.y, choices[j]);
      if (!free) continue;
      int score = free * 3 + random(5) + (j == 0 ? (turn ? 0 : 12) : (turn ? 15 : 0));
      if (score > best) { best = score; selected = choices[j]; }
    }
    if (selected < 0) { b.dead = true; b.crashed = now; return; }
    b.px = b.x; b.py = b.y; b.dir = selected;
    b.x += dx[selected]; b.y += dy[selected]; addTrail(n);
  }
}

// The original's dim halo would only thicken the stroke at one ink level.
void neonLine(int x, int y, int xx, int yy) {
  display.drawLine(x, y, xx, yy, DISPLAY_WHITE);
}

void drawBike(int x, int y, int d) {
  // Compact motorcycle profile: two round wheel rims, low fairing and a
  // crouched rider. Rotate the whole silhouette with the travel direction.
  // The reference sprite faces right; dark hubs keep both wheels distinct.
  static const uint8_t profile[5][7] = {
    {0, 0, 0, 4, 0, 0, 0}, // helmet
    {0, 0, 3, 3, 4, 2, 0}, // crouched rider / handlebars
    {0, 2, 3, 3, 3, 2, 0}, // low light-cycle fairing
    {2, 1, 2, 3, 2, 1, 2}, // rear and front wheels
    {0, 2, 0, 0, 0, 2, 0}
  };
  // Narrow overhead alternative: inline tyres, enclosed fairing and canopy.
  // Same maximum footprint so steering and digit tracing need no changes.
  static const uint8_t overhead[5][7] = {
    {0, 0, 0, 0, 0, 0, 0},
    {0, 0, 2, 3, 2, 0, 0},
    {1, 2, 3, 4, 3, 2, 1},
    {0, 0, 2, 3, 2, 0, 0},
    {0, 0, 0, 0, 0, 0, 0}
  };
  const uint8_t (*sprite)[7] = settings.tronBikeStyle == 1 ? overhead : profile;
  int ax = dx[d], ay = dy[d], sx = -ay, sy = ax;
  // Keep the whole silhouette on the panel; coordinates here are screen pixels.
  int halfWidth = settings.tronBikeStyle == 1 ? 1 : 2;
  int rx = ax ? BIKE_RADIUS : halfWidth, ry = ay ? BIKE_RADIUS : halfWidth;
  x = constrain(x, rx, SCREEN_WIDTH - 1 - rx);
  y = constrain(y, ry, SCREEN_HEIGHT - 1 - ry);
  for (int along = -BIKE_RADIUS; along <= BIKE_RADIUS; along++) {
    for (int across = -2; across <= 2; across++) {
      // Ink 1 is the dim wheel hub: unlit keeps the wheels from merging.
      if (sprite[across + 2][along + BIKE_RADIUS] < 2) continue;
      display.drawPixel(x + ax * along + sx * across, y + ay * along + sy * across,
                        DISPLAY_WHITE);
    }
  }
}

void drawDuel() {
  for (int n = 0; n < 2; n++) {
    Bike& b = bikes[n];
    // Dotted trail stands in for the second colour.
    const bool dotted = n == 1;
    for (int i = 1; i < b.count; i++) {
      if (dotted && (i & 1)) continue;
      Point a = b.trail[(b.first + i - 1) % TRAIL], p = b.trail[(b.first + i) % TRAIL];
      display.drawLine(a.x * 2, a.y * 2, p.x * 2, p.y * 2, DISPLAY_WHITE);
    }
    if (phase != DUEL && builder == n) continue;
    if (b.dead) {
      // Expanding debris ring; growing past the arena edge ends it.
      int r = 2 + (millis() - b.crashed) / 65;
      for (int d = 0; d < 4; d++)
        display.drawPixel(b.x * 2 + dx[d] * r, b.y * 2 + dy[d] * r, DISPLAY_WHITE);
    } else {
      float t = fminf((millis() - b.stepped) / (float)STEP_MS, 1);
      drawBike(lroundf((b.px + (b.x - b.px) * t) * 2), lroundf((b.py + (b.y - b.py) * t) * 2),
               b.dir);
    }
  }
}

// Walk every lit edge and retrace it on return. Each digit is one connected
// graph, so the motorcycle never jumps between disconnected strokes.
void traceGraph(int node) {
  for (int e = 0; e < 7; e++) {
    if (!(masks[traceValue] & (1 << e)) || (visited & (1 << e))) continue;
    int next = -1;
    if (ends[e][0] == node) next = ends[e][1];
    else if (ends[e][1] == node) next = ends[e][0];
    if (next < 0) continue;
    visited |= 1 << e; traceNodes[traceCount++] = next;
    traceGraph(next); traceNodes[traceCount++] = node;
  }
}

void beginChange(uint32_t now) {
  activeDigit = -1;
  for (int i = 0; i < 4; i++) if (shown[i] != target[i]) { activeDigit = i; break; }
  if (activeDigit < 0) return;
  builder = nextBuilder; nextBuilder = 1 - nextBuilder;
  if (bikes[builder].dead && !bikes[1 - builder].dead) builder = 1 - builder;
  Bike& b = bikes[builder];
  // A crashed cycle finishes respawning before it can be assigned a digit.
  if (b.dead) { activeDigit = -1; return; }
  buildX = b.x * 2; buildY = b.y * 2; buildDir = b.dir;
  traceValue = target[activeDigit];
  int start = 0;
  for (int e = 0; e < 7; e++) if (masks[traceValue] & (1 << e)) { start = ends[e][0]; break; }
  visited = built = 0; traceCount = 1; traceNodes[0] = start; traceGraph(start); traceIndex = 1;
  approachCount = approachIndex = 0;
  // From below the clock, first use a clear vertical passage. All subsequent
  // approach points lie above the digits, followed by entry into the active one.
  if (buildY > 46) {
    const int lanes[5] = {6, 32, 56, 92, 122};
    int lane = 0;
    for (int i = 1; i < 5; i++) if (fabsf(lanes[i] - buildX) < fabsf(lanes[lane] - buildX)) lane = i;
    approach[approachCount++] = {(uint8_t)lanes[lane], (uint8_t)buildY};
    approach[approachCount++] = {(uint8_t)lanes[lane], 14};
  } else if (buildY > 38 && buildX >= 62 && buildX <= 68) {
    approach[approachCount++] = {56, (uint8_t)buildY};
    approach[approachCount++] = {56, 14};
  } else approach[approachCount++] = {(uint8_t)buildX, 14};
  approach[approachCount++] = {(uint8_t)(digitX[activeDigit] + vx[start]), 14};
  approach[approachCount++] = {(uint8_t)(digitX[activeDigit] + vx[start]), (uint8_t)(digitY + vy[start])};
  phase = ERASE; phaseStart = now;
}

bool moveBuilder(float x, float y, float& distance) {
  float xx = x - buildX, yy = y - buildY, total = fabsf(xx) + fabsf(yy);
  if (total > 0.01f) buildDir = fabsf(xx) > 0.01f ? (xx > 0 ? 0 : 2) : (yy > 0 ? 1 : 3);
  if (total <= distance) { buildX = x; buildY = y; distance -= total; return true; }
  if (fabsf(xx) > 0.01f) buildX += xx > 0 ? distance : -distance;
  else buildY += yy > 0 ? distance : -distance;
  distance = 0; return false;
}

void updateTrace(float dt, uint32_t now) {
  if (phase == DUEL) { beginChange(now); return; }
  if (phase == ERASE) {
    // Retract from the tail instead of dimming; same 320ms beat.
    uint32_t elapsed = now - phaseStart;
    for (int n = 0; n < 2; n++) {
      int keep = bikes[n].count;
      if (elapsed >= 320) keep = 0;
      else keep = bikes[n].count - (int)(bikes[n].count * elapsed / 320);
      while (bikes[n].count > keep) dropOldest(n);
    }
    if (elapsed >= 320) {
      clearTrail(0); clearTrail(1);
      if (!bikes[1 - builder].dead) addTrail(1 - builder);
      phase = APPROACH; phaseStart = now;
    }
    return;
  }
  float distance = dt * (phase == TRACE ? 110 : 85);
  if (phase == APPROACH) {
    while (approachIndex < approachCount &&
           moveBuilder(approach[approachIndex].x, approach[approachIndex].y, distance))
      approachIndex++;
    if (approachIndex == approachCount) { phase = TRACE; phaseStart = now; }
  } else if (phase == TRACE) {
    while (traceIndex < traceCount) {
      int a = traceNodes[traceIndex - 1], b = traceNodes[traceIndex];
      if (!moveBuilder(digitX[activeDigit] + vx[b], digitY + vy[b], distance)) break;
      for (int e = 0; e < 7; e++)
        if ((ends[e][0] == a && ends[e][1] == b) || (ends[e][1] == a && ends[e][0] == b))
          built |= 1 << e;
      traceIndex++;
    }
    if (traceIndex == traceCount) { shown[activeDigit] = traceValue; phase = RETURN; phaseStart = now; }
  } else if (phase == RETURN && moveBuilder(buildX, 14, distance)) {
    Bike& b = bikes[builder];
    b.x = b.px = (int)buildX / 2; b.y = b.py = 7;
    b.dir = builder == 0 ? 0 : 2; b.stepped = now;
    b.dead = occupied[b.y][b.x] != 0; b.crashed = now;
    if (!b.dead) addTrail(builder);
    phase = DUEL; activeDigit = -1;
  }
}

void drawDigits() {
  for (int i = 0; i < 4; i++) {
    int mask = masks[shown[i]];
    if (i == activeDigit) {
      // ERASE dimmed the outgoing digit away; at one ink level it just goes.
      if (phase == ERASE || phase == APPROACH) mask = 0;
      if (phase == TRACE) mask = built;
    }
    for (int e = 0; e < 7; e++) if (mask & (1 << e)) {
      int a = ends[e][0], b = ends[e][1];
      neonLine(digitX[i] + vx[a], digitY + vy[a], digitX[i] + vx[b], digitY + vy[b]);
    }
  }
  if (phase == TRACE && traceIndex < traceCount) {
    int a = traceNodes[traceIndex - 1];
    neonLine(digitX[activeDigit] + vx[a], digitY + vy[a], (int)buildX, (int)buildY);
  }
  if (shouldShowColon()) {
    display.fillRect(64, 28, 2, 2, DISPLAY_WHITE);
    display.fillRect(64, 36, 2, 2, DISPLAY_WHITE);
  }
}

}  // namespace

void resetTronAnimation() { initialized = false; }

// The cycles never stop, so this is true once a time is on screen.
bool tronIsAnimating() { return initialized; }

void displayClockWithTron() {
  struct tm t;
  if (!getTimeWithTimeout(&t)) {
    display.setTextSize(1);
    display.setTextColor(DISPLAY_WHITE);
    display.setCursor(20, 28);
    display.print("Syncing time...");
    return;
  }
  int hour, minute; bool pm;
  formatTimeForDisplay(t.tm_hour, t.tm_min, hour, minute, pm);
  target[0] = hour / 10; target[1] = hour % 10;
  target[2] = minute / 10; target[3] = minute % 10;
  uint32_t now = millis();
  if (!initialized) {
    memset(bikes, 0, sizeof(bikes)); memset(occupied, 0, sizeof(occupied));
    for (int i = 0; i < 4; i++) shown[i] = target[i];
    phase = DUEL; activeDigit = -1; nextBuilder = 0;
    spawn(0, now); spawn(1, now); lastFrame = now; initialized = true;
  }
  float dt = fminf((now - lastFrame) / 1000.0f, 0.05f);
  lastFrame = now;
  updateBike(0, now); updateBike(1, now); updateTrace(dt, now);

  // Grid and border were dim texture; at one ink level they compete with the
  // trails, so the grid is half as dense and both are optional.
  if (settings.tronShowGrid) {
    for (int x = 8; x < SCREEN_WIDTH; x += 16)
      for (int y = 8; y < SCREEN_HEIGHT - 2; y += 16) display.drawPixel(x, y, DISPLAY_WHITE);
  }
  if (settings.tronShowBorder) display.drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, DISPLAY_WHITE);
  drawDuel();
  drawDigits();
  if (phase != DUEL) drawBike((int)buildX, (int)buildY, buildDir);
  if (!settings.use24Hour) drawMeridiemIndicator(110, 1, pm);
  if (!wifiConnected) drawNoWiFiIcon(0, 0);
}
