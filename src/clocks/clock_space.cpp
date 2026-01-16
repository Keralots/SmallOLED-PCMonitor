/*
 * SmallOLED-PCMonitor - Space Invaders Clock
 *
 * Clock style 3: Space character (Invader or Ship) patrols and shoots
 * laser at digits when minute changes.
 */

#include "../config/config.h"
#include "../display/display.h"
#include "clocks.h"
#include "clock_constants.h"
#include "clock_globals.h"

// ========== Forward Declarations ==========
void fireSpaceLaser(int target_digit_idx);
void spawnSpaceExplosion(int digitIndex);
SpaceFragment* findFreeSpaceFragment();

// ========== Space Clock Animation Functions (Clock Style 3 - Unified) ==========

// Draw Space character sprite (Invader or Ship based on settings.spaceCharacterType)
void drawSpaceCharacter(int x, int y, int frame) {
  // Bounds check
  if (x < -12 || x > SCREEN_WIDTH + 12) return;
  if (y < -10 || y > SCREEN_HEIGHT + 10) return;

  if (settings.spaceCharacterType == 0) {
    // Draw Invader sprite (11x11 pixels, classic invader design)
    int sx = x - 5;
    int sy = y - 4;

    // Antennae
    display.drawPixel(sx + 2, sy, DISPLAY_WHITE);
    display.drawPixel(sx + 8, sy, DISPLAY_WHITE);

    // Head
    display.fillRect(sx + 3, sy + 1, 5, 1, DISPLAY_WHITE);

    // Body
    display.fillRect(sx + 2, sy + 2, 7, 1, DISPLAY_WHITE);
    display.fillRect(sx + 1, sy + 3, 9, 1, DISPLAY_WHITE);

    // Eyes
    display.fillRect(sx, sy + 4, 3, 1, DISPLAY_WHITE);
    display.drawPixel(sx + 5, sy + 4, DISPLAY_WHITE);
    display.fillRect(sx + 8, sy + 4, 3, 1, DISPLAY_WHITE);

    // Mouth
    display.fillRect(sx, sy + 5, 11, 1, DISPLAY_WHITE);

    // Legs (frame-dependent)
    if (frame == 0) {
      // Legs down
      display.drawPixel(sx + 1, sy + 6, DISPLAY_WHITE);
      display.fillRect(sx + 4, sy + 6, 3, 1, DISPLAY_WHITE);
      display.drawPixel(sx + 9, sy + 6, DISPLAY_WHITE);
      display.fillRect(sx, sy + 7, 2, 1, DISPLAY_WHITE);
      display.drawPixel(sx + 5, sy + 7, DISPLAY_WHITE);
      display.fillRect(sx + 9, sy + 7, 2, 1, DISPLAY_WHITE);
    } else {
      // Legs up
      display.fillRect(sx + 2, sy + 6, 7, 1, DISPLAY_WHITE);
      display.drawPixel(sx + 1, sy + 7, DISPLAY_WHITE);
      display.drawPixel(sx + 9, sy + 7, DISPLAY_WHITE);
      display.fillRect(sx, sy + 8, 2, 1, DISPLAY_WHITE);
      display.fillRect(sx + 9, sy + 8, 2, 1, DISPLAY_WHITE);
    }
  } else {
    // Draw Ship sprite (11x7 pixels, classic ship design)
    int sx = x - 5;
    int sy = y - 3;

    // Top point
    display.drawPixel(sx + 5, sy, DISPLAY_WHITE);

    // Upper body
    display.fillRect(sx + 4, sy + 1, 3, 1, DISPLAY_WHITE);
    display.fillRect(sx + 3, sy + 2, 5, 1, DISPLAY_WHITE);

    // Main body
    display.fillRect(sx + 1, sy + 3, 9, 1, DISPLAY_WHITE);
    display.fillRect(sx, sy + 4, 11, 1, DISPLAY_WHITE);

    // Wings - animate between two frames for thruster effect
    if (frame == 0) {
      // Wings down
      display.fillRect(sx, sy + 5, 3, 1, DISPLAY_WHITE);
      display.fillRect(sx + 8, sy + 5, 3, 1, DISPLAY_WHITE);
      display.drawPixel(sx, sy + 6, DISPLAY_WHITE);
      display.drawPixel(sx + 10, sy + 6, DISPLAY_WHITE);
    } else {
      // Wings up (thruster pulse)
      display.fillRect(sx + 1, sy + 5, 2, 1, DISPLAY_WHITE);
      display.fillRect(sx + 8, sy + 5, 2, 1, DISPLAY_WHITE);
      display.drawPixel(sx + 1, sy + 6, DISPLAY_WHITE);
      display.drawPixel(sx + 9, sy + 6, DISPLAY_WHITE);
    }
  }
}

