/*
 * SmallOLED-PCMonitor - Pong/Breakout Clock
 *
 * Breakout-style clock with ball physics, paddle, and digit transitions.
 * Ball bounces off walls and digits, breaking them on time changes.
 */

#include "../config/config.h"
#include "../display/display.h"
#include "clocks.h"

// Fragment pool helper functions
SpaceFragment* findFreePongFragment() {
  for (int i = 0; i < MAX_PONG_FRAGMENTS; i++) {
    if (!pong_fragments[i].active) return &pong_fragments[i];
  }
  return nullptr;
}

bool allPongFragmentsInactive() {
  for (int i = 0; i < MAX_PONG_FRAGMENTS; i++) {
    if (pong_fragments[i].active) return false;
  }
  return true;
}

// Initialize Pong animation
void initPongAnimation() {
  // Reset ball 0 above paddle, traveling upward
  pong_balls[0].x = 64 * 16;  // Center X
  pong_balls[0].y = (BREAKOUT_PADDLE_Y - 4) * 16;  // Just above paddle

  // Always start upward (negative Y) with random X direction
  if (random(0, 2) == 0) {
    pong_balls[0].vx = settings.pongBallSpeed;  // Right
  } else {
    pong_balls[0].vx = -settings.pongBallSpeed;  // Left
  }
  pong_balls[0].vy = -settings.pongBallSpeed;  // Up

  pong_balls[0].state = PONG_BALL_SPAWNING;
  pong_balls[0].spawn_timer = millis();
  pong_balls[0].active = true;
  pong_balls[0].inside_digit = -1;  // Not inside any digit

  // Deactivate ball 1
  pong_balls[1].active = false;
  pong_balls[1].inside_digit = -1;

  // Reset paddle to center bottom
  breakout_paddle.x = 64;
  breakout_paddle.target_x = 64;
  breakout_paddle.width = settings.pongPaddleWidth;  // Use user-configured width

  // Clear digit transitions and reset bounce offsets
  for (int i = 0; i < 5; i++) {
    digit_transitions[i].state = DIGIT_NORMAL;
    digit_transitions[i].hit_count = 0;
    digit_transitions[i].fragments_spawned = 0;
    digit_transitions[i].assembly_progress = 0.0;
    digit_offset_x[i] = 0;
    digit_offset_y[i] = 0;
    digit_velocity_x[i] = 0;
    digit_velocity[i] = 0;
  }

  // Clear fragments and targets
  for (int i = 0; i < MAX_PONG_FRAGMENTS; i++) {
    pong_fragments[i].active = false;
    fragment_targets[i].target_digit = -1;
  }

  // Initialize displayed time from current time
  struct tm timeinfo;
  if (getTimeWithTimeout(&timeinfo)) {
    displayed_hour = timeinfo.tm_hour;
    displayed_min = timeinfo.tm_min;
  } else {
    // Fallback if time not available
    displayed_hour = 0;
    displayed_min = 0;
  }

  last_pong_update = millis();
}

// Reset Pong animation
void resetPongAnimation() {
  initPongAnimation();
}

// Spawn ball with upward direction (Breakout style)
void spawnPongBall(int ballIndex) {
  pong_balls[ballIndex].x = breakout_paddle.x * 16;  // Spawn on paddle
  pong_balls[ballIndex].y = (BREAKOUT_PADDLE_Y - 4) * 16;  // Just above paddle

  // Random X direction, always upward
  if (random(0, 2) == 0) {
    pong_balls[ballIndex].vx = settings.pongBallSpeed;
  } else {
    pong_balls[ballIndex].vx = -settings.pongBallSpeed;
  }
  pong_balls[ballIndex].vy = -settings.pongBallSpeed;  // Always up

  pong_balls[ballIndex].state = PONG_BALL_NORMAL;
  pong_balls[ballIndex].active = true;
  pong_balls[ballIndex].inside_digit = -1;
}

// Update breakout paddle (auto-tracking with smooth movement)
void updateBreakoutPaddle() {
  // Find target X position (track closest active ball)
  int closest_ball = -1;
  int closest_dist = 999;

  for (int i = 0; i < MAX_PONG_BALLS; i++) {
    if (pong_balls[i].active) {
      int ball_x = pong_balls[i].x / 16;
      int dist = abs(ball_x - breakout_paddle.x);
      if (dist < closest_dist) {
        closest_dist = dist;
        closest_ball = i;
      }
    }
  }

  // Set target to ball X position (perfect tracking)
  if (closest_ball >= 0) {
    int ball_x = pong_balls[closest_ball].x / 16;
    breakout_paddle.target_x = ball_x;
  }

  // Move toward target with smooth movement
  int dx = breakout_paddle.target_x - breakout_paddle.x;

  // Smooth acceleration based on distance
  int move_speed = breakout_paddle.speed;
  if (abs(dx) > 20) {
    move_speed = 5;  // Faster when far away
  } else if (abs(dx) > 10) {
    move_speed = 4;  // Medium speed
  } else if (abs(dx) > 3) {
    move_speed = 3;  // Normal speed
  } else {
    move_speed = 2;  // Slow when close (prevents overshoot)
  }

  if (abs(dx) > 1) {
    // 10% chance to move in wrong direction (adds unpredictability)
    bool move_wrong_way = (random(0, 100) < PADDLE_WRONG_DIRECTION_CHANCE);

    if (move_wrong_way) {
      // Intentionally move opposite direction
      if (dx > 0) {
        breakout_paddle.x -= move_speed;
      } else {
        breakout_paddle.x += move_speed;
      }
    } else {
      // Normal tracking
      if (dx > 0) {
        breakout_paddle.x += move_speed;
      } else {
        breakout_paddle.x -= move_speed;
      }
    }
  } else {
    breakout_paddle.x = breakout_paddle.target_x;  // Snap if very close
  }

  // Clamp paddle to screen bounds
  int paddle_half = breakout_paddle.width / 2;
  if (breakout_paddle.x - paddle_half < 0) {
    breakout_paddle.x = paddle_half;
  }
  if (breakout_paddle.x + paddle_half > 127) {
    breakout_paddle.x = 127 - paddle_half;
  }

  // Track paddle position for momentum-based ball release (updated after movement)
  paddle_last_x = breakout_paddle.x;
}

