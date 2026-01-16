/*
 * SmallOLED-PCMonitor - Clock Global State
 *
 * All global variables for clock animations.
 * This header declares the clock-specific state used by each clock implementation.
 */

#ifndef CLOCK_GLOBALS_H
#define CLOCK_GLOBALS_H

#include "../config/config.h"

// ========== Common Digit Positioning ==========
// Standard digit X positions for time display (18px spacing)
extern const int DIGIT_X[5];

// Progressive fragmentation spawn percentages
extern const float FRAGMENT_SPAWN_PERCENT[3];

// ========== Mario Clock Globals ==========
extern MarioState mario_state;
extern float mario_x;
extern float mario_jump_y;
extern float jump_velocity;
extern int mario_base_y;
extern bool mario_facing_right;
extern int mario_walk_frame;
extern unsigned long last_mario_update;

// Time display state
extern int displayed_hour;
extern int displayed_min;
extern bool time_overridden;
extern unsigned long time_override_start;  // Track when override started for timeout

// Animation control
extern int last_minute;
extern bool animation_triggered;
extern bool digit_bounce_triggered;

// Target tracking for digit changes
extern int num_targets;
extern int target_x_positions[4];
extern int target_digit_index[4];
extern int target_digit_values[4];
extern int current_target_index;

// Digit bounce animation state
extern float digit_offset_y[5];
extern float digit_velocity[5];

// ========== Space Clock Globals ==========
extern SpaceState space_state;
extern float space_x;
extern const float space_y;
extern int space_anim_frame;
extern int space_patrol_direction;
extern unsigned long last_space_update;
extern unsigned long last_space_sprite_toggle;

// Laser and explosions
extern Laser space_laser;
extern SpaceFragment space_fragments[MAX_SPACE_FRAGMENTS];
extern int space_explosion_timer;

// ========== Pong Clock Globals ==========
extern PongBall pong_balls[MAX_PONG_BALLS];
extern SpaceFragment pong_fragments[MAX_PONG_FRAGMENTS];
extern FragmentTarget fragment_targets[MAX_PONG_FRAGMENTS];
extern DigitTransition digit_transitions[5];
extern BreakoutPaddle breakout_paddle;
extern unsigned long last_pong_update;

// Ball state
extern bool ball_stuck_to_paddle[MAX_PONG_BALLS];
extern unsigned long ball_stick_release_time[MAX_PONG_BALLS];
extern int ball_stuck_x_offset[MAX_PONG_BALLS];
extern int paddle_last_x;

// ========== Pac-Man Clock Globals ==========
extern PacmanState pacman_state;
extern float pacman_x;
extern float pacman_y;
extern int pacman_direction;
extern int pacman_mouth_frame;
extern unsigned long last_pacman_update;
extern unsigned long last_pacman_mouth_toggle;

// Animation control
extern int last_minute_pacman;
extern bool pacman_animation_triggered;

// Digit eating state
extern bool digit_being_eaten[5];
extern int digit_eaten_rows_left[5];
extern int digit_eaten_rows_right[5];

// Patrol pellets
extern PatrolPellet patrol_pellets[MAX_PATROL_PELLETS];
extern int num_pellets;

// Eating path tracking
extern uint8_t digitEatenPellets[5][5];
extern uint8_t current_eating_digit_index;
extern uint8_t current_eating_digit_value;
extern uint8_t current_path_step;
extern float pellet_eat_distance;

// Target digit queue
extern uint8_t target_digit_queue[4];
extern uint8_t target_digit_new_values[4];
extern uint8_t target_queue_length;
extern uint8_t target_queue_index;
extern uint8_t pending_digit_index;
extern uint8_t pending_digit_value;

#endif // CLOCK_GLOBALS_H
