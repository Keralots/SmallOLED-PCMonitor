/*
 * SmallOLED-PCMonitor - Metrics Display Module
 *
 * Functions for displaying PC stats metrics on the OLED display.
 */

#include "metrics.h"
#include "../display/display.h"
#include "../utils/utils.h"
#include <WiFi.h>

// ========== Metrics Display Functions ==========

void displayStatsCompactGrid() {
  display.setTextSize(1);

  const int COL1_X = 0;
  const int COL2_X = 62;  // Moved 2px left to give right column more space

  // Dynamic row configuration based on user settings
  int startY;
  int ROW_HEIGHT;
  const int MAX_ROWS = (settings.displayRowMode == 0) ? 5 : 6;

  if (settings.displayRowMode == 0) {  // 5-row mode - optimized spacing
    startY = 0;  // Start at very top to maximize space
    // Use 13px spacing for maximum readability, except with centered clock (11px to fit)
    ROW_HEIGHT = (settings.showClock && settings.clockPosition == 0) ? 11 : 13;
  } else {  // 6-row mode - compact layout
    startY = 2;
    ROW_HEIGHT = 10;
  }

  // Clock positioning: 0=Center, 1=Left, 2=Right
  if (settings.showClock) {
    if (settings.clockPosition == 0) {
      // Center - Clock at top center, metrics below
      display.setCursor(48 + settings.clockOffset, startY);
      display.print(metricData.timestamp);
      startY += 10;  // Clock height (8px) + 2px gap
    } else if (settings.clockPosition == 1) {
      // Clock in left column, first row
      display.setCursor(COL1_X + settings.clockOffset, startY);
      display.print(metricData.timestamp);
    } else if (settings.clockPosition == 2) {
      // Clock in right column, first row
      display.setCursor(COL2_X + settings.clockOffset, startY);
      display.print(metricData.timestamp);
    }
  }

  // Render 6 rows using position-based system
  int visibleCount = 0;

  for (int row = 0; row < MAX_ROWS; row++) {
    int y = startY + (row * ROW_HEIGHT);

    // Check for overflow
    if (y + 8 > 64) break;

    // Calculate position indices for this row
    uint8_t leftPos = row * 2;      // 0, 2, 4, 6, 8, 10
    uint8_t rightPos = row * 2 + 1; // 1, 3, 5, 7, 9, 11

    // Skip first row left if clock is positioned there
    bool clockInLeft = (settings.showClock && settings.clockPosition == 1 && row == 0);
    bool clockInRight = (settings.showClock && settings.clockPosition == 2 && row == 0);

    // Find and render left column (check for bar first, then text)
    if (!clockInLeft) {
      bool rendered = false;

      // First check if any metric wants to display a bar at this position
      for (int i = 0; i < metricData.count; i++) {
        Metric& m = metricData.metrics[i];
        if (m.barPosition == leftPos) {
          drawProgressBar(COL1_X, y, 60, &m);  // Full-size bar for left column
          visibleCount++;
          rendered = true;
          break;
        }
      }

      // If no bar, check for text metric at this position
      if (!rendered) {
        for (int i = 0; i < metricData.count; i++) {
          Metric& m = metricData.metrics[i];
          if (m.position == leftPos) {
            display.setCursor(COL1_X, y);
            displayMetricCompact(&m);
            visibleCount++;
            break;
          }
        }
      }
    }

    // Find and render right column (check for bar first, then text)
    if (!clockInRight) {
      bool rendered = false;

      // First check if any metric wants to display a bar at this position
      for (int i = 0; i < metricData.count; i++) {
        Metric& m = metricData.metrics[i];
        if (m.barPosition == rightPos) {
          drawProgressBar(COL2_X, y, 64, &m);  // Full-size bar for right column
          visibleCount++;
          rendered = true;
          break;
        }
      }

      // If no bar, check for text metric at this position
      if (!rendered) {
        for (int i = 0; i < metricData.count; i++) {
          Metric& m = metricData.metrics[i];
          if (m.position == rightPos) {
            display.setCursor(COL2_X, y);
            displayMetricCompact(&m);
            visibleCount++;
            break;
          }
        }
      }
    }
  }

  // No metrics edge case
  if (visibleCount == 0) {
    display.setTextSize(1);
    display.setCursor(0, 10);
    display.print("Go to:");
    display.setCursor(0, 22);
    display.print(WiFi.localIP().toString().c_str());
    display.setCursor(0, 34);
    display.print("to configure");
    display.setCursor(0, 46);
    display.print("metrics");
  }
}

