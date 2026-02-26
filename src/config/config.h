/*
 * SmallOLED-PCMonitor - Global Configuration
 *
 * All structs, constants, enums, and extern global declarations.
 * Include this file in any module that needs access to shared types/globals.
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>
#include "user_config.h"

// ========== Version ==========
#define FIRMWARE_VERSION "1.5.1"

// ========== Constants ==========
#define MAX_METRICS 20
#define METRIC_NAME_LEN 11
#define METRIC_UNIT_LEN 8
#define TIMEOUT STATS_TIMEOUT

// ========== Metric Structures ==========
struct Metric {
  uint8_t id;
  char name[METRIC_NAME_LEN];
  char label[METRIC_NAME_LEN];  // Custom label (may differ from name)
  char unit[METRIC_UNIT_LEN];
  int value;
  uint8_t displayOrder;
  uint8_t companionId;  // ID of companion metric (0 = none)
  uint8_t position;     // Display position (0-11 for positions, 255 = hidden)
  uint8_t barPosition;  // Progress bar position (255 = no bar)
  int barMin;           // Progress bar minimum value
  int barMax;           // Progress bar maximum value
  int barWidth;         // Progress bar width in pixels
  int barOffsetX;       // Progress bar X offset
};

// Status codes for PC monitor connection
#define STATUS_OK 1
#define STATUS_API_ERROR 2
#define STATUS_LHM_NOT_RUNNING 3
#define STATUS_LHM_STARTING 4
#define STATUS_UNKNOWN_ERROR 5

struct MetricData {
  Metric metrics[MAX_METRICS];
  uint8_t count;
  char timestamp[6];  // "HH:MM\0"
  bool online;
  uint8_t status;     // Connection status code (see STATUS_* defines)
};

// ========== Settings Structure ========== 
struct Settings {
  // Clock settings
  uint8_t clockStyle;       // 0=Mario, 1=Standard, 2=Large, 3=Space Invader, 4=Space Ship, 5=Pong, 6=Pac-Man
  int16_t gmtOffset;        // GMT offset in minutes (deprecated, kept for migration)
  bool daylightSaving;      // DST enabled (deprecated, kept for migration)
  char timezoneString[64];  // POSIX TZ string (e.g., "CET-1CEST,M3.5.0/02:00,M10.5.0/03:00")
  uint8_t timezoneIndex;    // Index into timezone database (for UI display)
  bool use24Hour;           // 24-hour format
  uint8_t dateFormat;       // 0=DD/MM/YYYY, 1=MM/DD/YYYY, 2=YYYY-MM-DD
  uint8_t clockPosition;    // 0=Center, 1=Left, 2=Right
  int8_t clockOffset;       // Fine-tune clock position (-10 to +10)
  bool showClock;           // Show clock in metrics mode

  // Display settings
  uint8_t displayType;      // 0=SSD1306, 1=SH1106
  uint8_t displayRowMode;   // 0=5 rows, 1=6 rows, 2=Large 2-row, 3=Large 3-row
  uint8_t colonBlinkMode;   // 0=Always On, 1=Blink, 2=Always Off
  uint8_t colonBlinkRate;   // Tenths of Hz (10 = 1.0 Hz)
  uint8_t refreshRateMode;  // 0=Auto, 1=Manual
  uint8_t refreshRateHz;    // Manual refresh rate (1-60 Hz)
  bool boostAnimationRefresh;  // Enable 40 Hz boost during animations
  uint8_t displayBrightness;   // Display brightness 0-255 (default: 255)

  // Scheduled dimming (night mode)
  bool enableScheduledDimming;  // Enable time-based automatic dimming
  uint8_t dimStartHour;         // Hour to start dimming (0-23)
  uint8_t dimEndHour;           // Hour to end dimming (0-23)
  uint8_t dimBrightness;        // Brightness level during dim period (0-255)

  // LED Night Light settings
#if LED_PWM_ENABLED
  bool ledEnabled;              // LED night light on/off (default: false)
  uint8_t ledBrightness;        // LED brightness 0-255 (default: 128 = 50%)
#endif

  // Format options
  bool useRpmKFormat;       // Show RPM as K (e.g., 1.2K instead of 1200)
  bool useNetworkMBFormat;  // Show network as MB/s instead of KB/s

  // Network settings
  char deviceName[32];          // Device name for mDNS and app (default: "smalloled")
  bool showIPAtBoot;          // Show IP address on OLED at startup (default: true)
  bool useStaticIP;
  char staticIP[16];
  char gateway[16];
  char subnet[16];
  char dns1[16];
  char dns2[16];

  // Mario clock settings
  uint8_t marioBounceHeight;  // Tenths (40 = 4.0)
  uint8_t marioBounceSpeed;   // Tenths (6 = 0.6)
  bool marioSmoothAnimation;  // Enable 4-frame walk cycle (default: false = 2-frame)
  uint8_t marioWalkSpeed;     // Tenths (20 = 2.0, 25 = 2.5 old/fast)
  bool marioIdleEncounters;   // Enable idle enemy encounters (default: false)
  uint8_t marioEncounterFreq; // 0=Rare(25-35s), 1=Normal(15-25s), 2=Frequent(8-15s)
  uint8_t marioEncounterSpeed; // 0=Slow, 1=Normal, 2=Fast (default: 1)

  // Space clock settings
  uint8_t spaceCharacterType;   // 0=Invader, 1=Ship
  uint8_t spacePatrolSpeed;     // Tenths (10 = 1.0)
  uint8_t spaceAttackSpeed;     // Tenths (25 = 2.5)
  uint8_t spaceLaserSpeed;      // Tenths (40 = 4.0)
  uint8_t spaceExplosionGravity; // Tenths (5 = 0.5)

  // Pong clock settings
  uint8_t pongBallSpeed;        // Fixed-point (16 = 1.0)
  uint8_t pongBounceStrength;   // Tenths (3 = 0.3)
  uint8_t pongBounceDamping;    // Hundredths (85 = 0.85)
  uint8_t pongPaddleWidth;      // Pixels (20)
  bool pongHorizontalBounce;    // Enable horizontal digit bounce on side hits

  // Pac-Man clock settings
  uint8_t pacmanSpeed;          // Tenths (10 = 1.0)
  uint8_t pacmanEatingSpeed;    // Tenths (20 = 2.0)
  uint8_t pacmanMouthSpeed;     // Mouth animation speed (10 = 100ms)
  uint8_t pacmanPelletCount;    // Number of patrol pellets (0-20)
  bool pacmanPelletRandomSpacing;  // Random or even spacing
  bool pacmanBounceEnabled;     // Enable digit bounce on eat

  // Metric configuration arrays
  char metricLabels[MAX_METRICS][METRIC_NAME_LEN];
  char metricNames[MAX_METRICS][METRIC_NAME_LEN];
  uint8_t metricOrder[MAX_METRICS];
  uint8_t metricCompanions[MAX_METRICS];
  uint8_t metricPositions[MAX_METRICS];
  uint8_t metricBarPositions[MAX_METRICS];
  int metricBarMin[MAX_METRICS];
  int metricBarMax[MAX_METRICS];
  int metricBarWidths[MAX_METRICS];
  int metricBarOffsets[MAX_METRICS];
};

// ========== Mario Clock Types ==========
enum MarioState {
  MARIO_IDLE,
  MARIO_WALKING,
  MARIO_JUMPING,
  MARIO_WALKING_OFF,
  // Idle encounter states
  MARIO_ENCOUNTER_WALKING,
  MARIO_ENCOUNTER_JUMPING,
  MARIO_ENCOUNTER_SHOOTING,
  MARIO_ENCOUNTER_SQUASH,
  MARIO_ENCOUNTER_RETURNING
};

// Enemy types for idle encounters
enum EnemyType { ENEMY_NONE, ENEMY_GOOMBA, ENEMY_SPINY, ENEMY_KOOPA };
enum EnemyState { ENEMY_WALKING, ENEMY_SQUASHING, ENEMY_HIT, ENEMY_DEAD, ENEMY_SHELL_SLIDING };

struct MarioEnemy {
  EnemyType type;
  EnemyState state;
  float x;
  int walkFrame;
  uint8_t animTimer;
  bool fromRight;
};

struct MarioFireball {
  float x, y;
  float vy;
  bool active;
};

// Mario animation constants
#define MARIO_ANIM_SPEED 35
#define ENCOUNTER_ANIM_SPEED 16  // ~60fps for smooth encounter animations
#define ENCOUNTER_TIME_SCALE (ENCOUNTER_ANIM_SPEED / (float)MARIO_ANIM_SPEED)  // ~0.46
#define JUMP_POWER -4.5
#define GRAVITY 0.6
#define TIME_Y 26
#define MARIO_HEAD_OFFSET 10
#define DIGIT_BOTTOM (TIME_Y + 21)

// Digit X positions for time display
extern const int DIGIT_X[5];

// ========== Space Clock Types ==========
enum SpaceState {
  SPACE_PATROL,
  SPACE_SLIDING,
  SPACE_SHOOTING,
  SPACE_EXPLODING_DIGIT,
  SPACE_MOVING_NEXT,
  SPACE_RETURNING
};

struct Laser {
  float x, y;
  float length;
  bool active;
  int target_digit_idx;
};

#define MAX_SPACE_FRAGMENTS 20
#define LASER_MAX_LENGTH 50
#define SPACE_PATROL_LEFT 20
#define SPACE_PATROL_RIGHT 108

struct SpaceFragment {
  float x, y;
  float vx, vy;
  bool active;
};

// ========== Pong Clock Types ==========
enum PongBallState {
  PONG_BALL_NORMAL,
  PONG_BALL_SPAWNING
};

enum DigitTransitionState {
  DIGIT_NORMAL,
  DIGIT_BREAKING,
  DIGIT_ASSEMBLING
};

struct PongBall {
  int x, y;       // Fixed-point (multiply by 16)
  int vx, vy;     // Velocity (fixed-point)
  PongBallState state;
  unsigned long spawn_timer;
  bool active;
  int inside_digit;  // -1 = none, 0-4 = digit index
};

struct DigitTransition {
  DigitTransitionState state;
  char old_char;
  char new_char;
  unsigned long state_timer;
  int hit_count;
  int fragments_spawned;
  float assembly_progress;
};

struct BreakoutPaddle {
  int x;           // Center X position
  int target_x;    // Target X for auto-tracking
  int width;       // Paddle width in pixels
  int speed;       // Movement speed
};

struct FragmentTarget {
  int target_digit;
  int target_x;
  int target_y;
};

// Pong constants
#define MAX_PONG_BALLS 2
#define MAX_PONG_FRAGMENTS 40
#define PONG_BALL_SIZE 2
#define PONG_TIME_Y 16
#define PONG_PLAY_AREA_TOP 10          // Above digits (ball can enter date area)
#define BREAKOUT_PADDLE_Y 60
#define BREAKOUT_PADDLE_HEIGHT 2
#define PONG_UPDATE_INTERVAL 20
#define BALL_SPAWN_DELAY 500
#define PONG_FRAG_GRAVITY 0.3
#define PONG_FRAG_SPEED 1.5
#define BALL_HIT_THRESHOLD 3
#define DIGIT_TRANSITION_TIMEOUT 3000
#define DIGIT_ASSEMBLY_DURATION 800
#define PONG_BALL_SPEED_BOOST 28
#define MULTIBALL_ACTIVATE_SECOND 55
#define PADDLE_WRONG_DIRECTION_CHANCE 0    // 0 = disabled (smooth tracking), 10-20 = adds chaos
#define PADDLE_STICK_MIN_DELAY 0
#define PADDLE_STICK_MAX_DELAY 300
#define PADDLE_MOMENTUM_MULTIPLIER 2
#define BALL_RELEASE_RANDOM_VARIATION 2
#define BALL_COLLISION_ANGLE_VARIATION 3

extern const float FRAGMENT_SPAWN_PERCENT[3];

// ========== Pac-Man Clock Types ==========
enum PacmanState {
  PACMAN_PATROL,
  PACMAN_TARGETING,
  PACMAN_EATING,
  PACMAN_RETURNING
};

struct PatrolPellet {
  int x;
  bool active;
};

struct PathStep {
  uint8_t col;  // Column in grid (0-4)
  uint8_t row;  // Row in grid (0-6)
};

// Pac-Man constants
#define PACMAN_ANIM_SPEED 30
#define PACMAN_PATROL_Y 56
#define MAX_PATROL_PELLETS 20
#define TIME_Y_PACMAN 16
#define PELLET_SPACING 5
#define PELLET_SIZE 1
#define DIGIT_GRID_W 5
#define DIGIT_GRID_H 7

// ========== Extern Global Declarations ==========
// These are defined in main.cpp

// Settings and state
extern Settings settings;
extern MetricData metricData;
extern bool displayAvailable;
extern bool ntpSynced;
extern unsigned long lastNtpSyncTime;
extern unsigned long lastReceived;
extern unsigned long wifiDisconnectTime;
extern unsigned long nextDisplayUpdate;
extern bool wifiConnected;  // WiFi connection status for icon display

// Mario clock globals
extern MarioState mario_state;
extern float mario_x;
extern float mario_jump_y;
extern float jump_velocity;
extern int mario_base_y;
extern bool mario_facing_right;
extern int mario_walk_frame;
extern unsigned long last_mario_update;
extern int displayed_hour;
extern int displayed_min;
extern bool time_overridden;
extern int last_minute;
extern bool animation_triggered;
extern bool digit_bounce_triggered;
extern int num_targets;
extern int target_x_positions[4];
extern int target_digit_index[4];
extern int target_digit_values[4];
extern int current_target_index;
extern float digit_offset_y[5];
extern float digit_velocity[5];
extern float digit_offset_x[5];
extern float digit_velocity_x[5];

// Mario idle encounter globals
extern MarioEnemy currentEnemy;
extern MarioFireball marioFireball;
extern unsigned long lastEncounterEnd;
extern unsigned long nextEncounterDelay;

// Space clock globals
extern SpaceState space_state;
extern float space_x;
extern const float space_y;
extern int space_anim_frame;
extern int space_patrol_direction;
extern unsigned long last_space_update;
extern unsigned long last_space_sprite_toggle;
extern Laser space_laser;
extern SpaceFragment space_fragments[MAX_SPACE_FRAGMENTS];
extern int space_explosion_timer;

// Pong clock globals
extern PongBall pong_balls[MAX_PONG_BALLS];
extern SpaceFragment pong_fragments[MAX_PONG_FRAGMENTS];
extern FragmentTarget fragment_targets[MAX_PONG_FRAGMENTS];
extern DigitTransition digit_transitions[5];
extern BreakoutPaddle breakout_paddle;
extern unsigned long last_pong_update;
extern bool ball_stuck_to_paddle[MAX_PONG_BALLS];
extern unsigned long ball_stick_release_time[MAX_PONG_BALLS];
extern int ball_stuck_x_offset[MAX_PONG_BALLS];
extern int paddle_last_x;

// Pac-Man clock globals
extern PacmanState pacman_state;
extern float pacman_x;
extern float pacman_y;
extern int pacman_direction;
extern int pacman_mouth_frame;
extern unsigned long last_pacman_update;
extern unsigned long last_pacman_mouth_toggle;
extern int last_minute_pacman;
extern bool pacman_animation_triggered;
extern bool digit_being_eaten[5];
extern int digit_eaten_rows_left[5];
extern int digit_eaten_rows_right[5];
extern PatrolPellet patrol_pellets[MAX_PATROL_PELLETS];
extern int num_pellets;
extern uint8_t digitEatenPellets[5][5];
extern uint8_t current_eating_digit_index;
extern uint8_t current_eating_digit_value;
extern uint8_t current_path_step;
extern float pellet_eat_distance;
extern uint8_t target_digit_queue[4];
extern uint8_t target_digit_new_values[4];
extern uint8_t target_queue_length;
extern uint8_t target_queue_index;
extern uint8_t pending_digit_index;
extern uint8_t pending_digit_value;

#endif // CONFIG_H
