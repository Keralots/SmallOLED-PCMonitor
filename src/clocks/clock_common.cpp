/*
 * SmallOLED-PCMonitor - Common Clock Helpers
 *
 * Shared helper functions used by multiple clock implementations.
 */

#include "clocks.h"
#include "../display/display.h"

// ========== Colon Blink Helper ==========
bool shouldShowColon() {
  if (settings.colonBlinkMode == 0) {
    return true;  // Always on
  } else if (settings.colonBlinkMode == 2) {
    return false;  // Always off
  } else {
    // Blink mode - calculate blink state based on rate
    // colonBlinkRate is in tenths of Hz (10 = 1Hz, 20 = 2Hz, 5 = 0.5Hz)
    float hz = settings.colonBlinkRate / 10.0;
    unsigned long period_ms = (unsigned long)(1000.0 / hz);
    return (millis() % period_ms) < (period_ms / 2);  // 50% duty cycle
  }
}

// ========== Digit Bounce Animation ==========
void triggerDigitBounce(int digitIndex) {
  if (digitIndex >= 0 && digitIndex < 5) {
    // Use user-configured bounce height (stored as tenths, convert to float, negate for upward velocity)
    digit_velocity[digitIndex] = -(settings.marioBounceHeight / 10.0);
  }
}

// Gravity-based bounce for Mario clock (delta-time independent physics)
void updateDigitBounce() {
  static unsigned long lastPhysicsUpdate = 0;
  unsigned long currentTime = millis();

  // Calculate delta time in seconds (time since last physics update)
  float deltaTime = (currentTime - lastPhysicsUpdate) / 1000.0;

  // Cap delta time to prevent huge jumps on first call or after long pauses
  if (deltaTime > 0.1 || lastPhysicsUpdate == 0) {
    deltaTime = 0.025;  // Default to 25ms (40 Hz) for first frame
  }

  lastPhysicsUpdate = currentTime;

  // Target physics rate: 50ms (20 FPS) for consistent behavior
  // Scale physics to match original 50ms timing
  float physicsScale = deltaTime / 0.05;  // Normalize to 50ms reference frame

  for (int i = 0; i < 5; i++) {
    if (digit_offset_y[i] != 0 || digit_velocity[i] != 0) {
      // Use user-configured fall speed (stored as tenths, convert to float)
      // Scale by physicsScale to maintain consistent speed regardless of refresh rate
      digit_velocity[i] += (settings.marioBounceSpeed / 10.0) * physicsScale;
      digit_offset_y[i] += digit_velocity[i] * physicsScale;

      if (digit_offset_y[i] >= 0) {
        digit_offset_y[i] = 0;
        digit_velocity[i] = 0;
      }
    }
  }
}

// ========== Calculate Target Digits for Minute Change ==========
void calculateTargetDigits(int current_hour, int current_min) {
  // Calculate the next minute
  int next_min = (current_min + 1) % 60;
  int next_hour = current_hour;
  if (next_min == 0) {
    next_hour = (current_hour + 1) % 24;
  }

  // Compare digits and build target list
  num_targets = 0;

  // Hour tens (position 0) - add +7 to center Mario/Space on digit
  if ((current_hour / 10) != (next_hour / 10)) {
    target_x_positions[num_targets] = DIGIT_X[0] + 7;
    target_digit_index[num_targets] = 0;
    target_digit_values[num_targets] = next_hour / 10;
    num_targets++;
  }

  // Hour ones (position 1) - add +7 to center Mario/Space on digit
  if ((current_hour % 10) != (next_hour % 10)) {
    target_x_positions[num_targets] = DIGIT_X[1] + 7;
    target_digit_index[num_targets] = 1;
    target_digit_values[num_targets] = next_hour % 10;
    num_targets++;
  }

  // Minute tens (position 3 - skip colon at 2) - add +7 to center Mario/Space on digit
  if ((current_min / 10) != (next_min / 10)) {
    target_x_positions[num_targets] = DIGIT_X[3] + 7;
    target_digit_index[num_targets] = 3;
    target_digit_values[num_targets] = next_min / 10;
    num_targets++;
  }

  // Minute ones (position 4) - add +7 to center Mario/Space on digit
  if ((current_min % 10) != (next_min % 10)) {
    target_x_positions[num_targets] = DIGIT_X[4] + 7;
    target_digit_index[num_targets] = 4;
    target_digit_values[num_targets] = next_min % 10;
    num_targets++;
  }
}

