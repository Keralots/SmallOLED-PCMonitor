/*
 * SmallOLED-PCMonitor - Animation Detection
 *
 * Detects if any clock animation is currently active.
 * Used for adaptive refresh rate boosting.
 */

#include "clocks.h"
#include "clock_globals.h"

// Detects if any animation is currently active for refresh rate boosting
bool isAnimationActive() {
  // Only check for animations in clock mode (when offline)
  if (metricData.online) {
    return false; // No animations in metrics mode
  }

  // Check Mario clock animations (clockStyle == 0)
  if (settings.clockStyle == 0) {
    // Check if Mario is in any active state (walking, jumping, walking off)
    if (mario_state == MARIO_WALKING || mario_state == MARIO_JUMPING || mario_state == MARIO_WALKING_OFF) {
      return true;
    }
    // Check if any digit is bouncing (digit_offset_y != 0)
    for (int i = 0; i < 5; i++) {
      if (digit_offset_y[i] != 0.0) {
        return true;
      }
    }
  }

  // Check Space clock animations (clockStyle == 3 or 4)
  if (settings.clockStyle == 3 || settings.clockStyle == 4) {
    // Space clock is always animating (patrol, shooting, or exploding)
    return true;
  }

  // Check Pong clock animations (clockStyle == 5)
  if (settings.clockStyle == 5) {
    // Pong is always active (ball moving, paddles tracking)
    return true;
  }

  // Check Pac-Man clock animations (clockStyle == 6)
  if (settings.clockStyle == 6) {
    // Always active - Pac-Man is constantly moving and eating pellets
    return true;
  }

  // Standard and Large clocks (clockStyle 1 & 2) have no animations
  return false;
}
