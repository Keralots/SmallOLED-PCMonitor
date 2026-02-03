/*
 * SmallOLED-PCMonitor - Mario Clock Implementation
 *
 * Mario clock style with animated Mario character that jumps to change digits.
 */

#include "../config/config.h"
#include "../display/display.h"
#include "clocks.h"
#include "clock_constants.h"
#include "clock_globals.h"

// Forward declarations for helper functions used by Mario clock
void drawTimeWithBounce();
void advanceDisplayedTime();
void updateSpecificDigit(int digitIndex, int newValue);

// ========== Draw Time With Bounce Effect ==========
void drawTimeWithBounce() {
  display.setTextSize(3);

  char digits[5];
  digits[0] = '0' + (displayed_hour / 10);
  digits[1] = '0' + (displayed_hour % 10);
  digits[2] = shouldShowColon() ? ':' : ' ';  // Blinking colon
  digits[3] = '0' + (displayed_min / 10);
  digits[4] = '0' + (displayed_min % 10);

  for (int i = 0; i < 5; i++) {
    int y = TIME_Y + (int)digit_offset_y[i];
    display.setCursor(DIGIT_X[i], y);
    display.print(digits[i]);
  }
}

// ========== Advance Displayed Time ==========
void advanceDisplayedTime() {
  displayed_min++;
  if (displayed_min >= 60) {
    displayed_min = 0;
    displayed_hour++;
    if (displayed_hour >= 24) {
      displayed_hour = 0;
    }
  }
  time_overridden = true;
  time_override_start = millis();
}

