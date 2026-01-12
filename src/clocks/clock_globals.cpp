/*
 * SmallOLED-PCMonitor - Clock Global State Definitions
 *
 * Definitions for all clock-specific global variables.
 * These variables are used across multiple clock implementations.
 */

#include "clock_globals.h"
#include "clock_constants.h"

// ========== Common Digit Positioning ==========
// Standard digit X positions for time display (18px spacing, starting at 19)
const int DIGIT_X[5] = {19, 37, 55, 73, 91};

// Progressive fragmentation: spawn 25%, then 50%, then 25%
const float FRAGMENT_SPAWN_PERCENT[3] = {0.25, 0.50, 0.25};

// ========== Mario Clock Globals ==========
MarioState mario_state = MARIO_IDLE;
float mario_x = MARIO_START_X;
float mario_jump_y = 0.0;
float jump_velocity = 0.0;
int mario_base_y = 62;
bool mario_facing_right = true;
int mario_walk_frame = 0;
unsigned long last_mario_update = 0;

// Time display state
int displayed_hour = 0;
int displayed_min = 0;
bool time_overridden = false;

// Animation control
int last_minute = -1;
bool animation_triggered = false;
bool digit_bounce_triggered = false;

// Target tracking for digit changes
int num_targets = 0;
int target_x_positions[4] = {0};
int target_digit_index[4] = {0};
int target_digit_values[4] = {0};
int current_target_index = 0;

// Digit bounce animation state
float digit_offset_y[5] = {0};
float digit_velocity[5] = {0};

// ========== Space Clock Globals ==========
SpaceState space_state = SPACE_PATROL;
float space_x = SCREEN_CENTER_X;
const float space_y = 56;  // Fixed Y position at bottom
int space_anim_frame = 0;
int space_patrol_direction = 1;
unsigned long last_space_update = 0;
unsigned long last_space_sprite_toggle = 0;

// Laser and explosions
Laser space_laser = {0, 0, 0, false, -1};
SpaceFragment space_fragments[MAX_SPACE_FRAGMENTS];
int space_explosion_timer = 0;

// ========== Pong Clock Globals ==========
PongBall pong_balls[MAX_PONG_BALLS];
SpaceFragment pong_fragments[MAX_PONG_FRAGMENTS];
FragmentTarget fragment_targets[MAX_PONG_FRAGMENTS];
DigitTransition digit_transitions[5];
BreakoutPaddle breakout_paddle = {SCREEN_CENTER_X, 20, SCREEN_CENTER_X, 3};
unsigned long last_pong_update = 0;

// Ball state
bool ball_stuck_to_paddle[MAX_PONG_BALLS] = {false};
unsigned long ball_stick_release_time[MAX_PONG_BALLS] = {0};
int ball_stuck_x_offset[MAX_PONG_BALLS] = {0};
int paddle_last_x = SCREEN_CENTER_X;

// ========== Pac-Man Clock Globals ==========
PacmanState pacman_state = PACMAN_PATROL;
float pacman_x = 30.0;
float pacman_y = 56.0;  // Bottom patrol line
int pacman_direction = 1;
int pacman_mouth_frame = 0;
unsigned long last_pacman_update = 0;
unsigned long last_pacman_mouth_toggle = 0;

// Animation control
int last_minute_pacman = -1;
bool pacman_animation_triggered = false;

// Digit eating state
bool digit_being_eaten[5] = {false};
int digit_eaten_rows_left[5] = {0};
int digit_eaten_rows_right[5] = {0};

// Patrol pellets
PatrolPellet patrol_pellets[MAX_PATROL_PELLETS];
int num_pellets = 0;

// Eating path tracking
uint8_t digitEatenPellets[5][5] = {{0}};
uint8_t current_eating_digit_index = 0;
uint8_t current_eating_digit_value = 0;
uint8_t current_path_step = 0;
float pellet_eat_distance = 0.0;

// Target digit queue
uint8_t target_digit_queue[4] = {0};
uint8_t target_digit_new_values[4] = {0};
uint8_t target_queue_length = 0;
uint8_t target_queue_index = 0;
uint8_t pending_digit_index = 255;
uint8_t pending_digit_value = 0;
