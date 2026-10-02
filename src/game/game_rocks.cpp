/*
 * SmallOLED-PCMonitor - Space Rocks (game mode)
 *
 * Vector-style space shooter in the field under the top band; everything
 * wraps around the edges. The ship turns, thrusts with inertia and fires
 * up to four shots; big rocks split into two medium, medium into two
 * small, small ones burst. A cleared wave brings a bigger one. After a hit
 * the ship respawns in the centre, blinking and invulnerable for a moment.
 *
 * Controls: d-pad / left stick left-right turn, up or RT thrust,
 * A or RB fire.
 */

#include "../config/user_config.h"

#if GAMEPAD_ENABLED

#include "../config/config.h"
#include "../display/display.h"
#include "game_mode.h"
#include "game_common.h"

#define RK_MAX 18
#define RK_VERTS 8
#define SHOTS 4
#define SHOT_LIFE 0.8f
#define SHOT_SPEED 95.0f
#define SHOT_COOLDOWN_MS 160
#define TURN_RATE 4.6f       // rad/s
#define THRUST 75.0f         // px/s^2
#define DRAG 0.55f           // velocity kept per second
#define SHIP_MAX 65.0f
#define SHIP_R 4
#define INVULN_MS 2500
#define LIVES 3
#define FIELD_H (SCREEN_HEIGHT - GAME_TOP)

static const float ROCK_R[3] = {9.0f, 5.5f, 3.0f};
static const uint16_t ROCK_SCORE[3] = {20, 50, 100};

struct Rock {
  float x, y, vx, vy, ang, spin;
  uint8_t size;  // 0 big, 1 medium, 2 small
  uint8_t shape[RK_VERTS];  // radius per vertex, percent
  bool alive;
};
struct Shot {
  float x, y, vx, vy, life;
};

static Rock rocks[RK_MAX];
static Shot shots[SHOTS];
static float shipX, shipY, shipVX, shipVY, shipA;
static bool thrusting;
static unsigned long invulnUntil, lastShot, lastTick;
static uint8_t lives, wave;
static uint32_t score, hiScore;
static GamePhase phase;
static bool newHi;

static float wrapX(float x) { return x < 0 ? x + SCREEN_WIDTH : (x >= SCREEN_WIDTH ? x - SCREEN_WIDTH : x); }
static float wrapY(float y) {
  if (y < GAME_TOP) return y + FIELD_H;
  if (y >= SCREEN_HEIGHT) return y - FIELD_H;
  return y;
}

// Shortest wrapped distance squared.
static float dist2(float ax, float ay, float bx, float by) {
  float dx = fabsf(ax - bx), dy = fabsf(ay - by);
  if (dx > SCREEN_WIDTH / 2) dx = SCREEN_WIDTH - dx;
  if (dy > FIELD_H / 2) dy = FIELD_H - dy;
  return dx * dx + dy * dy;
}

static void spawnRock(float x, float y, uint8_t size, float speedScale) {
  for (auto &r : rocks) {
    if (r.alive) continue;
    float a = gameRandf(0, TWO_PI), v = gameRandf(8, 16) * speedScale * (1 + size * 0.45f);
    r = {x, y, cosf(a) * v, sinf(a) * v, gameRandf(0, TWO_PI), gameRandf(-1.5f, 1.5f), size, {}, true};
    for (auto &s : r.shape) s = 70 + esp_random() % 40;
    return;
  }
}

static void startWave() {
  float scale = 1 + (wave - 1) * 0.12f;
  for (int i = 0; i < 2 + wave && i < 6; i++) {
    float x, y;
    do {  // keep clear of the ship
      x = gameRandf(0, SCREEN_WIDTH);
      y = gameRandf(GAME_TOP, SCREEN_HEIGHT);
    } while (dist2(x, y, shipX, shipY) < 30 * 30);
    spawnRock(x, y, 0, scale);
  }
}

static void respawnShip(unsigned long now) {
  shipX = SCREEN_WIDTH / 2;
  shipY = GAME_TOP + FIELD_H / 2;
  shipVX = shipVY = 0;
  shipA = -HALF_PI;
  invulnUntil = now + INVULN_MS;
}

void rocksReset() {
  hiScore = gameLoadHi("rocksHi");
  for (auto &r : rocks) r.alive = false;
  for (auto &s : shots) s.life = 0;
  respawnShip(millis());
  lives = LIVES;
  wave = 1;
  score = 0;
  newHi = false;
  startWave();
  phase = G_READY;
}

static void hitRock(Rock &r) {
  r.alive = false;
  score += ROCK_SCORE[r.size];
  gameRumble(r.size == 0 ? 35 : 0, 25, 50);
  if (r.size < 2) {
    float scale = 1 + (wave - 1) * 0.12f;
    spawnRock(r.x, r.y, r.size + 1, scale);
    spawnRock(r.x, r.y, r.size + 1, scale);
  }
}

static void shipHit(unsigned long now) {
  gameRumble(100, 70, 400);
  if (--lives == 0) {
    phase = G_OVER;
    newHi = gameSubmitScore("rocksHi", score);
    if (newHi) hiScore = score;
    return;
  }
  respawnShip(now);
}