// Handle patrol state - slow left-right drift
void handleSpacePatrolState() {
  space_x += (settings.spacePatrolSpeed / 10.0) * space_patrol_direction;

  // Reverse direction at boundaries
  if (space_x <= SPACE_PATROL_LEFT) {
    space_x = SPACE_PATROL_LEFT;
    space_patrol_direction = 1;
  } else if (space_x >= SPACE_PATROL_RIGHT) {
    space_x = SPACE_PATROL_RIGHT;
    space_patrol_direction = -1;
  }
}

// Handle sliding to target position - fast horizontal movement
void handleSpaceSlidingState() {
  float target_x = target_x_positions[current_target_index];

  // Slide horizontally to target
  if (abs(space_x - target_x) > MOVEMENT_THRESHOLD) {
    if (space_x < target_x) {
      space_x += (settings.spaceAttackSpeed / 10.0);
      if (space_x > target_x) space_x = target_x;
    } else {
      space_x -= (settings.spaceAttackSpeed / 10.0);
      if (space_x < target_x) space_x = target_x;
    }
  } else {
    // Reached target position - start shooting
    space_x = target_x;
    space_state = SPACE_SHOOTING;
    fireSpaceLaser(target_digit_index[current_target_index]);
  }
}

// Handle shooting state - laser update handles transition
void handleSpaceShootingState() {
  // Laser update handles transition to EXPLODING_DIGIT
}

// Handle exploding state - move away quickly after 5 frames
void handleSpaceExplodingState() {
  space_explosion_timer++;
  // Move away quickly - don't wait for explosion to finish
  if (space_explosion_timer >= SPACE_EXPLOSION_FRAMES) {
    current_target_index++;
    if (current_target_index < num_targets) {
      space_state = SPACE_MOVING_NEXT;
    } else {
      space_state = SPACE_RETURNING;
    }
  }
}

// Handle moving to next target - slide to next digit
void handleSpaceMovingNextState() {
  float target_x = target_x_positions[current_target_index];

  if (abs(space_x - target_x) > MOVEMENT_THRESHOLD) {
    if (space_x < target_x) {
      space_x += (settings.spaceAttackSpeed / 10.0);
      if (space_x > target_x) space_x = target_x;
    } else {
      space_x -= (settings.spaceAttackSpeed / 10.0);
      if (space_x < target_x) space_x = target_x;
    }
  } else {
    space_x = target_x;
    space_state = SPACE_SHOOTING;
    fireSpaceLaser(target_digit_index[current_target_index]);
  }
}

// Handle returning to patrol - slide back to center
void handleSpaceReturningState() {
  float center_x = SCREEN_CENTER_X;

  if (abs(space_x - center_x) > MOVEMENT_THRESHOLD) {
    if (space_x < center_x) {
      space_x += (settings.spacePatrolSpeed / 10.0);
      if (space_x > center_x) space_x = center_x;
    } else {
      space_x -= (settings.spacePatrolSpeed / 10.0);
      if (space_x < center_x) space_x = center_x;
    }
  } else {
    space_x = center_x;
    space_state = SPACE_PATROL;
    time_overridden = false;  // Allow time to resync
  }
}

