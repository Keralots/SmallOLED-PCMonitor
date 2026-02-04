/*
 * SmallOLED-PCMonitor - Pac-Man Clock Implementation
 *
 * Pac-Man clock style with pellet-based digit display and eating animations.
 */

#include "../config/config.h"
#include "../display/display.h"
#include "clocks.h"
#include "clock_globals.h"

// ========== Pac-Man Digit X positions ==========
// Pac-Man clock uses different spacing (wider gaps for pellet layout)
static const int DIGIT_X_PACMAN[5] = {1, 30, 56, 74, 103};

// ========== Digit Patterns for Pac-Man Pellet Display (5x7 grid) ==========
// Bitmap patterns for digits 0-9 (5x7 grid, 1 = pellet present, 0 = no pellet)
static const uint8_t digitPatterns[10][DIGIT_GRID_H] = {
  // 0: Oval shape with hollow center
  {0b01110, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01110},
  // 1: Vertical line
  {0b00100, 0b01100, 0b00100, 0b00100, 0b00100, 0b00100, 0b01110},
  // 2: Z shape
  {0b01110, 0b10001, 0b00001, 0b00010, 0b00100, 0b01000, 0b11111},
  // 3: Backwards E
  {0b01110, 0b10001, 0b00001, 0b00110, 0b00001, 0b10001, 0b01110},
  // 4: Triangle/4 shape
  {0b00010, 0b00110, 0b01010, 0b10010, 0b11111, 0b00010, 0b00010},
  // 5: S shape
  {0b11111, 0b10000, 0b11110, 0b00001, 0b00001, 0b10001, 0b01110},
  // 6: Loop with tail
  {0b00110, 0b01000, 0b10000, 0b10110, 0b10001, 0b10001, 0b01110},
  // 7: Top bar and diagonal
  {0b11111, 0b00001, 0b00010, 0b00100, 0b01000, 0b01000, 0b01000},
  // 8: Two loops
  {0b01110, 0b10001, 0b10001, 0b01110, 0b10001, 0b10001, 0b01110},
  // 9: Loop with tail (inverse of 6)
  {0b01110, 0b10001, 0b10001, 0b01111, 0b00001, 0b00010, 0b01100}
};