// Draw breakout paddle at bottom
void drawBreakoutPaddle() {
  int paddle_left = breakout_paddle.x - (breakout_paddle.width / 2);
  int paddle_right = paddle_left + breakout_paddle.width;

  display.fillRect(paddle_left, BREAKOUT_PADDLE_Y, breakout_paddle.width, BREAKOUT_PADDLE_HEIGHT, DISPLAY_WHITE);
}

// Update fragment physics (gravity, boundaries)
void updatePongFragments() {
  for (int i = 0; i < MAX_PONG_FRAGMENTS; i++) {
    if (!pong_fragments[i].active) continue;

    pong_fragments[i].vy += PONG_FRAG_GRAVITY;  // Apply gravity
    pong_fragments[i].x += pong_fragments[i].vx;
    pong_fragments[i].y += pong_fragments[i].vy;

    // Deactivate if off-screen
    if (pong_fragments[i].y > SCREEN_HEIGHT + 5 ||
        pong_fragments[i].x < -5 ||
        pong_fragments[i].x > SCREEN_WIDTH + 5) {
      pong_fragments[i].active = false;
    }
  }
}

// Draw fragments as 2x2 pixels
void drawPongFragments() {
  for (int i = 0; i < MAX_PONG_FRAGMENTS; i++) {
    if (pong_fragments[i].active) {
      int fx = (int)pong_fragments[i].x;
      int fy = (int)pong_fragments[i].y;
      display.fillRect(fx, fy, 2, 2, DISPLAY_WHITE);
    }
  }
}

// Spawn 2-4 fragments on ball collision
void spawnPongBallHitFragments(int x, int y) {
  int frag_count = 2 + random(0, 3);  // 2-4 fragments

  for (int i = 0; i < frag_count; i++) {
    SpaceFragment* f = findFreePongFragment();
    if (!f) break;

    f->x = x + random(-2, 3);
    f->y = y + random(-2, 3);

    float angle = random(0, 360) * PI / 180.0;
    float speed = PONG_FRAG_SPEED + random(-20, 20) / 100.0;

    f->vx = cos(angle) * speed;
    f->vy = sin(angle) * speed;
    f->active = true;
  }
}

// Spawn pixel-accurate fragments from breaking digit (TRUE PIXEL SAMPLING)
void spawnDigitBreakFragments(int digitIndex, char oldChar) {
  uint8_t* buffer = display.getBuffer();
  if (!buffer) return;  // Safety check

  int digit_x = DIGIT_X[digitIndex];
  int digit_y = PONG_TIME_Y;

  // Sample every 2nd pixel to limit fragment count
  // Use random sampling: only spawn fragment for ~37.5% of lit pixels
  for (int dy = 0; dy < 24; dy += 2) {
    for (int dx = 0; dx < 15; dx += 2) {
      int px = digit_x + dx;
      int py = digit_y + dy;

      // Read pixel from buffer (1 bit per pixel, organized in 8-pixel pages)
      int page = py / 8;
      int bit = py % 8;
      int index = px + (page * 128);
      bool pixel_lit = (buffer[index] >> bit) & 0x01;

      if (pixel_lit && random(0, 8) < 3) {  // 37.5% spawn rate
        SpaceFragment* f = findFreePongFragment();
        if (!f) break;

        f->x = px;
        f->y = py;

        // Velocity: outward from digit center + randomization
        float dx_center = px - (digit_x + 7);
        float dy_center = py - (digit_y + 12);
        float angle = atan2(dy_center, dx_center) + random(-30, 30) / 100.0;
        float speed = PONG_FRAG_SPEED + random(-50, 50) / 100.0;

        f->vx = cos(angle) * speed;
        f->vy = sin(angle) * speed - 0.5;  // Slight upward bias
        f->active = true;
      }
    }
  }
}