// Draw space laser beam (upward)
void drawSpaceLaser(Laser* laser) {
  if (!laser->active) return;

  // Vertical laser beam shooting UPWARD
  for (int i = 0; i < (int)laser->length; i += 2) {
    int ly = (int)laser->y - i;  // Subtract to go upward
    if (ly >= 0 && ly < SCREEN_HEIGHT) {
      display.drawPixel((int)laser->x, ly, DISPLAY_WHITE);
      display.drawPixel((int)laser->x + 1, ly, DISPLAY_WHITE);
    }
  }

  // Impact flash at end (top of beam)
  int end_y = (int)(laser->y - laser->length);
  if (end_y >= 0 && end_y < SCREEN_HEIGHT) {
    display.drawPixel((int)laser->x - 1, end_y, DISPLAY_WHITE);
    display.drawPixel((int)laser->x + 2, end_y, DISPLAY_WHITE);
  }
}

// Update space laser
void updateSpaceLaser() {
  if (!space_laser.active) return;

  space_laser.length += (settings.spaceLaserSpeed / 10.0);

  // Check if reached digit (bottom of time digits)
  const int SPACE_TIME_Y = 16;
  int digit_bottom_y = SPACE_TIME_Y + 24;
  int laser_end_y = space_laser.y - space_laser.length;

  if (laser_end_y <= digit_bottom_y) {
    space_laser.active = false;
    spawnSpaceExplosion(space_laser.target_digit_idx);
    updateSpecificDigit(target_digit_index[current_target_index],
                       target_digit_values[current_target_index]);
    space_explosion_timer = 0;
    space_state = SPACE_EXPLODING_DIGIT;
  }

  if (space_laser.length > LASER_MAX_LENGTH) {
    space_laser.length = LASER_MAX_LENGTH;
  }
}

// Fire space laser
void fireSpaceLaser(int target_digit_idx) {
  space_laser.x = space_x;
  space_laser.y = space_y - SPACE_LASER_OFFSET_Y;  // Start from top of character
  space_laser.length = 0;
  space_laser.active = true;
  space_laser.target_digit_idx = target_digit_idx;
}

// Spawn space explosion fragments
void spawnSpaceExplosion(int digitIndex) {
  const int SPACE_TIME_Y = 16;
  int digit_x = DIGIT_X[digitIndex] + 9;
  int digit_y = SPACE_TIME_Y + 12;

  int frag_count = 10;
  float angle_step = (2 * PI) / frag_count;

  for (int i = 0; i < frag_count; i++) {
    SpaceFragment* f = findFreeSpaceFragment();
    if (!f) break;

    float angle = i * angle_step + random(-30, 30) / 100.0;
    float speed = 3.0 + random(-50, 50) / 100.0;  // Base speed ~3.0

    f->x = digit_x + random(-4, 4);
    f->y = digit_y + random(-6, 6);
    f->vx = cos(angle) * speed;
    f->vy = sin(angle) * speed - 1.0;
    f->active = true;
  }
}

// Update space fragments
void updateSpaceFragments() {
  for (int i = 0; i < MAX_SPACE_FRAGMENTS; i++) {
    if (space_fragments[i].active) {
      space_fragments[i].vy += (settings.spaceExplosionGravity / 10.0);
      space_fragments[i].x += space_fragments[i].vx;
      space_fragments[i].y += space_fragments[i].vy;

      if (space_fragments[i].y > 70 ||
          space_fragments[i].x < -5 ||
          space_fragments[i].x > 133) {
        space_fragments[i].active = false;
      }
    }
  }
}

// Draw space fragments
void drawSpaceFragments() {
  for (int i = 0; i < MAX_SPACE_FRAGMENTS; i++) {
    if (space_fragments[i].active) {
      display.fillRect((int)space_fragments[i].x,
                      (int)space_fragments[i].y, 2, 2, DISPLAY_WHITE);
    }
  }
}

// Check if all space fragments are inactive
bool allSpaceFragmentsInactive() {
  for (int i = 0; i < MAX_SPACE_FRAGMENTS; i++) {
    if (space_fragments[i].active) return false;
  }
  return true;
}

// Find free space fragment
SpaceFragment* findFreeSpaceFragment() {
  for (int i = 0; i < MAX_SPACE_FRAGMENTS; i++) {
    if (!space_fragments[i].active) return &space_fragments[i];
  }
  return nullptr;
}