static void update(const GamepadState &in, float dt, unsigned long now) {
  uint16_t b = in.buttons;
  if (b & GP_LEFT) shipA -= TURN_RATE * dt;
  if (b & GP_RIGHT) shipA += TURN_RATE * dt;
  thrusting = (b & GP_UP) || in.rt > 200;
  if (thrusting) {
    shipVX += cosf(shipA) * THRUST * dt;
    shipVY += sinf(shipA) * THRUST * dt;
  }
  float drag = powf(DRAG, dt);
  shipVX *= drag;
  shipVY *= drag;
  float sp = sqrtf(shipVX * shipVX + shipVY * shipVY);
  if (sp > SHIP_MAX) {
    shipVX *= SHIP_MAX / sp;
    shipVY *= SHIP_MAX / sp;
  }
  shipX = wrapX(shipX + shipVX * dt);
  shipY = wrapY(shipY + shipVY * dt);

  if ((in.pressed & (GP_A | GP_RB)) && now - lastShot >= SHOT_COOLDOWN_MS) {
    for (auto &s : shots) {
      if (s.life > 0) continue;
      s = {shipX + cosf(shipA) * 5, shipY + sinf(shipA) * 5, shipVX + cosf(shipA) * SHOT_SPEED,
           shipVY + sinf(shipA) * SHOT_SPEED, SHOT_LIFE};
      lastShot = now;
      break;
    }
  }

  for (auto &s : shots) {
    if (s.life <= 0) continue;
    s.life -= dt;
    s.x = wrapX(s.x + s.vx * dt);
    s.y = wrapY(s.y + s.vy * dt);
  }

  bool any = false;
  for (auto &r : rocks) {
    if (!r.alive) continue;
    any = true;
    r.x = wrapX(r.x + r.vx * dt);
    r.y = wrapY(r.y + r.vy * dt);
    r.ang += r.spin * dt;
    float rr = ROCK_R[r.size];
    for (auto &s : shots) {
      if (s.life > 0 && dist2(s.x, s.y, r.x, r.y) < rr * rr) {
        s.life = 0;
        hitRock(r);
        break;
      }
    }
    if (r.alive && now >= invulnUntil && dist2(shipX, shipY, r.x, r.y) < (rr + SHIP_R - 1) * (rr + SHIP_R - 1)) {
      hitRock(r);
      shipHit(now);
      if (phase != G_PLAY) return;
    }
  }
  if (!any) {
    wave++;
    invulnUntil = now + 1500;
    startWave();
  }
}

// Objects straddling an edge are drawn unwrapped; the band is repainted on top.
static void line(float x0, float y0, float x1, float y1) {
  display.drawLine((int)x0, (int)y0, (int)x1, (int)y1, DISPLAY_WHITE);
}

static void drawRock(const Rock &r) {
  float rr = ROCK_R[r.size];
  float px = 0, py = 0;
  for (int i = 0; i <= RK_VERTS; i++) {
    int k = i % RK_VERTS;
    float a = r.ang + k * TWO_PI / RK_VERTS, d = rr * r.shape[k] / 100.0f;
    float x = r.x + cosf(a) * d, y = r.y + sinf(a) * d;
    if (i) line(px, py, x, y);
    px = x;
    py = y;
  }
}

static void drawShip(unsigned long now) {
  if (now < invulnUntil && (now / 120) % 2) return;
  float c = cosf(shipA), s = sinf(shipA);
  auto pt = [&](float fwd, float side, float &x, float &y) {
    x = shipX + c * fwd - s * side;
    y = shipY + s * fwd + c * side;
  };
  float nx, ny, lx, ly, rx, ry, bx, by;
  pt(5, 0, nx, ny);
  pt(-4, -3.5f, lx, ly);
  pt(-4, 3.5f, rx, ry);
  pt(-2.5f, 0, bx, by);
  line(nx, ny, lx, ly);
  line(nx, ny, rx, ry);
  line(lx, ly, bx, by);
  line(rx, ry, bx, by);
  if (thrusting && (now / 60) % 2) {
    float fx, fy;
    pt(-7, 0, fx, fy);
    line(bx, by, fx, fy);
  }
}

static void draw(unsigned long now) {
  for (const auto &r : rocks)
    if (r.alive) drawRock(r);
  for (const auto &s : shots)
    if (s.life > 0) display.drawPixel((int)s.x, (int)s.y, DISPLAY_WHITE);
  if (phase != G_OVER) drawShip(now);

  // Band over everything, so wrapped lines never scribble on the score.
  display.fillRect(0, 0, SCREEN_WIDTH, GAME_TOP, DISPLAY_BLACK);
  gameDrawScore(0, score);
  for (int i = 0; i < lives; i++) {
    int x = 52 + i * 6;
    display.drawLine(x, 7, x + 2, 2, DISPLAY_WHITE);
    display.drawLine(x + 2, 2, x + 4, 7, DISPLAY_WHITE);
  }
  display.setCursor(74, 1);
  display.print("W");
  display.print(wave);
  gameDrawClock();
}

bool rocksFrame(const GamepadState &in, bool padLost) {
  unsigned long now = millis();
  switch (gameFlow(phase, in, padLost)) {
    case STEP_QUIT:
      return false;
    case STEP_RESTART:
      rocksReset();
      phase = G_PLAY;
      lastTick = now;
      invulnUntil = now + INVULN_MS;
      break;
    case STEP_RESUMED:
      lastTick = now;
      break;
    case STEP_PLAY:
      update(in, gameDt(lastTick, now), now);
      break;
    case STEP_HOLD:
      break;
  }

  draw(now);
  char info[16];
  snprintf(info, sizeof(info), phase == G_READY ? "HI %lu" : "%lu pts",
           (unsigned long)(phase == G_READY ? hiScore : score));
  gameDrawOverlay(phase, padLost, "SPACE ROCKS", phase == G_PAUSED ? nullptr : info, newHi,
                  "L/R turn, up thrust\nA or RB: fire");
  return true;
}

#endif // GAMEPAD_ENABLED