// Spawn progressive fragments (25%, 50%, or 25% based on hit number)
void spawnProgressiveFragments(int digitIndex, char oldChar, int hitNumber) {
  uint8_t* buffer = display.getBuffer();
  if (!buffer) return;

  int digit_x = DIGIT_X[digitIndex];
  int digit_y = PONG_TIME_Y;

  // Calculate how many fragments to spawn based on hit number (0, 1, or 2)
  float spawn_percent = FRAGMENT_SPAWN_PERCENT[hitNumber];
  int spawn_chance = (int)(spawn_percent * 8);  // 0-8 scale for random check

  // Sample pixels and spawn fragments
  for (int dy = 0; dy < 24; dy += 2) {
    for (int dx = 0; dx < 15; dx += 2) {
      int px = digit_x + dx;
      int py = digit_y + dy;

      int page = py / 8;
      int bit = py % 8;
      int index = px + (page * 128);
      bool pixel_lit = (buffer[index] >> bit) & 0x01;

      if (pixel_lit && random(0, 8) < spawn_chance) {
        SpaceFragment* f = findFreePongFragment();
        if (!f) break;

        f->x = px;
        f->y = py;

        // Velocity: outward from digit center
        float dx_center = px - (digit_x + 7);
        float dy_center = py - (digit_y + 12);
        float angle = atan2(dy_center, dx_center) + random(-30, 30) / 100.0;
        float speed = PONG_FRAG_SPEED + random(-50, 50) / 100.0;

        f->vx = cos(angle) * speed;
        f->vy = sin(angle) * speed - 0.5;
        f->active = true;
      }
    }
  }
}

// Spawn assembly fragments (fragments that will converge to form new digit)
void spawnAssemblyFragments(int digitIndex, char newChar) {
  // Temporarily render new digit to sample its pixels
  uint8_t* buffer = display.getBuffer();
  if (!buffer) return;

  int digit_x = DIGIT_X[digitIndex];
  int digit_y = PONG_TIME_Y;

  // Sample every 2nd pixel from new digit
  for (int dy = 0; dy < 24; dy += 2) {
    for (int dx = 0; dx < 15; dx += 2) {
      int px = digit_x + dx;
      int py = digit_y + dy;

      int page = py / 8;
      int bit = py % 8;
      int index = px + (page * SCREEN_WIDTH);
      bool pixel_lit = (buffer[index] >> bit) & 0x01;

      if (pixel_lit && random(0, 8) < 4) {  // 50% spawn rate
        SpaceFragment* f = findFreePongFragment();
        if (!f) break;

        // Start fragment from random position off-screen edges
        int start_side = random(0, 4);
        switch (start_side) {
          case 0: f->x = random(0, 128); f->y = -5; break;  // Top
          case 1: f->x = 133; f->y = random(0, 64); break;  // Right
          case 2: f->x = random(0, 128); f->y = 69; break;  // Bottom
          case 3: f->x = -5; f->y = random(0, 64); break;   // Left
        }

        // Store target position for this fragment
        for (int i = 0; i < MAX_PONG_FRAGMENTS; i++) {
          if (&pong_fragments[i] == f) {
            fragment_targets[i].target_digit = digitIndex;
            fragment_targets[i].target_x = px;
            fragment_targets[i].target_y = py;
            break;
          }
        }

        // Initial velocity toward target (will be updated in updateAssemblyFragments)
        float dx_target = px - f->x;
        float dy_target = py - f->y;
        float dist = sqrt(dx_target * dx_target + dy_target * dy_target);
        if (dist > 0) {
          f->vx = (dx_target / dist) * PONG_FRAG_SPEED * 2;
          f->vy = (dy_target / dist) * PONG_FRAG_SPEED * 2;
        }

        f->active = true;
      }
    }
  }
}

// Update assembly fragments (move toward target positions)
void updateAssemblyFragments() {
  for (int i = 0; i < MAX_PONG_FRAGMENTS; i++) {
    if (!pong_fragments[i].active) continue;
    if (fragment_targets[i].target_digit < 0) continue;  // Not an assembly fragment

    // Calculate direction to target
    float dx = fragment_targets[i].target_x - pong_fragments[i].x;
    float dy = fragment_targets[i].target_y - pong_fragments[i].y;
    float dist = sqrt(dx * dx + dy * dy);

    if (dist < 2.0) {
      // Reached target - snap to position and stop
      pong_fragments[i].x = fragment_targets[i].target_x;
      pong_fragments[i].y = fragment_targets[i].target_y;
      pong_fragments[i].vx = 0;
      pong_fragments[i].vy = 0;
      // Fragment stays active at target position
    } else {
      // Move toward target with acceleration
      float speed = PONG_FRAG_SPEED * 3;
      pong_fragments[i].vx = (dx / dist) * speed;
      pong_fragments[i].vy = (dy / dist) * speed;

      pong_fragments[i].x += pong_fragments[i].vx;
      pong_fragments[i].y += pong_fragments[i].vy;
    }
  }
}

// Check if ball is inside a digit hole (stub for now - complex feature)
bool checkDigitHoleCollision(int ballIndex, int digitIndex) {
  // TODO: Implement digit hole detection using pixel sampling
  // For now, return false (no hole collision)
  return false;
}