// ========== Eating Paths for Each Digit (50 steps max) ==========
// Format: {col, row} - Pac-Man follows these paths to eat digit pellets
static const PathStep eatingPaths[10][50] = {
  // Digit 0: Smooth oval outline - only visits actual pellet positions
  // Pattern: {0b01110, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01110}
  // Pellets at: row0(1,2,3), row1-5(0,4), row6(1,2,3)
  {{1,6}, {2,6}, {3,6},                 // Bottom arc (left to right)
   {4,5}, {4,4}, {4,3}, {4,2}, {4,1},   // Right side up
   {3,0}, {2,0}, {1,0},                 // Top arc (right to left)
   {0,1}, {0,2}, {0,3}, {0,4}, {0,5},   // Left side down
   {255,255}},
  // Digit 1: Bottom bar, up stem, serif
  // Pattern: {0b00100, 0b01100, 0b00100, 0b00100, 0b00100, 0b00100, 0b01110}
  // Pellets at: row0(2), row1(1,2), row2-5(2), row6(1,2,3)
  {{1,6}, {2,6}, {3,6},                 // Bottom bar
   {2,5}, {2,4}, {2,3}, {2,2},          // Stem up
   {1,1}, {2,1},                        // Serif + top of stem
   {2,0},                               // Top
   {255,255}},
  // Digit 2: Bottom bar, diagonal up, top curve
  // Pattern: {0b01110, 0b10001, 0b00001, 0b00010, 0b00100, 0b01000, 0b11111}
  // Pellets at: row0(1,2,3), row1(0,4), row2(4), row3(3), row4(2), row5(1), row6(0,1,2,3,4)
  {{0,6}, {1,6}, {2,6}, {3,6}, {4,6},   // Bottom bar (full width)
   {1,5},                               // Diagonal start
   {2,4},                               // Diagonal
   {3,3},                               // Diagonal
   {4,2}, {4,1},                        // Right side up
   {3,0}, {2,0}, {1,0},                 // Top arc
   {0,1},                               // Left top
   {255,255}},
  // Digit 3: Smooth S-curve from bottom
  // Pattern: {0b01110, 0b10001, 0b00001, 0b00110, 0b00001, 0b10001, 0b01110}
  // Pellets at: row0(1,2,3), row1(0,4), row2(4), row3(2,3), row4(4), row5(0,4), row6(1,2,3)
  {{1,6}, {2,6}, {3,6},                 // Bottom arc
   {4,5},                               // Right lower
   {4,4},                               // Right side
   {3,3}, {2,3},                        // Middle bar (right to left)
   {4,2},                               // Right upper
   {4,1},                               // Right side
   {3,0}, {2,0}, {1,0},                 // Top arc
   {0,1},                               // Left top corner
   {0,5},                               // Left bottom corner
   {255,255}},
  // Digit 4: Stem up first, then diagonal down, then bar
  // Pattern: {0b00010, 0b00110, 0b01010, 0b10010, 0b11111, 0b00010, 0b00010}
  // Pellets at: row0(3), row1(2,3), row2(1,3), row3(0,3), row4(0,1,2,3,4), row5(3), row6(3)
  {{3,6},                               // Bottom stem
   {3,5},                               // Stem up
   {3,4},                               // Stem at bar level
   {3,3},                               // Stem up
   {3,2},                               // Stem up
   {3,1},                               // Stem up
   {3,0},                               // Top of stem
   {2,1},                               // Diagonal left (row 1)
   {1,2},                               // Diagonal down-left (row 2)
   {0,3},                               // Left corner (row 3)
   {0,4},                               // Bar left end
   {1,4}, {2,4},                        // Bar continues (3,4 already eaten)
   {4,4},                               // Bar right end
   {255,255}},
  // Digit 5: Starting from bottom-left, going up
  // Pattern: {0b11111, 0b10000, 0b11110, 0b00001, 0b00001, 0b10001, 0b01110}
  // Pellets at: row0(0,1,2,3,4), row1(0), row2(0,1,2,3), row3(4), row4(4), row5(0,4), row6(1,2,3)
  {{0,5},                               // Start at left bottom corner
   {1,6}, {2,6}, {3,6},                 // Bottom arc
   {4,5}, {4,4}, {4,3},                 // Right side up
   {3,2}, {2,2}, {1,2}, {0,2},          // Middle bar (right to left)
   {0,1}, {0,0},                        // Left side up
   {1,0}, {2,0}, {3,0}, {4,0},          // Top bar
   {255,255}},
  // Digit 6: Bottom arc, up left, across middle, tail
  // Pattern: {0b00110, 0b01000, 0b10000, 0b10110, 0b10001, 0b10001, 0b01110}
  // Pellets at: row0(2,3), row1(1), row2(0), row3(0,2,3), row4(0,4), row5(0,4), row6(1,2,3)
  {{1,6}, {2,6}, {3,6},                 // Bottom arc
   {4,5}, {4,4},                        // Right side up
   {3,3}, {2,3},                        // Middle bar (partial)
   {0,3}, {0,4}, {0,5},                 // Left side (middle down)
   {0,2},                               // Left upper
   {1,1},                               // Diagonal
   {2,0}, {3,0},                        // Top tail
   {255,255}},
  // Digit 7: Top bar, smooth diagonal down
  // Pattern: {0b11111, 0b00001, 0b00010, 0b00100, 0b01000, 0b01000, 0b01000}
  // Pellets at: row0(0,1,2,3,4), row1(4), row2(3), row3(2), row4(1), row5(1), row6(1)
  {{0,0}, {1,0}, {2,0}, {3,0}, {4,0},   // Top bar
   {4,1},                               // Right corner
   {3,2},                               // Diagonal
   {2,3},                               // Diagonal
   {1,4}, {1,5}, {1,6},                 // Vertical finish
   {255,255}},
  // Digit 8: Figure-8, bottom loop then top loop
  // Pattern: {0b01110, 0b10001, 0b10001, 0b01110, 0b10001, 0b10001, 0b01110}
  // Pellets at: row0(1,2,3), row1(0,4), row2(0,4), row3(1,2,3), row4(0,4), row5(0,4), row6(1,2,3)
  {{1,6}, {2,6}, {3,6},                 // Bottom arc
   {4,5}, {4,4},                        // Right lower
   {3,3}, {2,3}, {1,3},                 // Middle bar
   {0,4}, {0,5},                        // Left lower
   {0,2}, {0,1},                        // Left upper
   {1,0}, {2,0}, {3,0},                 // Top arc
   {4,1}, {4,2},                        // Right upper
   {255,255}},
  // Digit 9: Bottom tail, up right, top loop
  // Pattern: {0b01110, 0b10001, 0b10001, 0b01111, 0b00001, 0b00010, 0b01100}
  // Pellets at: row0(1,2,3), row1(0,4), row2(0,4), row3(1,2,3,4), row4(4), row5(3), row6(1,2)
  {{1,6}, {2,6},                        // Bottom tail
   {3,5},                               // Diagonal
   {4,4}, {4,3},                        // Right side up
   {3,3}, {2,3}, {1,3},                 // Middle bar
   {0,2}, {0,1},                        // Left upper
   {1,0}, {2,0}, {3,0},                 // Top arc
   {4,1}, {4,2},                        // Right upper
   {255,255}}
};

