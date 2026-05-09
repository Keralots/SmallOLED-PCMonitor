/*
 * SmallOLED-PCMonitor - Common Clock Helpers
 *
 * Shared helper functions used by multiple clock implementations.
 */

#include "clocks.h"
#include "clock_globals.h"
#include "../display/display.h"

static void advanceDisplayTimeState(int& hour, int& minute, bool& isPM) {
  minute++;
  if (minute < 60) {
    return;
  }

  minute = 0;

  if (settings.use24Hour) {
    hour = (hour + 1) % 24;
    return;
  }

  if (hour == 11) {
    hour = 12;
    isPM = !isPM;
  } else if (hour == 12) {
    hour = 1;
  } else {
    hour++;
  }
}

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

// ========== Time Formatting Helpers ==========
void formatTimeForDisplay(int hour24, int minute, int& displayHour,
                          int& displayMin, bool& isPM) {
  displayHour = hour24;
  displayMin = minute;
  isPM = hour24 >= 12;

  if (!settings.use24Hour) {
    displayHour = hour24 % 12;
    if (displayHour == 0) {
      displayHour = 12;
    }
  }
}

void syncDisplayedTime(const struct tm* timeinfo) {
  formatTimeForDisplay(timeinfo->tm_hour, timeinfo->tm_min, displayed_hour,
                       displayed_min, displayed_is_pm);
}

void advanceDisplayedTimeOneMinute() {
  advanceDisplayTimeState(displayed_hour, displayed_min, displayed_is_pm);
}

bool displayedTimeMatches(const struct tm* timeinfo) {
  int realHour = 0;
  int realMin = 0;
  bool realIsPM = false;
  formatTimeForDisplay(timeinfo->tm_hour, timeinfo->tm_min, realHour, realMin,
                       realIsPM);
  return displayed_hour == realHour && displayed_min == realMin &&
         displayed_is_pm == realIsPM;
}

uint8_t getDisplayedDigitValue(uint8_t digitIndex) {
  switch (digitIndex) {
    case 0:
      return displayed_hour / 10;
    case 1:
      return displayed_hour % 10;
    case 3:
      return displayed_min / 10;
    case 4:
      return displayed_min % 10;
    default:
      return 0;
  }
}

void updateDisplayedTimeDigit(uint8_t digitIndex, uint8_t newValue) {
  int oldHour = displayed_hour;

  int hourTens = displayed_hour / 10;
  int hourOnes = displayed_hour % 10;
  int minTens = displayed_min / 10;
  int minOnes = displayed_min % 10;

  if (digitIndex == 0) {
    hourTens = newValue;
    displayed_hour = hourTens * 10 + hourOnes;
  } else if (digitIndex == 1) {
    hourOnes = newValue;
    displayed_hour = hourTens * 10 + hourOnes;
  } else if (digitIndex == 3) {
    minTens = newValue;
    displayed_min = minTens * 10 + minOnes;
  } else if (digitIndex == 4) {
    minOnes = newValue;
    displayed_min = minTens * 10 + minOnes;
  }

  if (!settings.use24Hour && oldHour == 11 && displayed_hour == 12) {
    displayed_is_pm = !displayed_is_pm;
  }

  if (!time_overridden) {
    time_override_start = millis();
  }
  time_overridden = true;
}

// Centralized time-override maintenance. Called once per frame from each
// animated clock's display function. Clears the override either after
// TIME_OVERRIDE_MAX_MS (absolute cap from the start of the override) or
// when the caller is animation-idle and the cached time matches NTP.
// Force-syncs displayed time on timeout-driven clears so a stuck animation
// can never leave displayed_hour/displayed_min behind reality.
void maintainTimeOverride(const struct tm* timeinfo, bool animationIdle) {
  if (!time_overridden) {
    return;
  }
  bool ntp_matches = animationIdle && displayedTimeMatches(timeinfo);
  bool timeout_expired = (millis() - time_override_start > TIME_OVERRIDE_MAX_MS);
  if (ntp_matches || timeout_expired) {
    time_overridden = false;
    if (timeout_expired && !ntp_matches) {
      syncDisplayedTime(timeinfo);
    }
  }
}

void drawMeridiemIndicator(int x, int y, bool isPM) {
  if (settings.use24Hour) {
    return;
  }

  display.setTextSize(1);
  display.setCursor(x, y);
  display.print(isPM ? "PM" : "AM");
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
void calculateTargetDigits(int current_hour, int current_min, bool current_is_pm) {
  int next_hour = current_hour;
  int next_min = current_min;
  bool next_is_pm = current_is_pm;
  advanceDisplayTimeState(next_hour, next_min, next_is_pm);

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

  int displayHour = 0;
  int displayMin = 0;
  bool isPM = false;
  formatTimeForDisplay(timeinfo.tm_hour, timeinfo.tm_min, displayHour,
                       displayMin, isPM);

  // Use blinking colon based on settings
  char separator = shouldShowColon() ? ':' : ' ';
  sprintf(timeStr, "%02d%c%02d", displayHour, separator, displayMin);

  // Center time
  int time_width = 5 * 18;  // 5 chars * 18px
  int time_x = (SCREEN_WIDTH - time_width) / 2;
  display.setCursor(time_x, 8);
  display.print(timeStr);

  // AM/PM indicator for 12-hour format
  drawMeridiemIndicator(110, 8, isPM);

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
    case 3:  // DD.MM.YYYY
      sprintf(dateStr, "%02d.%02d.%04d", timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900);
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

  // Draw no-WiFi icon if disconnected
  if (!wifiConnected) {
    drawNoWiFiIcon(0, 0);
  }
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

  int displayHour = 0;
  int displayMin = 0;
  bool isPM = false;
  formatTimeForDisplay(timeinfo.tm_hour, timeinfo.tm_min, displayHour,
                       displayMin, isPM);

  // Large time display - size 4 (24px per char)
  display.setTextSize(4);
  char timeStr[6];
  // Use blinking colon based on settings
  char separator = shouldShowColon() ? ':' : ' ';
  sprintf(timeStr, "%02d%c%02d", displayHour, separator, displayMin);

  // Center time: 5 chars * 24px = 120px, centered in 128px
  int time_x = (SCREEN_WIDTH - 120) / 2;
  display.setCursor(time_x, 4);
  display.print(timeStr);

  // AM/PM indicator for 12-hour format lives in the bottom-right corner here,
  // so it does not collide with the oversized minute digits.
  drawMeridiemIndicator(110, 54, isPM);

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
    case 3:  // DD.MM.YYYY
      sprintf(dateStr, "%02d.%02d.%04d", timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900);
      break;
  }

  int date_x = (SCREEN_WIDTH - 60) / 2;
  display.setCursor(date_x, 54);
  display.print(dateStr);

  // Draw no-WiFi icon if disconnected
  if (!wifiConnected) {
    drawNoWiFiIcon(0, 0);
  }
}
