/*
 * SmallOLED-PCMonitor - Clock Animation Constants
 *
 * Named constants for clock animation values.
 * These values are tuned for specific animation behaviors.
 */

#ifndef CLOCK_CONSTANTS_H
#define CLOCK_CONSTANTS_H

// ========== Mario Clock Constants ==========
// Starting position (off-screen left)
#define MARIO_START_X -15

// Walking speed (pixels per frame at MARIO_ANIM_SPEED)
#define MARIO_WALK_SPEED 2.5f

// Velocity after hitting a digit (bounce upward)
#define MARIO_BOUNCE_VELOCITY 2.0f

// Second trigger threshold for animation
#define MARIO_ANIMATION_TRIGGER_SECOND 55

// ========== Space Clock Constants ==========
// Laser offset from character top (where laser starts)
#define SPACE_LASER_OFFSET_Y 4

// Explosion frames before moving to next target
#define SPACE_EXPLOSION_FRAMES 5

// ========== Digit Positioning ==========
// Standard digit X positions (18px spacing, starting at 19)
#define DIGIT_SPACING_PX 18
#define DIGIT_START_X 19

// ========== Common Values ==========
// Movement threshold (considered "at target" when within this distance)
#define MOVEMENT_THRESHOLD 1.0f

// Walk direction proximity threshold (within 3 pixels = at target)
#define MARIO_TARGET_PROXIMITY 3

// Date display width calculation (for centering)
#define DATE_DISPLAY_WIDTH 60

// Screen center X position
#define SCREEN_CENTER_X 64

#endif // CLOCK_CONSTANTS_H