// Main space animation update
void updateSpaceAnimation(struct tm* timeinfo) {
  unsigned long currentMillis = millis();

  const int SPACE_ANIM_SPEED = 50;  // 50ms = 20 FPS
  const int SPRITE_TOGGLE_SPEED = 200;  // Slow retro animation

  if (currentMillis - last_space_update < SPACE_ANIM_SPEED) return;
  last_space_update = currentMillis;

  int seconds = timeinfo->tm_sec;
  int current_minute = timeinfo->tm_min;

  // Reset trigger
  if (current_minute != last_minute) {
    last_minute = current_minute;
    animation_triggered = false;
  }

  // Toggle sprite
  if (currentMillis - last_space_sprite_toggle >= SPRITE_TOGGLE_SPEED) {
    space_anim_frame = 1 - space_anim_frame;
    last_space_sprite_toggle = currentMillis;
  }

  // Trigger at 55 seconds - transition from PATROL to SLIDING
  if (seconds >= 55 && !animation_triggered && space_state == SPACE_PATROL) {
    animation_triggered = true;
    time_overridden = true;
    time_override_start = millis();
    calculateTargetDigits(displayed_hour, displayed_min);

    if (num_targets > 0) {
      current_target_index = 0;
      space_state = SPACE_SLIDING;
    }
  }

  updateSpaceFragments();
  updateSpaceLaser();

  switch (space_state) {
    case SPACE_PATROL:
      handleSpacePatrolState();
      break;
    case SPACE_SLIDING:
      handleSpaceSlidingState();
      break;
    case SPACE_SHOOTING:
      handleSpaceShootingState();
      break;
    case SPACE_EXPLODING_DIGIT:
      handleSpaceExplodingState();
      break;
    case SPACE_MOVING_NEXT:
      handleSpaceMovingNextState();
      break;
    case SPACE_RETURNING:
      handleSpaceReturningState();
      break;
  }
}

// Display clock with space animation
void displayClockWithSpaceInvader() {
  struct tm timeinfo;
  if(!getTimeWithTimeout(&timeinfo)) {
    display.setTextSize(1);
    display.setCursor(20, 28);
    if (!ntpSynced) {
      display.print("Syncing time...");
    } else {
      display.print("Time Error");
    }
    return;
  }

  // Update animation FIRST so time advances before drawing
  updateSpaceAnimation(&timeinfo);

  // Time management
  if (!time_overridden) {
    displayed_hour = timeinfo.tm_hour;
    displayed_min = timeinfo.tm_min;
  }

  // Check if time override should be cleared
  if (time_overridden) {
    bool ntp_matches = (timeinfo.tm_hour == displayed_hour &&
                        timeinfo.tm_min == displayed_min &&
                        space_state == SPACE_PATROL);
    bool timeout_expired = (millis() - time_override_start > TIME_OVERRIDE_MAX_MS);

    if (ntp_matches || timeout_expired) {
      time_overridden = false;
      // If timeout expired but NTP doesn't match, force sync to real time
      if (timeout_expired && !ntp_matches) {
        displayed_hour = timeinfo.tm_hour;
        displayed_min = timeinfo.tm_min;
      }
    }
  }

  // Date (at top, Y=4)
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

  // Time digits
  const int SPACE_TIME_Y = 16;
  display.setTextSize(3);
  char digits[5];
  digits[0] = '0' + (displayed_hour / 10);
  digits[1] = '0' + (displayed_hour % 10);
  digits[2] = shouldShowColon() ? ':' : ' ';  // Blinking colon
  digits[3] = '0' + (displayed_min / 10);
  digits[4] = '0' + (displayed_min % 10);

  for (int i = 0; i < 5; i++) {
    display.setCursor(DIGIT_X[i], SPACE_TIME_Y);
    display.print(digits[i]);
  }

  // Render space character (ALWAYS visible - either patrolling or attacking)
  drawSpaceCharacter((int)space_x, (int)space_y, space_anim_frame);

  // Render laser if active
  if (space_laser.active) {
    drawSpaceLaser(&space_laser);
  }

  // Render explosion fragments
  drawSpaceFragments();
}