// ========== Pac-Man Clock Implementation ==========

void displayClockWithPacman() {
  // Initialize Pac-Man on first call
  static bool pacman_initialized = false;
  if (!pacman_initialized) {
    generatePellets();
    pacman_initialized = true;
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

  // FIRST: Always sync time unless actively animating
  // This MUST happen before updatePacmanAnimation() so the animation
  // calculations use the correct current time, not stale values
  bool isAnimating = (pacman_state != PACMAN_PATROL);
  if (!isAnimating || !time_overridden) {
    displayed_hour = timeinfo.tm_hour;
    displayed_min = timeinfo.tm_min;
    time_overridden = false;
  }

  // THEN: Update animation (now using correct displayed_hour/displayed_min)
  updatePacmanAnimation(&timeinfo);

  // Date at top
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

  // Draw time digits as pellets
  uint8_t digitValues[5];
  digitValues[0] = displayed_hour / 10;
  digitValues[1] = displayed_hour % 10;
  digitValues[2] = 10;  // Colon marker (not a digit)
  digitValues[3] = displayed_min / 10;
  digitValues[4] = displayed_min % 10;

  for (int i = 0; i < 5; i++) {
    if (i == 2) {
      // Draw colon as two pellets (top and bottom)
      // Center colon between hour digits and minute digits
      if (shouldShowColon()) {
        // H2 ends at: DIGIT_X_PACMAN[1] + 4 * PELLET_SPACING
        // M1 starts at: DIGIT_X_PACMAN[3]
        // Center the colon in the gap between them
        int colon_x = (DIGIT_X_PACMAN[1] + 4 * PELLET_SPACING + DIGIT_X_PACMAN[3]) / 2;
        display.fillCircle(colon_x, TIME_Y_PACMAN + 8, PELLET_SIZE, DISPLAY_WHITE);   // Top dot (lowered)
        display.fillCircle(colon_x, TIME_Y_PACMAN + 18, PELLET_SIZE, DISPLAY_WHITE);  // Bottom dot (lowered)
      }
      continue;
    }

    // Apply bounce offset
    int base_y = TIME_Y_PACMAN + (int)digit_offset_y[i];
    int base_x = DIGIT_X_PACMAN[i];

    // Draw digit pellets
    uint8_t digit = digitValues[i];
    const uint8_t* pattern = digitPatterns[digit];

    for (uint8_t row = 0; row < DIGIT_GRID_H; row++) {
      for (uint8_t col = 0; col < DIGIT_GRID_W; col++) {
        // Check if this pellet exists in the digit pattern
        uint8_t pellet_bit = (pattern[row] >> (DIGIT_GRID_W - 1 - col)) & 1;
        if (!pellet_bit) continue;

        // Calculate pellet index (0-34)
        uint8_t pellet_idx = row * DIGIT_GRID_W + col;
        // Check if pellet has been eaten
        uint8_t byte_idx = pellet_idx / 8;
        uint8_t bit_mask = 1 << (pellet_idx % 8);
        bool is_eaten = (digitEatenPellets[i][byte_idx] & bit_mask) != 0;

        if (!is_eaten) {
          // Draw pellet (small circle like patrol pellets)
          int px = base_x + col * PELLET_SPACING;
          int py = base_y + row * PELLET_SPACING;
          display.fillCircle(px, py, PELLET_SIZE, DISPLAY_WHITE);
        }
      }
    }
  }

  // Draw patrol pellets
  drawPellets();

  // Draw Pac-Man
  drawPacman((int)pacman_x, (int)pacman_y, pacman_direction, pacman_mouth_frame);

  // Draw no-WiFi icon if disconnected
  if (!wifiConnected) {
    drawNoWiFiIcon(0, 0);
  }
}

void updatePacmanAnimation(struct tm* timeinfo) {
  unsigned long currentMillis = millis();

  // Update bounce animation (runs every frame for smooth physics)
  updateDigitBounce();

  // Throttle updates
  if (currentMillis - last_pacman_update < PACMAN_ANIM_SPEED) {
    return;
  }
  last_pacman_update = currentMillis;

  // Mouth animation (waka-waka)
  if (currentMillis - last_pacman_mouth_toggle >= settings.pacmanMouthSpeed * 10) {
    pacman_mouth_frame = (pacman_mouth_frame + 1) % 4;  // 0-3 frames
    last_pacman_mouth_toggle = currentMillis;
  }

  int seconds = timeinfo->tm_sec;
  int current_minute = timeinfo->tm_min;

  // Reset trigger when minute changes
  if (current_minute != last_minute_pacman) {
    last_minute_pacman = current_minute;
    pacman_animation_triggered = false;
  }

  // Trigger animation at 55 seconds
  if (seconds >= 55 && !pacman_animation_triggered && pacman_state == PACMAN_PATROL) {
    pacman_animation_triggered = true;
    time_overridden = true;
    time_override_start = millis();

    // Calculate which digits will change
    calculateTargetDigits(displayed_hour, displayed_min);

    if (num_targets > 0) {
      // Build queue in left-to-right order, SKIPPING the colon (position 2)
      target_queue_length = 0;
      for (int i = 0; i < num_targets; i++) {  // Left to right order
        // Skip colon position - Pac-Man only eats digits, not colons!
        if (target_digit_index[i] != 2) {
          target_digit_queue[target_queue_length] = target_digit_index[i];
          target_digit_new_values[target_queue_length] = target_digit_values[i];
          target_queue_length++;
        }
      }
      target_queue_index = 0;

      // Only start animation if we have actual digits to eat
      if (target_queue_length > 0) {
        // Move to TARGETING state - Pac-Man will rush to the first digit
        pacman_state = PACMAN_TARGETING;
        // Set direction toward first digit's first pellet
        uint8_t first_idx = target_digit_queue[0];
        uint8_t first_digit_val = 0;
        if (first_idx == 0) first_digit_val = displayed_hour / 10;
        else if (first_idx == 1) first_digit_val = displayed_hour % 10;
        else if (first_idx == 3) first_digit_val = displayed_min / 10;
        else if (first_idx == 4) first_digit_val = displayed_min % 10;

        const PathStep first_step = eatingPaths[first_digit_val][0];
        float first_x = DIGIT_X_PACMAN[first_idx] + first_step.col * PELLET_SPACING;
        pacman_direction = (first_x > pacman_x) ? 1 : -1;
      } else {
        // No digits to eat (only colon changed), cancel animation
        pacman_animation_triggered = false;
        time_overridden = false;
      }
    }
  }

  // State machine
  switch (pacman_state) {
    case PACMAN_PATROL:
      updatePacmanPatrol();
      break;

    case PACMAN_TARGETING:
      // 2-phase L-shaped movement: horizontal first (along patrol), then vertical to first pellet
      {
        uint8_t target_idx = target_digit_queue[target_queue_index];

        // Get the CURRENT digit value to determine the path start position
        uint8_t current_digit_value = 0;
        if (target_idx == 0) current_digit_value = displayed_hour / 10;
        else if (target_idx == 1) current_digit_value = displayed_hour % 10;
        else if (target_idx == 3) current_digit_value = displayed_min / 10;
        else if (target_idx == 4) current_digit_value = displayed_min % 10;

        // Target the first pellet position in the eating path
        const PathStep first_step = eatingPaths[current_digit_value][0];
        float target_x = DIGIT_X_PACMAN[target_idx] + first_step.col * PELLET_SPACING;
        float target_y = TIME_Y_PACMAN + first_step.row * PELLET_SPACING;
        float speed = settings.pacmanEatingSpeed / 10.0;  // Use eating speed for targeting

        float dx = target_x - pacman_x;
        float dy = target_y - pacman_y;

        // Phase 1: Move horizontally along patrol line first
        if (abs(dx) > speed) {
          // Still need to move horizontally
          pacman_x += (dx > 0) ? speed : -speed;
          pacman_direction = (dx > 0) ? 1 : -1;  // Face left or right
        }
        // Phase 2: Move vertically to first pellet (only when X is aligned)
        else if (abs(dy) > speed) {
          // Snap X to exact position, then move vertically
          pacman_x = target_x;
          pacman_y += (dy > 0) ? speed : -speed;
          pacman_direction = (dy > 0) ? 2 : -2;  // Face down or up
        }
        // Reached target - start eating
        else {
          pacman_x = target_x;
          pacman_y = target_y;
          startEatingDigit(target_idx, current_digit_value);
        }

        // Eat patrol pellets while targeting (keep patrol area clean)
        updatePellets();
      }
      break;

    case PACMAN_EATING:
      updatePacmanEating();
      break;

    case PACMAN_RETURNING:
      // Move back to patrol Y
      pacman_direction = 2;  // Face down while returning
      {
        float speed = settings.pacmanEatingSpeed / 10.0;  // Use eating speed for quick return
        float dy = PACMAN_PATROL_Y - pacman_y;

        // Snap to patrol line when within one speed step (more robust at high speeds)
        if (abs(dy) <= speed * 1.5) {
          pacman_y = PACMAN_PATROL_Y;

          // Apply pending digit update now that Pac-Man is back at patrol
          if (pending_digit_index != 255) {
            // Clear eaten pellets BEFORE updating digit value (prevents old digit reappearance)
            memset(digitEatenPellets[pending_digit_index], 0, 5);

            updateSpecificDigit(pending_digit_index, pending_digit_value);

            // Trigger bounce animation if enabled
            if (settings.pacmanBounceEnabled) {
              triggerDigitBounce(pending_digit_index);
            }

            pending_digit_index = 255;  // Clear pending flag
          }

          // Check if there are more digits to eat
          if (target_queue_index < target_queue_length) {
            // More digits to eat - move to next digit
            pacman_state = PACMAN_TARGETING;
            // Set direction toward next digit's first pellet
            uint8_t next_idx = target_digit_queue[target_queue_index];
            uint8_t next_digit_val = 0;
            if (next_idx == 0) next_digit_val = displayed_hour / 10;
            else if (next_idx == 1) next_digit_val = displayed_hour % 10;
            else if (next_idx == 3) next_digit_val = displayed_min / 10;
            else if (next_idx == 4) next_digit_val = displayed_min % 10;

            const PathStep next_step = eatingPaths[next_digit_val][0];
            float next_x = DIGIT_X_PACMAN[next_idx] + next_step.col * PELLET_SPACING;
            pacman_direction = (next_x > pacman_x) ? 1 : -1;
          } else {
            // All done, stay in patrol
            pacman_state = PACMAN_PATROL;
            // Randomly pick left or right direction for patrolling
            pacman_direction = (random(2) == 0) ? 1 : -1;
          }
        } else {
          // Still moving down
          pacman_y += (dy > 0 ? speed : -speed);
        }

        // Eat patrol pellets while returning (keep patrol area clean)
        updatePellets();
      }
      break;
  }
}

void updatePacmanPatrol() {
  float speed = settings.pacmanSpeed / 10.0;

  // Move left/right within bounds
  pacman_x += speed * pacman_direction;

  // Fixed patrol bounds (10 pixels from edges)
  constexpr int PACMAN_PATROL_BOUNDS = 10;
  int left_bound = PACMAN_PATROL_BOUNDS;
  int right_bound = SCREEN_WIDTH - PACMAN_PATROL_BOUNDS;

  if (pacman_x <= left_bound) {
    pacman_x = left_bound;
    pacman_direction = 1;  // Turn right
  } else if (pacman_x >= right_bound) {
    pacman_x = right_bound;
    pacman_direction = -1;  // Turn left
  }

  // Update pellets
  updatePellets();
}

void updatePacmanEating() {
  // Pellet-based eating: Pac-Man follows the eating path, consuming pellets
  uint8_t digit_idx = current_eating_digit_index;
  uint8_t digit_val = current_eating_digit_value;

  int digit_base_x = DIGIT_X_PACMAN[digit_idx];
  int digit_base_y = TIME_Y_PACMAN;
  float speed = settings.pacmanEatingSpeed / 10.0;  // Use eating speed for digit eating

  // Get current path step
  const PathStep* path = eatingPaths[digit_val];
  const PathStep current_step = path[current_path_step];

  // Check if path is complete
  if (current_step.col == 255 || current_step.row == 255) {
    finishEatingDigit();
    return;
  }

  // Calculate target pellet position
  float target_x = digit_base_x + current_step.col * PELLET_SPACING;
  float target_y = digit_base_y + current_step.row * PELLET_SPACING;

  // Calculate distance to target pellet
  float dx = target_x - pacman_x;
  float dy = target_y - pacman_y;
  float dist = sqrt(dx * dx + dy * dy);

  // Update mouth direction based on movement (including diagonals)
  if (dist > 0.1) {
    // Check for diagonal movement (when dx and dy are similar in magnitude)
    float abs_dx = abs(dx);
    float abs_dy = abs(dy);
    float ratio = (abs_dx > abs_dy) ? (abs_dy / abs_dx) : (abs_dx / abs_dy);

    // If ratio is close to 1, it's a diagonal move (within ~40% tolerance)
    if (ratio > 0.6) {
      // Diagonal movement
      if (dx > 0 && dy > 0) {
        pacman_direction = 3;   // Down-right diagonal
      } else if (dx < 0 && dy > 0) {
        pacman_direction = 4;   // Down-left diagonal
      } else if (dx < 0 && dy < 0) {
        pacman_direction = -3;  // Up-left diagonal
      } else {
        pacman_direction = -4;  // Up-right diagonal
      }
    } else if (abs_dx > abs_dy) {
      pacman_direction = (dx > 0) ? 1 : -1;  // Right or left
    } else {
      pacman_direction = (dy > 0) ? 2 : -2;  // Down or up
    }
  }

  // Move toward target pellet
  if (dist <= speed) {
    // Reached this step point - move to next
    pacman_x = target_x;
    pacman_y = target_y;
    current_path_step++;

    // Check if path is complete (next step is terminator)
    const PathStep next_step = path[current_path_step];
    if (next_step.col == 255 || next_step.row == 255) {
      finishEatingDigit();
      return;  // Exit immediately to prevent proximity eating after digit is finished
    }
  } else {
    // Move toward target
    pacman_x += (dx / dist) * speed;
    pacman_y += (dy / dist) * speed;
  }

  // PROXIMITY EATING: Eat all pellets near Pac-Man's current position
  // This ensures pellets are eaten even when path jumps across the digit
  const uint8_t* pattern = digitPatterns[digit_val];
  constexpr float EAT_RADIUS = 7.0;  // Pellets within 7px get eaten (covers diagonal paths)

  for (uint8_t row = 0; row < DIGIT_GRID_H; row++) {
    for (uint8_t col = 0; col < DIGIT_GRID_W; col++) {
      // Check if this pellet exists in the digit pattern
      uint8_t pellet_bit = (pattern[row] >> (DIGIT_GRID_W - 1 - col)) & 1;
      if (!pellet_bit) continue;

      // Calculate pellet screen position
      float pellet_x = digit_base_x + col * PELLET_SPACING;
      float pellet_y = digit_base_y + row * PELLET_SPACING;

      // Calculate distance from Pac-Man to this pellet
      float pdx = pellet_x - pacman_x;
      float pdy = pellet_y - pacman_y;
      float pellet_dist = sqrt(pdx * pdx + pdy * pdy);

      // If pellet is close enough, eat it
      // ONLY eat pellets from the digit we're currently eating (prevent eating new digit's pellets)
      if (pellet_dist <= EAT_RADIUS) {
        uint8_t pellet_idx = row * DIGIT_GRID_W + col;
        uint8_t byte_idx = pellet_idx / 8;
        uint8_t bit_mask = 1 << (pellet_idx % 8);
        digitEatenPellets[digit_idx][byte_idx] |= bit_mask;
      }
    }
  }

  // ALSO eat patrol pellets during eating animation (keep patrol area clean)
  updatePellets();
}

void startEatingDigit(uint8_t digit_index, uint8_t digit_value) {
  pacman_state = PACMAN_EATING;
  current_eating_digit_index = digit_index;
  current_eating_digit_value = digit_value;

  // Clear eaten pellets for this digit
  memset(digitEatenPellets[digit_index], 0, 5);

  // Start at beginning of path
  current_path_step = 0;
  pellet_eat_distance = 0.0;

  // Position Pac-Man at first path point
  int digit_base_x = DIGIT_X_PACMAN[digit_index];
  int digit_base_y = TIME_Y_PACMAN;

  const PathStep* path = eatingPaths[digit_value];
  const PathStep first_step = path[0];

  pacman_x = digit_base_x + first_step.col * PELLET_SPACING;
  pacman_y = digit_base_y + first_step.row * PELLET_SPACING;

  // Set initial direction based on first and second path points (including diagonals)
  const PathStep second_step = path[1];
  if (second_step.col != 255) {
    float dx = (float)(second_step.col - first_step.col) * PELLET_SPACING;
    float dy = (float)(second_step.row - first_step.row) * PELLET_SPACING;
    float abs_dx = abs(dx);
    float abs_dy = abs(dy);
    float ratio = (abs_dx > abs_dy) ? (abs_dy / abs_dx) : (abs_dx / abs_dy);

    // Check for diagonal movement (ratio close to 1)
    if (ratio > 0.6) {
      if (dx > 0 && dy > 0) pacman_direction = 3;   // Down-right
      else if (dx < 0 && dy > 0) pacman_direction = 4;   // Down-left
      else if (dx < 0 && dy < 0) pacman_direction = -3;  // Up-left
      else pacman_direction = -4;  // Up-right
    } else if (abs_dx > abs_dy) {
      pacman_direction = (dx > 0) ? 1 : -1;
    } else {
      pacman_direction = (dy > 0) ? 2 : -2;
    }
  } else {
    pacman_direction = 1;
  }

  digit_being_eaten[digit_index] = true;

  // Eat the first pellet immediately
  uint8_t pellet_idx = first_step.row * DIGIT_GRID_W + first_step.col;
  uint8_t byte_idx = pellet_idx / 8;
  uint8_t bit_mask = 1 << (pellet_idx % 8);
  digitEatenPellets[digit_index][byte_idx] |= bit_mask;
}

void finishEatingDigit() {
  uint8_t digit_idx = current_eating_digit_index;

  digit_being_eaten[digit_idx] = false;

  // Store pending digit update (deferred until Pac-Man returns to patrol)
  pending_digit_index = digit_idx;
  pending_digit_value = target_digit_new_values[target_queue_index];

  // Move to next digit in queue
  target_queue_index++;

  // Return to patrol
  pacman_state = PACMAN_RETURNING;
}

void generatePellets() {
  num_pellets = settings.pacmanPelletCount;

  if (num_pellets == 0) {
    return;
  }

  if (settings.pacmanPelletRandomSpacing) {
    // Random positions with minimum spacing
    for (int i = 0; i < num_pellets; i++) {
      int attempts = 0;
      do {
        patrol_pellets[i].x = random(15, SCREEN_WIDTH - 15);

        // Check minimum spacing from other pellets
        bool too_close = false;
        for (int j = 0; j < i; j++) {
          if (abs(patrol_pellets[i].x - patrol_pellets[j].x) < 8) {
            too_close = true;
            break;
          }
        }

        if (!too_close) break;
        attempts++;
      } while (attempts < 10);

      patrol_pellets[i].active = true;
    }
  } else {
    // Even spacing
    int spacing = (SCREEN_WIDTH - 30) / (num_pellets + 1);
    for (int i = 0; i < num_pellets; i++) {
      patrol_pellets[i].x = 15 + spacing * (i + 1);
      patrol_pellets[i].active = true;
    }
  }
}

void updatePellets() {
  for (int i = 0; i < num_pellets; i++) {
    if (patrol_pellets[i].active) {
      // Check if Pac-Man ate this pellet
      if (abs((int)pacman_x - patrol_pellets[i].x) < 5) {
        patrol_pellets[i].active = false;
      }
    }
  }

  // Check if all eaten
  bool all_eaten = true;
  for (int i = 0; i < num_pellets; i++) {
    if (patrol_pellets[i].active) {
      all_eaten = false;
      break;
    }
  }

  if (all_eaten && num_pellets > 0) {
    generatePellets();
  }
}

void drawPellets() {
  for (int i = 0; i < num_pellets; i++) {
    if (patrol_pellets[i].active) {
      display.fillCircle(patrol_pellets[i].x, PACMAN_PATROL_Y, 1, DISPLAY_WHITE);
    }
  }
}

void drawPacman(int x, int y, int direction, int mouthFrame) {
  if (x < -10 || x > SCREEN_WIDTH + 10) return;
  if (y < -10 || y > SCREEN_HEIGHT + 10) return;

  // Draw Pac-Man as 8x8 circle with mouth cutout
  display.fillCircle(x, y, 4, DISPLAY_WHITE);

  if (mouthFrame > 0) {
    // Draw mouth (wedge cutout)
    int mouth_size = mouthFrame + 2;  // 2-5 pixels for better visibility

    if (direction == 1) {
      // Facing right - mouth opens to the right
      display.fillTriangle(x + 1, y, x + 5, y - mouth_size, x + 5, y + mouth_size, DISPLAY_BLACK);
    } else if (direction == -1) {
      // Facing left - mouth opens to the left
      display.fillTriangle(x - 1, y, x - 5, y - mouth_size, x - 5, y + mouth_size, DISPLAY_BLACK);
    } else if (direction == 2) {
      // Facing down - mouth opens downward
      display.fillTriangle(x, y + 1, x - mouth_size, y + 5, x + mouth_size, y + 5, DISPLAY_BLACK);
    } else if (direction == -2) {
      // Facing up - mouth opens upward
      display.fillTriangle(x, y - 1, x - mouth_size, y - 5, x + mouth_size, y - 5, DISPLAY_BLACK);
    } else if (direction == 3) {
      // Diagonal down-right (45°) - mouth points down-right
      display.fillTriangle(x, y, x + 4, y + 4, x + mouth_size, y + mouth_size, DISPLAY_BLACK);
    } else if (direction == -3) {
      // Diagonal up-left (225°) - mouth points up-left
      display.fillTriangle(x, y, x - 4, y - 4, x - mouth_size, y - mouth_size, DISPLAY_BLACK);
    } else if (direction == 4) {
      // Diagonal down-left (135°) - mouth points down-left
      display.fillTriangle(x, y, x - 4, y + 4, x - mouth_size, y + mouth_size, DISPLAY_BLACK);
    } else if (direction == -4) {
      // Diagonal up-right (315°) - mouth points up-right
      display.fillTriangle(x, y, x + 4, y - 4, x + mouth_size, y - mouth_size, DISPLAY_BLACK);
    }
  }

  // Eye position adjusts based on direction (always on the "back" side, Pac-Man style)
  if (direction == 1) {       // Right
    display.drawPixel(x - 1, y - 2, DISPLAY_BLACK);
  } else if (direction == -1) { // Left
    display.drawPixel(x + 1, y - 2, DISPLAY_BLACK);
  } else if (direction == 2) {  // Down
    display.drawPixel(x, y - 3, DISPLAY_BLACK);
  } else if (direction == -2) { // Up
    display.drawPixel(x, y + 1, DISPLAY_BLACK);
  } else if (direction == 3) {  // Down-right
    display.drawPixel(x - 2, y - 2, DISPLAY_BLACK);
  } else if (direction == -3) { // Up-left
    display.drawPixel(x + 2, y + 2, DISPLAY_BLACK);
  } else if (direction == 4) {  // Down-left
    display.drawPixel(x + 2, y - 2, DISPLAY_BLACK);
  } else if (direction == -4) { // Up-right
    display.drawPixel(x - 2, y + 2, DISPLAY_BLACK);
  } else {
    display.drawPixel(x, y - 2, DISPLAY_BLACK);  // Default
  }
}

void updateSpecificDigit(uint8_t digitIndex, uint8_t newValue) {
  // Update the specific digit in displayed_hour or displayed_min
  // digitIndex corresponds to DIGIT_X array: 0=hour tens, 1=hour ones, 3=min tens, 4=min ones
  int hour_tens = displayed_hour / 10;
  int hour_ones = displayed_hour % 10;
  int min_tens = displayed_min / 10;
  int min_ones = displayed_min % 10;

  if (digitIndex == 0) {
    hour_tens = newValue;
    displayed_hour = hour_tens * 10 + hour_ones;
  } else if (digitIndex == 1) {
    hour_ones = newValue;
    displayed_hour = hour_tens * 10 + hour_ones;
  } else if (digitIndex == 3) {
    min_tens = newValue;
    displayed_min = min_tens * 10 + min_ones;
  } else if (digitIndex == 4) {
    min_ones = newValue;
    displayed_min = min_tens * 10 + min_ones;
  }

  time_overridden = true;
  time_override_start = millis();
}