// Update ball position and physics
void updatePongBall(int ballIndex) {
  if (pong_balls[ballIndex].state == PONG_BALL_SPAWNING) {
    if (millis() - pong_balls[ballIndex].spawn_timer >= BALL_SPAWN_DELAY) {
      pong_balls[ballIndex].state = PONG_BALL_NORMAL;
    }
    return;  // Don't move while spawning
  }

  // Handle ball stuck to paddle (appears to bounce normally but locked to paddle X)
  if (ball_stuck_to_paddle[ballIndex]) {
    // Lock ball to paddle position (moves with paddle)
    int ball_px = breakout_paddle.x + ball_stuck_x_offset[ballIndex];
    int ball_py = BREAKOUT_PADDLE_Y - PONG_BALL_SIZE;
    pong_balls[ballIndex].x = ball_px * 16;
    pong_balls[ballIndex].y = ball_py * 16;

    // Check if it's time to release
    if (millis() >= ball_stick_release_time[ballIndex]) {
      // Release ball with momentum-based trajectory
      ball_stuck_to_paddle[ballIndex] = false;

      // Calculate paddle velocity (movement since last frame)
      int paddle_velocity = breakout_paddle.x - paddle_last_x;

      // Set horizontal velocity based on paddle movement direction
      if (paddle_velocity > 0) {
        // Paddle moving right - launch ball right
        pong_balls[ballIndex].vx = settings.pongBallSpeed + (paddle_velocity * PADDLE_MOMENTUM_MULTIPLIER);
      } else if (paddle_velocity < 0) {
        // Paddle moving left - launch ball left
        pong_balls[ballIndex].vx = -settings.pongBallSpeed + (paddle_velocity * PADDLE_MOMENTUM_MULTIPLIER);
      } else {
        // Paddle stationary - random direction
        pong_balls[ballIndex].vx = random(0, 2) == 0 ? settings.pongBallSpeed : -settings.pongBallSpeed;
      }

      // Always launch upward
      pong_balls[ballIndex].vy = -settings.pongBallSpeed;

      // Add small random variation for natural movement
      pong_balls[ballIndex].vx += random(-BALL_RELEASE_RANDOM_VARIATION, BALL_RELEASE_RANDOM_VARIATION + 1);
      pong_balls[ballIndex].vy += random(-BALL_RELEASE_RANDOM_VARIATION, BALL_RELEASE_RANDOM_VARIATION + 1);
    }

    return;  // Don't do normal movement while stuck
  }

  // Update position (fixed-point math - straight-line bounces only)
  pong_balls[ballIndex].x += pong_balls[ballIndex].vx;
  pong_balls[ballIndex].y += pong_balls[ballIndex].vy;

  // Convert to pixel coordinates for collision checks
  int ball_px = pong_balls[ballIndex].x / 16;
  int ball_py = pong_balls[ballIndex].y / 16;

  // Top wall collision (bounce down)
  if (ball_py <= PONG_PLAY_AREA_TOP) {
    ball_py = PONG_PLAY_AREA_TOP;
    pong_balls[ballIndex].y = ball_py * 16;
    pong_balls[ballIndex].vy = abs(pong_balls[ballIndex].vy);  // Force downward
  }

  // Bottom paddle collision (bounce up)
  if (ball_py + PONG_BALL_SIZE >= BREAKOUT_PADDLE_Y) {
    // Check if ball is within paddle width
    int paddle_left = breakout_paddle.x - (breakout_paddle.width / 2);
    int paddle_right = breakout_paddle.x + (breakout_paddle.width / 2);

    if (ball_px + PONG_BALL_SIZE >= paddle_left && ball_px <= paddle_right) {
      // Hit paddle - STICK TO PADDLE with random delay (prevents vertical loop)
      ball_py = BREAKOUT_PADDLE_Y - PONG_BALL_SIZE;
      pong_balls[ballIndex].y = ball_py * 16;

      // Activate stick mechanic
      ball_stuck_to_paddle[ballIndex] = true;

      // Random delay before release (0-300ms, customizable above)
      int stick_delay = random(PADDLE_STICK_MIN_DELAY, PADDLE_STICK_MAX_DELAY + 1);
      ball_stick_release_time[ballIndex] = millis() + stick_delay;

      // Store ball's X offset from paddle center (so it moves with paddle)
      ball_stuck_x_offset[ballIndex] = ball_px - breakout_paddle.x;
    } else {
      // Missed paddle
      if (ballIndex == 0) {
        // Ball 0: respawn on paddle
        spawnPongBall(ballIndex);
      } else {
        // Ball 1: deactivate (don't respawn, wait for next :55)
        pong_balls[ballIndex].active = false;
      }
      return;
    }
  }

  // Left/right wall collisions (bounce horizontally)
  if (ball_px < 0) {
    ball_px = 0;
    pong_balls[ballIndex].x = ball_px * 16;
    pong_balls[ballIndex].vx = abs(pong_balls[ballIndex].vx);  // Force right
  }
  if (ball_px + PONG_BALL_SIZE > SCREEN_WIDTH) {
    ball_px = SCREEN_WIDTH - PONG_BALL_SIZE;
    pong_balls[ballIndex].x = ball_px * 16;
    pong_balls[ballIndex].vx = -abs(pong_balls[ballIndex].vx);  // Force left
  }

  // Clamp velocities to reasonable limits (prevent too shallow or steep angles)
  if (abs(pong_balls[ballIndex].vx) < 8) {
    pong_balls[ballIndex].vx = (pong_balls[ballIndex].vx > 0) ? 8 : -8;
  }
  if (abs(pong_balls[ballIndex].vx) > 40) {
    pong_balls[ballIndex].vx = (pong_balls[ballIndex].vx > 0) ? 40 : -40;
  }
  if (abs(pong_balls[ballIndex].vy) < 8) {
    pong_balls[ballIndex].vy = (pong_balls[ballIndex].vy > 0) ? 8 : -8;
  }
  if (abs(pong_balls[ballIndex].vy) > 40) {
    pong_balls[ballIndex].vy = (pong_balls[ballIndex].vy > 0) ? 40 : -40;
  }
}

