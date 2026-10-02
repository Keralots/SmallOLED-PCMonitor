/*
 * SmallOLED-PCMonitor - Bricks (game mode)
 *
 * Paddle-and-ball brick breaker. 14x5 bricks of 8x3px under the top band,
 * a 2x2 ball and an 18px paddle at the bottom. Where the ball meets the
 * paddle sets its outgoing angle. The ball moves in sub-pixel steps so it
 * cannot tunnel through a 3px brick at speed. Clearing the wall starts the
 * next level a little faster; three lives.
 *
 * Controls: left stick (analog speed) or d-pad move, A launches.
 */

#include "../config/user_config.h"

#if GAMEPAD_ENABLED

#include "../config/config.h"
#include "../display/display.h"
#include "game_mode.h"
#include "game_common.h"

#define BR_COLS 14
#define BR_ROWS 5
#define BR_W 8
#define BR_H 3
#define BR_X0 1
#define BR_Y0 14
#define BR_PITCH_X 9
#define BR_PITCH_Y 4
#define BR_FULL ((1u << BR_COLS) - 1)
#define PAD_W 18
#define PAD_Y 60
#define PAD_SPEED 120.0f      // px/s at full deflection
#define BALL 2
#define BALL_SPEED 62.0f      // px/s on level 1
#define BALL_SPEED_LEVEL 8.0f
#define BALL_SPEED_MAX 120.0f
#define LIVES 3

static uint16_t bricks[BR_ROWS];
static float padX;                       // paddle centre
static float ballX, ballY, ballVX, ballVY, speed;
static bool attached;
static uint8_t lives, level;
static uint32_t score, hiScore;
static GamePhase phase;
static bool newHi;
static unsigned long lastTick;

static void attachBall() {
  attached = true;
  ballVX = ballVY = 0;
}

static void fillWall() {
  for (int r = 0; r < BR_ROWS; r++) bricks[r] = BR_FULL;
}

void bricksReset() {
  hiScore = gameLoadHi("bricksHi");
  fillWall();
  padX = SCREEN_WIDTH / 2;
  lives = LIVES;
  level = 1;
  score = 0;
  speed = BALL_SPEED;
  newHi = false;
  attachBall();
  phase = G_READY;
}

static void launch() {
  attached = false;
  float a = gameRandf(-0.5f, 0.5f);
  ballVX = speed * sinf(a);
  ballVY = -speed * cosf(a);
}

static void loseLife() {
  gameRumble(90, 40, 300);
  if (--lives == 0) {
    phase = G_OVER;
    newHi = gameSubmitScore("bricksHi", score);
    if (newHi) hiScore = score;
    return;
  }
  attachBall();
}

// Brick under a point, or false. Points in the gaps hit nothing.
static bool brickAt(float x, float y, int &col, int &row) {
  int px = (int)x - BR_X0, py = (int)y - BR_Y0;
  if (px < 0 || py < 0) return false;
  col = px / BR_PITCH_X;
  row = py / BR_PITCH_Y;
  if (col >= BR_COLS || row >= BR_ROWS) return false;
  if (px % BR_PITCH_X >= BR_W || py % BR_PITCH_Y >= BR_H) return false;
  return bricks[row] & (1u << col);
}