// ========== Standard Clock Display ==========
void displayStandardClock() {
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

  // Time display
  display.setTextSize(3);
  char timeStr[9];

  int displayHour = timeinfo.tm_hour;
  bool isPM = false;

  if (!settings.use24Hour) {
    isPM = displayHour >= 12;
    displayHour = displayHour % 12;
    if (displayHour == 0) displayHour = 12;
  }

  // Use blinking colon based on settings
  char separator = shouldShowColon() ? ':' : ' ';
  sprintf(timeStr, "%02d%c%02d", displayHour, separator, timeinfo.tm_min);

  // Center time
  int time_width = 5 * 18;  // 5 chars * 18px
  int time_x = (SCREEN_WIDTH - time_width) / 2;
  display.setCursor(time_x, 8);
  display.print(timeStr);

  // AM/PM indicator for 12-hour format
  if (!settings.use24Hour) {
    display.setTextSize(1);
    display.setCursor(110, 8);
    display.print(isPM ? "PM" : "AM");
  }

  // Date display
  display.setTextSize(1);
  char dateStr[12];

  switch (settings.dateFormat) {
    case 0:  // DD/MM/YYYY
      sprintf(dateStr, "%02d/%02d/%04d", timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900);
      break;
    case 1:  // MM/DD/YYYY
      sprintf(dateStr, "%02d/%02d/%04d", timeinfo.tm_mon + 1, timeinfo.tm_mday, timeinfo.tm_year + 1900);
      break;
    case 2:  // YYYY-MM-DD
      sprintf(dateStr, "%04d-%02d-%02d", timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday);
      break;
  }

  int date_x = (SCREEN_WIDTH - 60) / 2;
  display.setCursor(date_x, 38);
  display.print(dateStr);

  // Day of week
  const char* days[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
  const char* dayName = days[timeinfo.tm_wday];
  int day_width = strlen(dayName) * 6;
  int day_x = (SCREEN_WIDTH - day_width) / 2;
  display.setCursor(day_x, 52);
  display.print(dayName);
}

// ========== Large Clock Display ==========
void displayLargeClock() {
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

  int displayHour = timeinfo.tm_hour;
  bool isPM = false;

  if (!settings.use24Hour) {
    isPM = displayHour >= 12;
    displayHour = displayHour % 12;
    if (displayHour == 0) displayHour = 12;
  }

  // Large time display - size 4 (24px per char)
  display.setTextSize(4);
  char timeStr[6];
  // Use blinking colon based on settings
  char separator = shouldShowColon() ? ':' : ' ';
  sprintf(timeStr, "%02d%c%02d", displayHour, separator, timeinfo.tm_min);

  // Center time: 5 chars * 24px = 120px, centered in 128px
  int time_x = (SCREEN_WIDTH - 120) / 2;
  display.setCursor(time_x, 4);
  display.print(timeStr);

  // AM/PM indicator for 12-hour format
  if (!settings.use24Hour) {
    display.setTextSize(1);
    display.setCursor(116, 4);
    display.print(isPM ? "PM" : "AM");
  }

  // Date at bottom
  display.setTextSize(1);
  char dateStr[12];

  switch (settings.dateFormat) {
    case 0:  // DD/MM/YYYY
      sprintf(dateStr, "%02d/%02d/%04d", timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900);
      break;
    case 1:  // MM/DD/YYYY
      sprintf(dateStr, "%02d/%02d/%04d", timeinfo.tm_mon + 1, timeinfo.tm_mday, timeinfo.tm_year + 1900);
      break;
    case 2:  // YYYY-MM-DD
      sprintf(dateStr, "%04d-%02d-%02d", timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday);
      break;
  }

  int date_x = (SCREEN_WIDTH - 60) / 2;
  display.setCursor(date_x, 54);
  display.print(dateStr);
}