// Helper: Check if pixel is lit in display buffer (for pixel-perfect collision)
bool isPixelLit(int x, int y) {
  if (x < 0 || x >= SCREEN_WIDTH || y < 0 || y >= SCREEN_HEIGHT) {
    return false;
  }

  uint8_t* buffer = display.getBuffer();
  if (!buffer) return false;

  int page = y / 8;
  int bit = y % 8;
  int index = x + (page * SCREEN_WIDTH);

  return (buffer[index] >> bit) & 0x01;
}

// Check ball-digit collisions (pixel-perfect with progressive fragmentation)
void checkPongCollisions(int ballIndex) {
  int ball_px = pong_balls[ballIndex].x / 16;
  int ball_py = pong_balls[ballIndex].y / 16;

  // Check collision with each digit (textSize 3: ~15px wide x 24px tall)
  for (int d = 0; d < 5; d++) {
    // Skip colon (index 2) - it shouldn't bounce or be hit
    if (d == 2) continue;

    // Skip digits that are assembling (fragments are converging)
    if (digit_transitions[d].state == DIGIT_ASSEMBLING) continue;

    // Tighter AABB collision box (reduces false positives while ensuring hits work)
    int dx1 = DIGIT_X[d] + 1;      // 1px padding on left
    int dx2 = DIGIT_X[d] + 14;     // 1px padding on right
    int dy1 = PONG_TIME_Y + 1;     // 1px padding on top
    int dy2 = PONG_TIME_Y + 23;    // 1px padding on bottom

    // AABB collision check
    if (ball_px + PONG_BALL_SIZE >= dx1 && ball_px <= dx2 &&
        ball_py + PONG_BALL_SIZE >= dy1 && ball_py <= dy2) {

      // Calculate ball hit side for push direction
      int ball_cx = ball_px + PONG_BALL_SIZE / 2;
      int ball_cy = ball_py + PONG_BALL_SIZE / 2;
      int digit_cx = (dx1 + dx2) / 2;
      int digit_cy = (dy1 + dy2) / 2;

      // Apply visible push to digit - always push AWAY from ball
      float push_strength = 3.0;  // Strong enough to see (2-3 pixel visible movement)

      // Determine hit direction and apply appropriate push
      if (abs(ball_cx - digit_cx) > 4 && settings.pongHorizontalBounce) {
        // Side hit - push digit horizontally (only if horizontal bounce enabled)
        if (ball_cx < digit_cx) {
          // Ball is left of digit center - push digit RIGHT
          digit_velocity_x[d] = push_strength;
        } else {
          // Ball is right of digit center - push digit LEFT
          digit_velocity_x[d] = -push_strength;
        }
      } else {
        // Top/bottom hit OR horizontal bounce disabled - push digit vertically
        if (ball_cy < digit_cy) {
          // Ball is above digit center - push digit DOWN
          digit_velocity[d] = push_strength;
        } else {
          // Ball is below digit center - push digit UP
          digit_velocity[d] = -push_strength;
        }
      }

      // Check if this is a breaking digit (target for hits)
      if (digit_transitions[d].state == DIGIT_BREAKING) {
        // Increment hit count and spawn progressive fragments
        int hit_num = digit_transitions[d].hit_count;

        if (hit_num < BALL_HIT_THRESHOLD) {
          digit_transitions[d].hit_count++;

          // Spawn fragments based on hit number (0=25%, 1=50%, 2=25%)
          spawnProgressiveFragments(d, digit_transitions[d].old_char, hit_num);
        }
      }

      // Reflect ball and push it out of collision to prevent sticking
      if (abs(ball_cx - digit_cx) > 4) {
        // Side hit
        pong_balls[ballIndex].vx = -pong_balls[ballIndex].vx;
        // Add random angle variation for natural movement
        pong_balls[ballIndex].vy += random(-BALL_COLLISION_ANGLE_VARIATION, BALL_COLLISION_ANGLE_VARIATION + 1);

        // Push ball out horizontally
        if (ball_cx < digit_cx) {
          ball_px = dx1 - PONG_BALL_SIZE - 1;  // Push left
        } else {
          ball_px = dx2 + 1;  // Push right
        }
        pong_balls[ballIndex].x = ball_px * 16;
      } else {
        // Top/bottom hit
        pong_balls[ballIndex].vy = -pong_balls[ballIndex].vy;
        // Add random angle variation for natural movement
        pong_balls[ballIndex].vx += random(-BALL_COLLISION_ANGLE_VARIATION, BALL_COLLISION_ANGLE_VARIATION + 1);

        // Push ball out vertically
        if (ball_cy < digit_cy) {
          ball_py = dy1 - PONG_BALL_SIZE - 1;  // Push up
        } else {
          ball_py = dy2 + 1;  // Push down
        }
        pong_balls[ballIndex].y = ball_py * 16;
      }

      break;  // Only process one collision per frame
    }
  }

  // Check for ball in gap between adjacent digit pairs (hours: 0-1, minutes: 3-4)
  // This handles the case where ball flows straight through without hitting either digit
  // Only active when horizontal bounce is enabled - otherwise ball passes through freely
  if (settings.pongHorizontalBounce) {
    int dy1 = PONG_TIME_Y + 1;
    int dy2 = PONG_TIME_Y + 23;

    // Only check if ball is at digit Y level
    if (ball_py + PONG_BALL_SIZE >= dy1 && ball_py <= dy2) {
      // Check gap between hour digits (0 and 1)
      int gap1_left = DIGIT_X[0] + 14;   // Right edge of digit 0
      int gap1_right = DIGIT_X[1] + 1;   // Left edge of digit 1
      int ball_cx = ball_px + PONG_BALL_SIZE / 2;

      if (ball_cx > gap1_left && ball_cx < gap1_right) {
        // Ball is in gap between hour digits - bounce and push both apart
        float push_strength = 3.0;
        digit_velocity_x[0] = -push_strength;  // Push left digit left
        digit_velocity_x[1] = push_strength;   // Push right digit right

        // Reverse ball horizontal direction
        pong_balls[ballIndex].vx = -pong_balls[ballIndex].vx;

        // Push ball out of gap
        if (pong_balls[ballIndex].vx > 0) {
          ball_px = gap1_right + 1;
        } else {
          ball_px = gap1_left - PONG_BALL_SIZE - 1;
        }
        pong_balls[ballIndex].x = ball_px * 16;
      }

      // Check gap between minute digits (3 and 4)
      int gap2_left = DIGIT_X[3] + 14;   // Right edge of digit 3
      int gap2_right = DIGIT_X[4] + 1;   // Left edge of digit 4

      if (ball_cx > gap2_left && ball_cx < gap2_right) {
        // Ball is in gap between minute digits - bounce and push both apart
        float push_strength = 3.0;
        digit_velocity_x[3] = -push_strength;  // Push left digit left
        digit_velocity_x[4] = push_strength;   // Push right digit right

        // Reverse ball horizontal direction
        pong_balls[ballIndex].vx = -pong_balls[ballIndex].vx;

        // Push ball out of gap
        if (pong_balls[ballIndex].vx > 0) {
          ball_px = gap2_right + 1;
        } else {
          ball_px = gap2_left - PONG_BALL_SIZE - 1;
        }
        pong_balls[ballIndex].x = ball_px * 16;
      }
    }
  }
}