// One sub-pixel move of the ball; returns false when it fell out.
static bool moveBall(float dx, float dy) {
  float nx = ballX + dx, ny = ballY + dy;

  if (nx < 0) { nx = -nx; ballVX = fabsf(ballVX); }
  if (nx > SCREEN_WIDTH - BALL) { nx = 2 * (SCREEN_WIDTH - BALL) - nx; ballVX = -fabsf(ballVX); }
  if (ny < GAME_TOP) { ny = 2 * GAME_TOP - ny; ballVY = fabsf(ballVY); }
  if (ny > SCREEN_HEIGHT) return false;

  // Bricks: test the ball's four corners at the new spot.
  for (int c = 0; c < 4; c++) {
    float cx = nx + (c & 1) * (BALL - 1), cy = ny + (c >> 1) * (BALL - 1);
    int col, row;
    if (!brickAt(cx, cy, col, row)) continue;
    bricks[row] &= ~(1u << col);
    score += (BR_ROWS - row) * 5 + 5;
    gameRumble(0, 18, 30);
    // Came in from the side (old position already level with the brick): flip X.
    float top = BR_Y0 + row * BR_PITCH_Y;
    if (ballY + BALL > top && ballY < top + BR_H) {
      ballVX = -ballVX;
      nx = ballX;
    } else {
      ballVY = -ballVY;
      ny = ballY;
    }
    break;
  }

  // Paddle: only while falling, angle from where it lands on the paddle.
  if (ballVY > 0 && ny + BALL >= PAD_Y && ballY + BALL <= PAD_Y + 1 &&
      nx + BALL > padX - PAD_W / 2 && nx < padX + PAD_W / 2) {
    float off = constrain((nx + BALL / 2 - padX) / (PAD_W / 2), -1.0f, 1.0f);
    float a = off * 1.05f;  // up to ~60 degrees off vertical
    ballVX = speed * sinf(a);
    ballVY = -speed * cosf(a);
    ny = PAD_Y - BALL;
  }

  ballX = nx;
  ballY = ny;
  return true;
}

static void update(const GamepadState &in, float dt) {
  float v = 0;
  if (abs(in.lx) > 4000) v = in.lx / 32768.0f;
  if (in.buttons & GP_LEFT) v = min(v, -0.85f);
  if (in.buttons & GP_RIGHT) v = max(v, 0.85f);
  padX = constrain(padX + v * PAD_SPEED * dt, PAD_W / 2.0f, SCREEN_WIDTH - PAD_W / 2.0f);

  if (attached) {
    ballX = padX - BALL / 2;
    ballY = PAD_Y - BALL;
    if (in.pressed & (GP_A | GP_UP)) launch();
    return;
  }

  float dx = ballVX * dt, dy = ballVY * dt;
  int steps = (int)ceilf(max(fabsf(dx), fabsf(dy))) + 1;
  for (int i = 0; i < steps; i++) {
    if (!moveBall(ballVX * dt / steps, ballVY * dt / steps)) {
      loseLife();
      return;
    }
  }

  bool cleared = true;
  for (int r = 0; r < BR_ROWS; r++) cleared &= bricks[r] == 0;
  if (cleared) {
    level++;
    speed = min(BALL_SPEED + (level - 1) * BALL_SPEED_LEVEL, BALL_SPEED_MAX);
    fillWall();
    attachBall();
    gameRumble(60, 60, 250);
  }
}

static void draw() {
  for (int r = 0; r < BR_ROWS; r++)
    for (int c = 0; c < BR_COLS; c++)
      if (bricks[r] & (1u << c))
        display.fillRect(BR_X0 + c * BR_PITCH_X, BR_Y0 + r * BR_PITCH_Y, BR_W, BR_H, DISPLAY_WHITE);
  display.fillRect((int)(padX - PAD_W / 2), PAD_Y, PAD_W, 2, DISPLAY_WHITE);
  display.fillRect((int)ballX, (int)ballY, BALL, BALL, DISPLAY_WHITE);

  gameDrawScore(0, score);
  for (int i = 0; i < lives; i++) display.fillRect(52 + i * 5, 4, 3, 3, DISPLAY_WHITE);
  display.setCursor(72, 1);
  display.print("L");
  display.print(level);
  gameDrawClock();
}

bool bricksFrame(const GamepadState &in, bool padLost) {
  unsigned long now = millis();
  switch (gameFlow(phase, in, padLost)) {
    case STEP_QUIT:
      return false;
    case STEP_RESTART:
      bricksReset();
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
  gameDrawOverlay(phase, padLost, "BRICKS", phase == G_PAUSED ? nullptr : info, newHi,
                  "stick/d-pad: paddle\nA: launch ball");
  return true;
}

#endif // GAMEPAD_ENABLED