// ========== Update Specific Digit ==========
void updateSpecificDigit(int digitIndex, int newValue) {
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

// ========== Display Clock With Mario ==========
void displayClockWithMario() {
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

  if (mario_state == MARIO_IDLE) {
    time_overridden = false;
  }

  if (!time_overridden) {
    displayed_hour = timeinfo.tm_hour;
    displayed_min = timeinfo.tm_min;
  }

  // Check if time override should be cleared
  if (time_overridden) {
    bool ntp_matches = (timeinfo.tm_hour == displayed_hour && timeinfo.tm_min == displayed_min);
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

  // Date at top
  display.setTextSize(1);
  char dateStr[12];

  switch (settings.dateFormat) {
    case 0:
      sprintf(dateStr, "%02d/%02d/%04d", timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900);
      break;
    case 1:
      sprintf(dateStr, "%02d/%02d/%04d", timeinfo.tm_mon + 1, timeinfo.tm_mday, timeinfo.tm_year + 1900);
      break;
    case 2:
      sprintf(dateStr, "%04d-%02d-%02d", timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday);
      break;
  }

  int date_x = (SCREEN_WIDTH - DATE_DISPLAY_WIDTH) / 2;
  display.setCursor(date_x, 4);
  display.print(dateStr);

  updateDigitBounce();
  drawTimeWithBounce();

  updateMarioAnimation(&timeinfo);

  int mario_draw_y = mario_base_y + (int)mario_jump_y;
  bool isJumping = (mario_state == MARIO_JUMPING);
  drawMario((int)mario_x, mario_draw_y, mario_facing_right, mario_walk_frame, isJumping);

  // Draw no-WiFi icon if disconnected
  if (!wifiConnected) {
    drawNoWiFiIcon(0, 0);
  }
}

// ========== Update Mario Animation ==========
void updateMarioAnimation(struct tm* timeinfo) {
  unsigned long currentMillis = millis();

  if (currentMillis - last_mario_update < MARIO_ANIM_SPEED) {
    return;
  }
  last_mario_update = currentMillis;

  int seconds = timeinfo->tm_sec;
  int current_minute = timeinfo->tm_min;

  if (current_minute != last_minute) {
    last_minute = current_minute;
    animation_triggered = false;
  }

  if (seconds >= MARIO_ANIMATION_TRIGGER_SECOND && !animation_triggered && mario_state == MARIO_IDLE) {
    animation_triggered = true;
    calculateTargetDigits(displayed_hour, displayed_min);
    if (num_targets > 0) {
      current_target_index = 0;
      mario_x = MARIO_START_X;
      mario_state = MARIO_WALKING;
      mario_facing_right = true;
      digit_bounce_triggered = false;
    }
  }

  switch (mario_state) {
    case MARIO_IDLE:
      mario_walk_frame = 0;
      mario_x = MARIO_START_X;
      break;

    case MARIO_WALKING:
      if (current_target_index < num_targets) {
        int target = target_x_positions[current_target_index];

        if (abs(mario_x - target) > MARIO_TARGET_PROXIMITY) {
          float walkSpeed = settings.marioWalkSpeed / 10.0f;
          if (mario_x < target) {
            mario_x += walkSpeed;
            mario_facing_right = true;
          } else {
            mario_x -= walkSpeed;
            mario_facing_right = false;
          }
          int frameCount = settings.marioSmoothAnimation ? 4 : 2;
          mario_walk_frame = (mario_walk_frame + 1) % frameCount;
        } else {
          mario_x = target;
          mario_state = MARIO_JUMPING;
          jump_velocity = JUMP_POWER;
          mario_jump_y = 0;
          digit_bounce_triggered = false;
        }
      } else {
        mario_state = MARIO_WALKING_OFF;
        mario_facing_right = true;
      }
      break;

    case MARIO_JUMPING:
      {
        jump_velocity += GRAVITY;
        mario_jump_y += jump_velocity;

        int mario_head_y = mario_base_y + (int)mario_jump_y - MARIO_HEAD_OFFSET;

        if (!digit_bounce_triggered && mario_head_y <= DIGIT_BOTTOM) {
          digit_bounce_triggered = true;
          triggerDigitBounce(target_digit_index[current_target_index]);

          // Update only the specific digit that Mario just hit
          updateSpecificDigit(target_digit_index[current_target_index],
                             target_digit_values[current_target_index]);

          jump_velocity = MARIO_BOUNCE_VELOCITY;
        }

        if (mario_jump_y >= 0) {
          mario_jump_y = 0;
          jump_velocity = 0;

          current_target_index++;

          if (current_target_index < num_targets) {
            mario_state = MARIO_WALKING;
            mario_facing_right = (target_x_positions[current_target_index] > mario_x);
            digit_bounce_triggered = false;
          } else {
            mario_state = MARIO_WALKING_OFF;
            mario_facing_right = true;
          }
        }
      }
      break;

    case MARIO_WALKING_OFF:
      mario_x += settings.marioWalkSpeed / 10.0f;
      {
        int frameCount = settings.marioSmoothAnimation ? 4 : 2;
        mario_walk_frame = (mario_walk_frame + 1) % frameCount;
      }

      if (mario_x > SCREEN_WIDTH + 15) {
        mario_state = MARIO_IDLE;
        mario_x = MARIO_START_X;
      }
      break;
  }
}

// ========== Draw Mario Sprite ==========
void drawMario(int x, int y, bool facingRight, int frame, bool jumping) {
  if (x < -10 || x > SCREEN_WIDTH + 10) return;

  int sx = x - 4;
  int sy = y - 10;

  if (jumping) {
    display.fillRect(sx + 2, sy, 4, 3, DISPLAY_WHITE);
    display.fillRect(sx + 2, sy + 3, 4, 3, DISPLAY_WHITE);
    display.drawPixel(sx + 1, sy + 2, DISPLAY_WHITE);
    display.drawPixel(sx + 6, sy + 2, DISPLAY_WHITE);
    display.drawPixel(sx + 0, sy + 1, DISPLAY_WHITE);
    display.drawPixel(sx + 7, sy + 1, DISPLAY_WHITE);
    display.fillRect(sx + 2, sy + 6, 2, 3, DISPLAY_WHITE);
    display.fillRect(sx + 4, sy + 6, 2, 3, DISPLAY_WHITE);
  } else {
    display.fillRect(sx + 2, sy, 4, 3, DISPLAY_WHITE);
    if (facingRight) {
      display.drawPixel(sx + 6, sy + 1, DISPLAY_WHITE);
    } else {
      display.drawPixel(sx + 1, sy + 1, DISPLAY_WHITE);
    }

    display.fillRect(sx + 2, sy + 3, 4, 3, DISPLAY_WHITE);

    if (settings.marioSmoothAnimation) {
      // 4-frame mode: both arms animate in opposite phase
      if (facingRight) {
        display.drawPixel(sx + 1, sy + 4 - (frame % 2), DISPLAY_WHITE);  // Back arm (opposite phase)
        display.drawPixel(sx + 6, sy + 3 + (frame % 2), DISPLAY_WHITE);  // Front arm
      } else {
        display.drawPixel(sx + 6, sy + 4 - (frame % 2), DISPLAY_WHITE);  // Back arm (opposite phase)
        display.drawPixel(sx + 1, sy + 3 + (frame % 2), DISPLAY_WHITE);  // Front arm
      }

      // 4-frame walk cycle for smoother animation
      switch (frame % 4) {
        case 0:  // Legs together (neutral)
          display.fillRect(sx + 2, sy + 6, 2, 3, DISPLAY_WHITE);
          display.fillRect(sx + 4, sy + 6, 2, 3, DISPLAY_WHITE);
          break;
        case 1:  // Left leg forward
          display.fillRect(sx + 1, sy + 6, 2, 3, DISPLAY_WHITE);
          display.fillRect(sx + 4, sy + 6, 2, 3, DISPLAY_WHITE);
          break;
        case 2:  // Legs apart (full stride)
          display.fillRect(sx + 1, sy + 6, 2, 3, DISPLAY_WHITE);
          display.fillRect(sx + 5, sy + 6, 2, 3, DISPLAY_WHITE);
          break;
        case 3:  // Right leg forward
          display.fillRect(sx + 2, sy + 6, 2, 3, DISPLAY_WHITE);
          display.fillRect(sx + 5, sy + 6, 2, 3, DISPLAY_WHITE);
          break;
      }
    } else {
      // 2-frame mode (original): back arm static, front arm moves
      if (facingRight) {
        display.drawPixel(sx + 1, sy + 4, DISPLAY_WHITE);
        display.drawPixel(sx + 6, sy + 3 + (frame % 2), DISPLAY_WHITE);
      } else {
        display.drawPixel(sx + 6, sy + 4, DISPLAY_WHITE);
        display.drawPixel(sx + 1, sy + 3 + (frame % 2), DISPLAY_WHITE);
      }

      // 2-frame walk cycle (original)
      if (frame == 0) {
        display.fillRect(sx + 2, sy + 6, 2, 3, DISPLAY_WHITE);
        display.fillRect(sx + 4, sy + 6, 2, 3, DISPLAY_WHITE);
      } else {
        display.fillRect(sx + 1, sy + 6, 2, 3, DISPLAY_WHITE);
        display.fillRect(sx + 5, sy + 6, 2, 3, DISPLAY_WHITE);
      }
    }
  }
}