// Trigger digit transition (start breaking process - wait for ball hits)
void triggerDigitTransition(int digitIndex, char oldChar, char newChar) {
  digit_transitions[digitIndex].state = DIGIT_BREAKING;
  digit_transitions[digitIndex].old_char = oldChar;
  digit_transitions[digitIndex].new_char = newChar;
  digit_transitions[digitIndex].state_timer = millis();
  digit_transitions[digitIndex].hit_count = 0;
  digit_transitions[digitIndex].fragments_spawned = 0;
  digit_transitions[digitIndex].assembly_progress = 0.0;

  // Don't spawn fragments yet - wait for ball hits
}

// Update digit transition state machine
void updateDigitTransitions() {
  for (int i = 0; i < 5; i++) {
    if (digit_transitions[i].state == DIGIT_NORMAL) continue;

    unsigned long elapsed = millis() - digit_transitions[i].state_timer;

    if (digit_transitions[i].state == DIGIT_BREAKING) {
      // Check if fully broken (threshold hits reached) OR timeout exceeded
      if (digit_transitions[i].hit_count >= BALL_HIT_THRESHOLD || elapsed >= DIGIT_TRANSITION_TIMEOUT) {
        // Transition to ASSEMBLING state
        digit_transitions[i].state = DIGIT_ASSEMBLING;
        digit_transitions[i].state_timer = millis();
        digit_transitions[i].assembly_progress = 0.0;

        // Spawn assembly fragments for new digit
        spawnAssemblyFragments(i, digit_transitions[i].new_char);
      }
    } else if (digit_transitions[i].state == DIGIT_ASSEMBLING) {
      // Animate assembly (fragments converging)
      float progress = (float)elapsed / DIGIT_ASSEMBLY_DURATION;
      if (progress >= 1.0) {
        digit_transitions[i].state = DIGIT_NORMAL;
        digit_transitions[i].assembly_progress = 1.0;
      } else {
        digit_transitions[i].assembly_progress = progress;
      }
    }
  }
}

// Draw Pong ball(s)
void drawPongBall() {
  for (int i = 0; i < MAX_PONG_BALLS; i++) {
    if (!pong_balls[i].active) continue;

    int ball_px = pong_balls[i].x / 16;
    int ball_py = pong_balls[i].y / 16;

    if (pong_balls[i].state == PONG_BALL_SPAWNING) {
      // Flash ball during spawn (blink effect)
      if ((millis() / 100) % 2 == 0) {
        display.fillRect(ball_px, ball_py, PONG_BALL_SIZE, PONG_BALL_SIZE, DISPLAY_WHITE);
      }
    } else {
      display.fillRect(ball_px, ball_py, PONG_BALL_SIZE, PONG_BALL_SIZE, DISPLAY_WHITE);
    }
  }
}