// Helper function to display a metric in compact format
void displayMetricCompact(Metric* m) {
  // Process label: convert '^' to spaces, strip trailing '%', move trailing spaces to after colon
  char displayLabel[METRIC_NAME_LEN];
  strncpy(displayLabel, m->label, METRIC_NAME_LEN - 1);
  displayLabel[METRIC_NAME_LEN - 1] = '\0';

  // Convert '^' to spaces for custom alignment
  convertCaretToSpaces(displayLabel);

  // Count and remove trailing spaces (to be added after colon)
  int labelLen = strlen(displayLabel);
  int trailingSpaces = 0;
  while (labelLen > 0 && displayLabel[labelLen - 1] == ' ') {
    trailingSpaces++;
    displayLabel[labelLen - 1] = '\0';
    labelLen--;
  }

  // Strip trailing '%' if present (after removing spaces)
  if (labelLen > 0 && displayLabel[labelLen - 1] == '%') {
    displayLabel[labelLen - 1] = '\0';
  }

  // Format: "LABEL: VAL" with spaces after colon if needed
  char text[40];
  char spaces[11] = "";  // Max 10 spaces
  for (int i = 0; i < trailingSpaces && i < 10; i++) {
    spaces[i] = ' ';
    spaces[i + 1] = '\0';
  }

  if (settings.useRpmKFormat && strcmp(m->unit, "RPM") == 0 && m->value >= 1000) {
    // RPM with K suffix: "FAN1: 1.2K"
    snprintf(text, 40, "%s:%s%.1fK", displayLabel, spaces, m->value / 1000.0);
  } else if (strcmp(m->unit, "KB/s") == 0) {
    // Network throughput - value is multiplied by 10 from Python for decimal precision
    // Divide by 10 to get actual value, then format appropriately
    float actualValue = m->value / 10.0;
    if (settings.useNetworkMBFormat) {
      // M suffix: "DL: 1.2M" (value in MB/s)
      snprintf(text, 40, "%s:%s%.1fM", displayLabel, spaces, actualValue / 1000.0);
    } else {
      // Show with 1 decimal: "DL: 1.5KB/s"
      snprintf(text, 40, "%s:%s%.1f%s", displayLabel, spaces, actualValue, m->unit);
    }
  } else {
    // Normal: "CPU: 45%" or "FAN1: 1800RPM"
    snprintf(text, 40, "%s:%s%d%s", displayLabel, spaces, m->value, m->unit);
  }

  // Check for companion metric (append to same line)
  if (m->companionId > 0) {
    // Find companion metric by ID
    for (int c = 0; c < metricData.count; c++) {
      if (metricData.metrics[c].id == m->companionId) {
        Metric& companion = metricData.metrics[c];
        char companionText[20];
        // Handle KB/s throughput values (multiplied by 10 from Python)
        if (strcmp(companion.unit, "KB/s") == 0) {
          float compValue = companion.value / 10.0;
          if (settings.useNetworkMBFormat) {
            // M suffix for companion too: " 1.2M"
            snprintf(companionText, 20, " %.1fM", compValue / 1000.0);
          } else {
            snprintf(companionText, 20, " %.1f%s", compValue, companion.unit);
          }
        } else {
          snprintf(companionText, 20, " %d%s", companion.value, companion.unit);
        }
        strncat(text, companionText, 40 - strlen(text) - 1);
        break;
      }
    }
  }

  display.print(text);
}

// Helper function to draw a full-size progress bar (occupies entire position slot)
void drawProgressBar(int x, int y, int width, Metric* m) {
  // Apply custom width and offset
  int actualX = x + m->barOffsetX;
  int actualWidth = m->barWidth;

  // Constrain to ensure bar doesn't exceed screen boundaries
  if (actualX + actualWidth > 128) {
    actualWidth = 128 - actualX;
  }

  // Calculate bar fill percentage based on min/max values
  int range = m->barMax - m->barMin;
  if (range <= 0) range = 100;  // Avoid division by zero

  // For KB/s throughput: value is x10, but barMin/barMax are normal
  // So divide value by 10 for proper bar display
  int displayValue = m->value;
  if (strcmp(m->unit, "KB/s") == 0) {
    displayValue = m->value / 10;
  }

  int valueInRange = constrain(displayValue, m->barMin, m->barMax) - m->barMin;
  int fillWidth = map(valueInRange, 0, range, 0, actualWidth - 2);

  // Draw bar outline (8px tall, full row height)
  display.drawRect(actualX, y, actualWidth, 8, DISPLAY_WHITE);

  // Fill bar based on value
  if (fillWidth > 0) {
    display.fillRect(actualX + 1, y + 1, fillWidth, 6, DISPLAY_WHITE);
  }
}

// Main display function - always uses compact grid layout
void displayStats() {
  displayStatsCompactGrid();   // Compact 2-column grid layout
}