// Draw Pong clock digits with custom pop-in animation
void drawPongDigits() {
  display.setTextSize(3);
  display.setTextColor(DISPLAY_WHITE);

  // Build digit string
  char digits[6];
  digits[0] = '0' + (displayed_hour / 10);
  digits[1] = '0' + (displayed_hour % 10);
  digits[2] = shouldShowColon() ? ':' : ' ';
  digits[3] = '0' + (displayed_min / 10);
  digits[4] = '0' + (displayed_min % 10);
  digits[5] = '\0';

  for (int i = 0; i < 5; i++) {
    if (digit_transitions[i].state == DIGIT_BREAKING) {
      // Show old digit fading out as it gets hit
      // Render with reduced brightness based on hit count
      float fade = 1.0 - ((float)digit_transitions[i].hit_count / BALL_HIT_THRESHOLD);

      // Flicker based on hits: more hits = more flicker
      if (digit_transitions[i].hit_count > 0) {
        int flicker_speed = 100 - (digit_transitions[i].hit_count * 20);
        if ((millis() / flicker_speed) % 2 == 0) continue;  // Skip rendering = flicker
      }

      // Draw old digit with bounce offset
      int x = DIGIT_X[i] + (int)digit_offset_x[i];
      int y = PONG_TIME_Y + (int)digit_offset_y[i];
      display.setCursor(x, y);
      char old_digit = digit_transitions[i].old_char;
      display.print(old_digit);

    } else if (digit_transitions[i].state == DIGIT_ASSEMBLING) {
      // Show new digit assembling from fragments
      // Don't draw the digit itself - fragments will converge to form it
      // Once assembly is complete (progress >= 0.8), start showing the solid digit
      if (digit_transitions[i].assembly_progress >= 0.8) {
        int x = DIGIT_X[i] + (int)digit_offset_x[i];
        int y = PONG_TIME_Y + (int)digit_offset_y[i];
        display.setCursor(x, y);
        char new_digit = digit_transitions[i].new_char;
        display.print(new_digit);
      }

    } else {
      // Normal rendering with bounce offset from ball hits
      int x = DIGIT_X[i] + (int)digit_offset_x[i];
      int y = PONG_TIME_Y + (int)digit_offset_y[i];
      display.setCursor(x, y);
      display.print(digits[i]);
    }
  }
}

// Spring-based bounce for Pong clock (delta-time independent oscillating physics)
void updateDigitBouncePong() {
  static unsigned long lastPhysicsUpdate = 0;
  unsigned long currentTime = millis();

  // Calculate delta time in seconds
  float deltaTime = (currentTime - lastPhysicsUpdate) / 1000.0;

  // Cap delta time to prevent huge jumps
  if (deltaTime > 0.1 || lastPhysicsUpdate == 0) {
    deltaTime = 0.025;  // Default to 25ms (40 Hz) for first frame
  }

  lastPhysicsUpdate = currentTime;

  // Scale physics to match original 50ms timing
  float physicsScale = deltaTime / 0.05;

  // Use user-configured bounce physics (stored as tenths/hundredths, convert to floats)
  const float SPRING_STRENGTH = settings.pongBounceStrength / 10.0;  // Pull back to center
  const float DAMPING = settings.pongBounceDamping / 100.0;          // Damping factor

  for (int i = 0; i < 5; i++) {
    // Y-axis spring bounce (vertical)
    if (digit_offset_y[i] != 0 || digit_velocity[i] != 0) {
      // Spring force: pulls digit back to rest position (0)
      float spring_force_y = -digit_offset_y[i] * SPRING_STRENGTH;

      // Apply spring force and damping (scaled by physics rate)
      digit_velocity[i] += spring_force_y * physicsScale;
      digit_velocity[i] *= pow(DAMPING, physicsScale);  // Exponential damping scaling

      // Update position
      digit_offset_y[i] += digit_velocity[i] * physicsScale;

      // Clamp to visible movement range (allow up to 4 pixels)
      if (digit_offset_y[i] > 4) digit_offset_y[i] = 4;
      if (digit_offset_y[i] < -4) digit_offset_y[i] = -4;

      // Stop when very close to rest position
      if (abs(digit_offset_y[i]) < 0.1 && abs(digit_velocity[i]) < 0.1) {
        digit_offset_y[i] = 0;
        digit_velocity[i] = 0;
      }
    }

    // X-axis spring bounce (horizontal)
    if (digit_offset_x[i] != 0 || digit_velocity_x[i] != 0) {
      // Spring force: pulls digit back to rest position (0)
      float spring_force_x = -digit_offset_x[i] * SPRING_STRENGTH;

      // Apply spring force and damping (scaled by physics rate)
      digit_velocity_x[i] += spring_force_x * physicsScale;
      digit_velocity_x[i] *= pow(DAMPING, physicsScale);  // Exponential damping scaling

      // Update position
      digit_offset_x[i] += digit_velocity_x[i] * physicsScale;

      // Clamp to visible movement range (allow up to 4 pixels)
      if (digit_offset_x[i] > 4) digit_offset_x[i] = 4;
      if (digit_offset_x[i] < -4) digit_offset_x[i] = -4;

      // Stop when very close to rest position
      if (abs(digit_offset_x[i]) < 0.1 && abs(digit_velocity_x[i]) < 0.1) {
        digit_offset_x[i] = 0;
        digit_velocity_x[i] = 0;
      }
    }
  }
}

// Main Pong animation update loop
void updatePongAnimation(struct tm* timeinfo) {
  unsigned long currentMillis = millis();

  // Throttle updates to PONG_UPDATE_INTERVAL (50 FPS)
  if (currentMillis - last_pong_update < PONG_UPDATE_INTERVAL) {
    return;
  }
  last_pong_update = currentMillis;

  // Update displayed time and detect digit changes
  if (!time_overridden) {
    int new_hour = timeinfo->tm_hour;
    int new_min = timeinfo->tm_min;

    // Detect minute change (trigger digit transitions)
    if (new_min != displayed_min || new_hour != displayed_hour) {
      // Check each digit for changes
      int old_hour_tens = displayed_hour / 10;
      int old_hour_ones = displayed_hour % 10;
      int old_min_tens = displayed_min / 10;
      int old_min_ones = displayed_min % 10;

      int new_hour_tens = new_hour / 10;
      int new_hour_ones = new_hour % 10;
      int new_min_tens = new_min / 10;
      int new_min_ones = new_min % 10;

      if (old_hour_tens != new_hour_tens) {
        triggerDigitTransition(0, '0' + old_hour_tens, '0' + new_hour_tens);
      }
      if (old_hour_ones != new_hour_ones) {
        triggerDigitTransition(1, '0' + old_hour_ones, '0' + new_hour_ones);
      }
      if (old_min_tens != new_min_tens) {
        triggerDigitTransition(3, '0' + old_min_tens, '0' + new_min_tens);
      }
      if (old_min_ones != new_min_ones) {
        triggerDigitTransition(4, '0' + old_min_ones, '0' + new_min_ones);
      }

      displayed_hour = new_hour;
      displayed_min = new_min;
    }
  }

  int seconds = timeinfo->tm_sec;

  // Check if any digit is currently breaking
  bool any_digit_breaking = false;
  int breaking_digit_index = -1;
  for (int i = 0; i < 5; i++) {
    if (digit_transitions[i].state == DIGIT_BREAKING) {
      any_digit_breaking = true;
      breaking_digit_index = i;
      break;
    }
  }

  // Multi-ball mode: ONLY when digit is breaking (not just at :55)
  if (any_digit_breaking && seconds >= MULTIBALL_ACTIVATE_SECOND) {
    // Multi-ball mode: spawn 2nd ball and reposition paddle
    // 1st ball continues normal play, 2nd ball is the "breaker"

    // Use the breaking digit as target
    int target_digit = breaking_digit_index;

    // Spawn 2nd ball if not active
    if (!pong_balls[1].active) {
      spawnPongBall(1);
      pong_balls[1].state = PONG_BALL_NORMAL;  // Immediately active (no spawn delay)
    }

    // Reposition paddle to be under the target digit
    if (target_digit >= 0) {
      breakout_paddle.target_x = DIGIT_X[target_digit] + 7;  // Center of digit
    }

    // Apply speed boost to both balls
    for (int i = 0; i < MAX_PONG_BALLS; i++) {
      if (pong_balls[i].active && pong_balls[i].state == PONG_BALL_NORMAL) {
        int speed = PONG_BALL_SPEED_BOOST;
        if (pong_balls[i].vx > 0) pong_balls[i].vx = speed;
        else pong_balls[i].vx = -speed;
        if (pong_balls[i].vy > 0) pong_balls[i].vy = speed;
        else pong_balls[i].vy = -speed;
      }
    }

  } else {
    // Normal mode: single ball, normal speed, paddle auto-tracks
    pong_balls[1].active = false;  // Deactivate 2nd ball

    // Normal speed for ball 0
    if (pong_balls[0].active && pong_balls[0].state == PONG_BALL_NORMAL) {
      int speed = settings.pongBallSpeed;
      if (pong_balls[0].vx > 0) pong_balls[0].vx = speed;
      else pong_balls[0].vx = -speed;
      if (pong_balls[0].vy > 0) pong_balls[0].vy = speed;
      else pong_balls[0].vy = -speed;
    }

    // Paddle returns to auto-tracking mode (handled by updateBreakoutPaddle)
  }

  // Update all active balls
  for (int i = 0; i < MAX_PONG_BALLS; i++) {
    if (pong_balls[i].active) {
      updatePongBall(i);
      checkPongCollisions(i);
    }
  }

  // Update game systems
  updateBreakoutPaddle();
  updateDigitTransitions();
  updatePongFragments();
  updateAssemblyFragments();  // Update assembling digit fragments
  updateDigitBouncePong();  // Spring-based bounce physics for Pong
}

// Main display function for Pong Clock
void displayClockWithPong() {
  // Initialize Pong on first call
  static bool pong_initialized = false;
  if (!pong_initialized) {
    initPongAnimation();
    pong_initialized = true;
  }

  struct tm timeinfo;
  if (!getTimeWithTimeout(&timeinfo)) {
    display.setTextSize(1);
    display.setCursor(20, 28);
    if (!ntpSynced) {
      display.print("Syncing time...");
    } else {
      display.print("Time Error");
    }
    return;
  }

  // Update animation
  updatePongAnimation(&timeinfo);

  // RENDERING ORDER (back to front):

  // 1. Date at top (textSize 1)
  display.setTextSize(1);
  char dateStr[12];
  switch (settings.dateFormat) {
    case 0: sprintf(dateStr, "%02d/%02d/%04d", timeinfo.tm_mday,
                    timeinfo.tm_mon + 1, timeinfo.tm_year + 1900); break;
    case 1: sprintf(dateStr, "%02d/%02d/%04d", timeinfo.tm_mon + 1,
                    timeinfo.tm_mday, timeinfo.tm_year + 1900); break;
    case 2: sprintf(dateStr, "%04d-%02d-%02d", timeinfo.tm_year + 1900,
                    timeinfo.tm_mon + 1, timeinfo.tm_mday); break;
  }
  display.setCursor((SCREEN_WIDTH - 60) / 2, 4);
  display.print(dateStr);

  // 2. Digits (with transitions and bounce)
  drawPongDigits();

  // 3. Fragments (digit breaks + ball hits)
  drawPongFragments();

  // 4. Paddles
  drawBreakoutPaddle();

  // 5. Ball(s) (on top)
  drawPongBall();
}
