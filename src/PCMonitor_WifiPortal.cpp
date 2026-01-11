/*
 * PC Stats Monitor Display - WiFi Portal Version with Web Config
 * ESP32-C3 Super Mini + SSD1306/SH1106 128x64 OLED
 *
 * Features:
 * - WiFi setup portal for easy configuration
 * - Web-based settings page (access via ESP32 IP address)
 * - PC stats display when online (CPU, RAM, GPU, Disk, Fan)
 * - Clock + date when offline (Mario / Standard / Large)
 * - Settings saved to flash memory
 * - UDP data reception on port 4210
 * - Configurable timezone and date format
 *
 * Required Libraries:
 * - WiFiManager by tzapu (install from Library Manager)
 * - Adafruit SSD1306 / Adafruit SH110x
 * - Adafruit GFX
 * - ArduinoJson
 * - Preferences (built-in)
 * - WebServer (built-in)
 */

// ========== Display Type Selection ==========
// Set to 0 for 0.96" SSD1306 (I2C: 0x3C), set to 1 for 1.3" SH1106 (I2C: 0x3D)
#define DISPLAY_TYPE 1  // Change to 1 for 1.3" OLED

#include <WiFi.h>
#include <WiFiManager.h>
#include <WebServer.h>
#include <Preferences.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#if DISPLAY_TYPE == 1
  #include <Adafruit_SH110X.h>  // For 1.3" SH1106
#else
  #include <Adafruit_SSD1306.h>  // For 0.96" SSD1306
#endif
#include <ArduinoJson.h>
#include <time.h>
#include <esp_task_wdt.h>
#include <Update.h>

// ========== WiFi Portal Configuration ==========
const char* AP_NAME = "PCMonitor-Setup";
const char* AP_PASSWORD = "monitor123";

// ========== Optional Hardcoded WiFi (for modules with faulty AP) ==========
// Leave blank ("") to use WiFiManager portal (default behavior)
// Set your WiFi credentials here to bypass WiFiManager:
const char* HARDCODED_SSID = "";        // Your WiFi network name
const char* HARDCODED_PASSWORD = "";    // Your WiFi password

// ========== UDP Configuration ==========
WiFiUDP udp;
const int UDP_PORT = 4210;

// ========== Web Server ==========
WebServer server(80);
Preferences preferences;

// ========== Display Configuration ==========
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define SDA_PIN 8
#define SCL_PIN 9
#if DISPLAY_TYPE == 1
  Adafruit_SH1106G display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
  #define DISPLAY_WHITE SH110X_WHITE
  #define DISPLAY_BLACK SH110X_BLACK
#else
  Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
  #define DISPLAY_WHITE SSD1306_WHITE
  #define DISPLAY_BLACK SSD1306_BLACK
#endif

// ========== NTP Time Configuration ==========
const char* ntpServer = "pool.ntp.org";

// ========== Dynamic Metrics System (v2.0) ==========
#define MAX_METRICS 20  // Increased from 12 to support more metrics with companions
#define METRIC_NAME_LEN 11  // 10 chars + null terminator
#define METRIC_UNIT_LEN 5   // 4 chars + null terminator

struct Metric {
  uint8_t id;                     // 1-20
  char name[METRIC_NAME_LEN];     // "CPU%", "FAN1", etc. (from Python)
  char label[METRIC_NAME_LEN];    // Custom label (user editable, preserves '^' for spaces)
  char unit[METRIC_UNIT_LEN];     // "%", "C", "RPM", etc.
  int value;                      // Sensor value
  uint8_t displayOrder;           // Display position (0-19, for sorting in web UI)
  uint8_t companionId;            // ID of companion metric (0 = none, 1-20 = metric ID)
  uint8_t position;               // Display position: 0-11 (0=R1-L, 1=R1-R, 2=R2-L, ..., 11=R6-R), 255=None/Hidden
  uint8_t barPosition;            // Position where progress bar should be displayed (0-11 or 255=None)
  int barMin;                     // Min value for progress bar (default 0)
  int barMax;                     // Max value for progress bar (default 100)
  uint8_t barWidth;               // Bar width in pixels (default 60 for left, 64 for right)
  uint8_t barOffsetX;             // Horizontal offset in pixels from left edge (default 0)
};

struct MetricData {
  Metric metrics[MAX_METRICS];
  uint8_t count;                  // Actual number of metrics received
  char timestamp[6];              // "HH:MM"
  bool online;                    // Connection status
};

MetricData metricData;

// ========== Settings (loaded from flash) ==========
struct Settings {
  int clockStyle;        // 0 = Mario, 1 = Standard, 2 = Large, 3 = Space Invaders (Jumping), 4 = Space Invaders (Sliding)
  int gmtOffset;         // GMT offset in minutes (-720 to +840, supports half-hour zones like +5:30 = 330)
  bool daylightSaving;   // Daylight saving time
  bool use24Hour;        // 24-hour format
  int dateFormat;        // 0 = DD/MM/YYYY, 1 = MM/DD/YYYY, 2 = YYYY-MM-DD

  // Clock positioning
  int clockPosition;     // 0 = Center, 1 = Left, 2 = Right
  int clockOffset;       // Horizontal offset in pixels (can be negative or positive)

  // Custom metric labels (preserves '^' character for spacing)
  char metricLabels[MAX_METRICS][METRIC_NAME_LEN];  // Custom display names

  // Metric names (from Python - for validation)
  char metricNames[MAX_METRICS][METRIC_NAME_LEN];   // Original metric names (e.g., "CPU%", "PUMP")

  // Metric display order
  uint8_t metricOrder[MAX_METRICS];  // Display position for each metric

  // Companion metrics (pair metrics on same line)
  uint8_t metricCompanions[MAX_METRICS];  // Companion metric ID (0 = none)

  // Metric position assignment (slot-based: 0-11 for Row1-Left to Row6-Right, 255=None)
  uint8_t metricPositions[MAX_METRICS];  // Display slot position

  // Progress bar settings
  uint8_t metricBarPositions[MAX_METRICS];  // Position where bar should be displayed (0-11 or 255=None)
  int metricBarMin[MAX_METRICS];            // Min value for progress bars
  int metricBarMax[MAX_METRICS];            // Max value for progress bars
  uint8_t metricBarWidths[MAX_METRICS];     // Bar width in pixels
  uint8_t metricBarOffsets[MAX_METRICS];    // Bar horizontal offset in pixels

  // Clock toggle
  bool showClock;        // Show/hide timestamp in metrics display

  // Display layout mode
  int displayRowMode;    // 0 = 5 rows (12px spacing), 1 = 6 rows (10px compact)

  // RPM display format
  bool useRpmKFormat;    // true = "1.8K", false = "1800RPM"
  bool useNetworkMBFormat;  // true = "1.2M", false = "1200KB/s"

  // Colon blink settings
  uint8_t colonBlinkMode;    // 0 = Always On, 1 = Blink, 2 = Always Off
  uint8_t colonBlinkRate;    // Blink rate in tenths of Hz (5 = 0.5Hz, 10 = 1Hz, 20 = 2Hz)

  // Display refresh rate settings
  uint8_t refreshRateMode;   // 0 = Auto (adaptive), 1 = Manual
  uint8_t refreshRateHz;     // Manual refresh rate 1-60 Hz (only used if refreshRateMode = 1)
  bool boostAnimationRefresh; // Enable smooth animations by temporarily boosting refresh rate during active animations

  // Mario clock bounce settings
  uint8_t marioBounceHeight; // Bounce height in tenths (e.g., 35 = 3.5, range: 10-80)
  uint8_t marioBounceSpeed;  // Fall speed in tenths (e.g., 6 = 0.6, range: 2-15)

  // Pong clock settings
  uint8_t pongBallSpeed;       // Ball speed (e.g., 18 = 1.125 px/frame, range: 10-30)
  uint8_t pongBounceStrength;  // Spring strength in tenths (e.g., 3 = 0.3, range: 1-8)
  uint8_t pongBounceDamping;   // Damping in hundredths (e.g., 85 = 0.85, range: 50-95)
  uint8_t pongPaddleWidth;     // Paddle width in pixels (e.g., 20, range: 10-40)

  // Space clock settings (Invader/Ship)
  uint8_t spaceCharacterType;  // 0 = Invader, 1 = Ship (default: 1)
  uint8_t spacePatrolSpeed;    // Patrol drift speed in tenths (e.g., 5 = 0.5, range: 2-15)
  uint8_t spaceAttackSpeed;    // Slide speed in tenths (e.g., 25 = 2.5, range: 10-40)
  uint8_t spaceLaserSpeed;     // Laser extend speed in tenths (e.g., 40 = 4.0, range: 20-80)
  uint8_t spaceExplosionGravity; // Fragment gravity in tenths (e.g., 5 = 0.5, range: 3-10)

  // Pac-Man clock settings
  uint8_t pacmanSpeed;              // Patrol speed (5-30, default 10 = 1.0 px/frame during patrol)
  uint8_t pacmanEatingSpeed;        // Eating animation speed (10-50, default 15 = 1.5 px/frame, higher = faster digit eating)
  uint8_t pacmanMouthSpeed;         // Mouth animation speed (5-20, default 10)
  uint8_t pacmanPelletCount;        // Number of pellets (0-20, default 8)
  bool pacmanPelletRandomSpacing;   // Random vs even spacing (default true)
  bool pacmanBounceEnabled;         // Enable bounce animation for new digits (default true)

  // Network configuration
  bool useStaticIP;      // true = Static IP, false = DHCP (default)
  char staticIP[16];     // Static IP address (e.g., "192.168.1.100")
  char gateway[16];      // Gateway address (e.g., "192.168.1.1")
  char subnet[16];       // Subnet mask (e.g., "255.255.255.0")
  char dns1[16];         // Primary DNS (e.g., "8.8.8.8")
  char dns2[16];         // Secondary DNS (e.g., "8.8.4.4")
};

Settings settings;

unsigned long lastReceived = 0;
const unsigned long TIMEOUT = 6000;
bool ntpSynced = false;  // Track NTP sync status
unsigned long lastNtpSyncTime = 0;  // Track when NTP was last synced
const unsigned long NTP_RESYNC_INTERVAL = 3600000;  // Verify NTP every 1 hour (ms)
unsigned long wifiDisconnectTime = 0;  // Track WiFi disconnect time
const unsigned long WIFI_RECONNECT_TIMEOUT = 30000;  // 30s before restart
bool displayAvailable = false;  // Track if display is working
unsigned long nextDisplayUpdate = 0;  // Scheduled time for next display refresh (prevents frame drops on rate changes)
unsigned long lastAnimationUpdate = 0;  // Separate timer for animation logic (fixed 50ms intervals)

// ========== Mario Animation Variables ==========
float mario_x = -15;
int mario_base_y = 62;
float mario_jump_y = 0;
bool mario_facing_right = true;
int mario_walk_frame = 0;
unsigned long last_mario_update = 0;
const int MARIO_ANIM_SPEED = 50;

enum MarioState {
  MARIO_IDLE,
  MARIO_WALKING,
  MARIO_JUMPING,
  MARIO_WALKING_OFF
};
MarioState mario_state = MARIO_IDLE;

// Digit targeting
int target_x_positions[4];
int target_digit_index[4];
int target_digit_values[4];  // Store the target value for each digit
int num_targets = 0;
int current_target_index = 0;
int last_minute = -1;
bool animation_triggered = false;

// Jump physics
float jump_velocity = 0;
const float GRAVITY = 0.6;
const float JUMP_POWER = -4.5;
bool digit_bounce_triggered = false;

const int MARIO_HEAD_OFFSET = 10;
const int DIGIT_BOTTOM = 47;

// Displayed time
int displayed_hour = -1;
int displayed_min = -1;
bool time_overridden = false;

// Digit bounce animation
float digit_offset_y[5] = {0, 0, 0, 0, 0};
float digit_velocity[5] = {0, 0, 0, 0, 0};
const float DIGIT_BOUNCE_POWER = -3.5;
const float DIGIT_GRAVITY = 0.6;

// Time display constants
constexpr int TIME_Y = 26;  // Standard Y position for time display (Mario, Standard, Large, Space, Pong)

constexpr int TIME_Y_PACMAN = 16;  // Y position for Pac-Man clock (moved up for patrol clearance)
// Standard digit positions for Mario, Standard, Large, Space, Pong clocks (evenly spaced 18px)
const int DIGIT_X[5] = {19, 37, 55, 73, 91};

// Pac-Man clock uses different spacing (wider gaps for pellet layout)
const int DIGIT_X_PACMAN[5] = {1, 30, 56, 74, 103};

// Display layout constants
constexpr int OLED_SCREEN_WIDTH = 128;
constexpr int OLED_SCREEN_HEIGHT = 64;
constexpr int OLED_SCREEN_CENTER_X = 64;

// ========== Space Clock Animation Variables (Clock Style 3 & 4 merged) ==========
// This animation works for both Invader (character type 0) and Ship (character type 1)
enum SpaceState {
  SPACE_PATROL,
  SPACE_SLIDING,
  SPACE_SHOOTING,
  SPACE_EXPLODING_DIGIT,
  SPACE_MOVING_NEXT,
  SPACE_RETURNING
};

// Space fragment (explosion debris - works for both Invader and Ship)
struct SpaceFragment {
  float x, y;
  float vx, vy;
  bool active;
};

float space_x = 64;                    // Horizontal position
const float space_y = 56;              // Fixed Y position (always at bottom)
int space_anim_frame = 0;              // Sprite animation frame
int space_patrol_direction = 1;        // 1 = right, -1 = left
SpaceState space_state = SPACE_PATROL;

// Timing
unsigned long last_space_update = 0;
unsigned long last_space_sprite_toggle = 0;

// Movement constants (boundaries shared by both characters)
const int SPACE_PATROL_LEFT = 15;           // Left boundary
const int SPACE_PATROL_RIGHT = 113;         // Right boundary

// Laser system
struct Laser {
  float x, y;
  float length;
  bool active;
  int target_digit_idx;
};

// Unified laser (used by both Invader and Ship characters)
Laser space_laser = {0, 0, false, 0, -1};
const float LASER_MAX_LENGTH = 30.0;  // Max visual length

#define MAX_SPACE_FRAGMENTS 16
SpaceFragment space_fragments[MAX_SPACE_FRAGMENTS] = {0};
int space_explosion_timer = 0;
const int SPACE_EXPLOSION_DURATION = 25;

// Note: Speed values now come from settings (configured via web UI):
// - settings.spacePatrolSpeed (tenths) = patrol drift speed (default: 5 = 0.5)
// - settings.spaceAttackSpeed (tenths) = slide speed (default: 25 = 2.5)
// - settings.spaceLaserSpeed (tenths) = laser extend speed (default: 40 = 4.0)
// - settings.spaceExplosionGravity (tenths) = fragment gravity (default: 5 = 0.5)

// ========== Breakout Clock Animation Variables (Clock Style 5) ==========

enum PongBallState {
  PONG_BALL_NORMAL,
  PONG_BALL_SPAWNING
};

enum DigitTransitionState {
  DIGIT_NORMAL,
  DIGIT_BREAKING,      // Being hit progressively (1-3 times)
  DIGIT_ASSEMBLING     // New digit fragments converging
};

struct PongBall {
  int x, y;             // Fixed-point position (pixels × 16)
  int vx, vy;           // Fixed-point velocity
  PongBallState state;
  unsigned long spawn_timer;
  bool active;          // For multi-ball support
  int inside_digit;     // -1 = not inside digit hole, 0-4 = inside digit index
};

struct BreakoutPaddle {
  int x;                // X position (center of paddle)
  int width;
  int target_x;         // Target X for auto-tracking
  int speed;
};

struct DigitTransition {
  DigitTransitionState state;
  char old_char;
  char new_char;
  unsigned long state_timer;
  int hit_count;              // 0-3 hits for progressive fragmentation
  int fragments_spawned;      // Total fragments spawned so far
  float assembly_progress;    // 0.0 to 1.0 for fragment convergence
};

// Game state
#define MAX_PONG_BALLS 2        // Support multi-ball mode
PongBall pong_balls[MAX_PONG_BALLS] = {{0, 0, 0, 0, PONG_BALL_NORMAL, 0, false, -1}, {0, 0, 0, 0, PONG_BALL_NORMAL, 0, false, -1}};
BreakoutPaddle breakout_paddle = {64, 20, 64, 3};  // Start at center
int paddle_random_offset = 0;              // Current random offset for paddle
unsigned long last_paddle_offset_update = 0;  // Last time offset was updated

DigitTransition digit_transitions[5] = {
  {DIGIT_NORMAL, 0, 0, 0, 0, 0, 0.0},
  {DIGIT_NORMAL, 0, 0, 0, 0, 0, 0.0},
  {DIGIT_NORMAL, 0, 0, 0, 0, 0, 0.0},
  {DIGIT_NORMAL, 0, 0, 0, 0, 0, 0.0},
  {DIGIT_NORMAL, 0, 0, 0, 0, 0, 0.0}
};

// Fragment pool (reuse SpaceFragment structure)
// Extended for fragment assembly - need to track target positions
#define MAX_PONG_FRAGMENTS 32   // Increased for progressive break + assembly
SpaceFragment pong_fragments[MAX_PONG_FRAGMENTS] = {0};

// Fragment target tracking for assembly animation
struct FragmentTarget {
  int target_digit;     // Which digit this fragment belongs to
  int target_x, target_y;  // Final position in digit
};
FragmentTarget fragment_targets[MAX_PONG_FRAGMENTS] = {0};

unsigned long last_pong_update = 0;

// Paddle stick mechanic - prevents vertical loop by adding random delay
bool ball_stuck_to_paddle[MAX_PONG_BALLS] = {false, false};
unsigned long ball_stick_release_time[MAX_PONG_BALLS] = {0, 0};
int ball_stuck_x_offset[MAX_PONG_BALLS] = {0, 0};
int paddle_last_x = 64;  // Track paddle position from previous frame for velocity

// ========== PONG CLOCK CONFIGURATION ==========
// See pong_clock_options.md for detailed documentation

// Loop Prevention & Paddle Behavior
const int PADDLE_STICK_MIN_DELAY = 0;           // Min ms ball sticks to paddle (0 = instant possible)
const int PADDLE_STICK_MAX_DELAY = 60;          // Max ms ball sticks to paddle (prevents vertical loops)
const int PADDLE_WRONG_DIRECTION_CHANCE = 0;    // % chance paddle moves wrong way (0 = disabled, 10-20 = chaos)
const int PADDLE_MOMENTUM_MULTIPLIER = 3;       // How much paddle movement affects ball (1-5)

// Ball Physics & Collision
const int BALL_COLLISION_ANGLE_VARIATION = 6;   // Random angle change on digit hit (±degrees)
const int BALL_RELEASE_RANDOM_VARIATION = 4;    // Random velocity change on paddle release

// Digit Transition Timeout (Critical Fix)
const int DIGIT_TRANSITION_TIMEOUT = 5000;      // Max ms to wait for ball hit before auto-completing (prevents stuck digits)

// Game Constants (rarely changed)
const int PONG_UPDATE_INTERVAL = 20;        // 50 FPS
const int PONG_BALL_SIZE = 2;
const int BREAKOUT_PADDLE_Y = 60;           // Fixed Y at bottom
const int BREAKOUT_PADDLE_HEIGHT = 2;
const int PONG_PLAY_AREA_TOP = 10;          // Above digits (digits at Y=16)
const int PONG_PLAY_AREA_BOTTOM = 58;       // Above paddle
// Ball speed now configured via settings.pongBallSpeed (default: 18)
const int PONG_BALL_SPEED_BOOST = 32;       // 2.0 px/frame
const float PONG_FRAG_SPEED = 2.0;
const float PONG_FRAG_GRAVITY = 0.4;        // Fragment gravity
const int BALL_SPAWN_DELAY = 400;           // ms
const int PONG_TIME_Y = 16;                 // Digit Y position
const int MULTIBALL_ACTIVATE_SECOND = 55;   // When to spawn 2nd ball
const int BALL_HIT_THRESHOLD = 1;           // Hits to break digit (1 = instant)
const int DIGIT_ASSEMBLY_DURATION = 500;    // ms for fragments to converge
const int DIGIT_HIT_DELAY = 150;            // ms between hits on breaking digit

// Progressive fragmentation: spawn 25%, then 50%, then 25%
const float FRAGMENT_SPAWN_PERCENT[3] = {0.25, 0.50, 0.25};

// ========== Pac-Man Animation Variables ==========

// Pac-Man state machine
enum PacmanState {
  PACMAN_PATROL,        // Moving at bottom eating pellets
  PACMAN_TARGETING,     // Moving to next digit to eat
  PACMAN_EATING,        // Following digit outline path
  PACMAN_RETURNING      // Returning to patrol after all digits eaten
};
PacmanState pacman_state = PACMAN_PATROL;

// Position & movement
float pacman_x = 30.0;
float pacman_y = 56.0;  // Bottom patrol line (same as Space)
int8_t pacman_direction = 1;  // 1 = right, -1 = left, 2 = up, -2 = down
uint8_t pacman_mouth_frame = 0;  // 0-3 for waka-waka animation
unsigned long last_pacman_update = 0;
unsigned long last_pacman_mouth_toggle = 0;

// Digit targeting (which digits changed)
int8_t target_digit_queue[4];            // Queue of digit indices to eat (left to right order)
uint8_t target_digit_new_values[4];      // New values for each digit (parallel to queue)
uint8_t target_queue_length = 0;
uint8_t target_queue_index = 0;

// Pending digit update (deferred until Pac-Man returns to patrol)
uint8_t pending_digit_index = 255;       // Digit slot to update (255 = none pending)
uint8_t pending_digit_value = 0;         // New value to display

// ========== Pellet-Based Digit System ==========
// Digits are composed of pellets (dots) that Pac-Man eats

// Digit pellet grid: 5 columns x 7 rows = 35 max pellets per digit
// Each pellet is a small circle
constexpr uint8_t DIGIT_GRID_W = 5;   // 5 pellets wide
constexpr uint8_t DIGIT_GRID_H = 7;   // 7 pellets tall
constexpr uint8_t MAX_PELLETS_PER_DIGIT = DIGIT_GRID_W * DIGIT_GRID_H;  // 35
constexpr uint8_t PELLET_SPACING = 5;  // Pixels between pellet centers
constexpr uint8_t PELLET_SIZE = 1;     // Pellet radius (2px diameter)

// Bitmap patterns for digits 0-9 (5x7 grid, 1 = pellet present, 0 = no pellet)
// Each row is a 5-bit value (MSB = leftmost pellet for easier readability)
const uint8_t digitPatterns[10][DIGIT_GRID_H] = {
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

// Eating paths: sequence of pellet positions (col, row) for each digit
// Pac-Man follows this path, eating pellets along the way
// Path ends with {255, 255} as terminator
// Each path now covers ALL pellets in the digit pattern
constexpr uint8_t MAX_PATH_STEPS = 50;

struct PathStep {
  uint8_t col;  // Column in grid (0-4)
  uint8_t row;  // Row in grid (0-6)
};

const PathStep eatingPaths[10][MAX_PATH_STEPS] = {
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

// Track which pellets have been eaten for each digit position (5 positions x 35 pellets)
// Using bitset: 35 bits = 5 bytes (only need 35 bits per digit)
uint8_t digitEatenPellets[5][5];  // 5 digit positions, 5 bytes each (35 bits + padding)

bool digit_being_eaten[5] = {false, false, false, false, false};
uint8_t current_eating_digit_index = 0;
uint8_t current_eating_digit_value = 0;

// Path following state
uint8_t current_path_step = 0;      // Current step in eating path
float pellet_eat_distance = 0.0;    // Progress between pellets (0.0 to PELLET_SPACING)

// Old vertical eating state (deprecated, kept for compatibility)
uint8_t current_eating_pass = 1;
float eating_y_position = 0.0;
int8_t digit_eaten_rows_left[5] = {0, 0, 0, 0, 0};
int8_t digit_eaten_rows_right[5] = {0, 0, 0, 0, 0};

// ========== Patrol Pellet System (unchanged) ==========
#define MAX_PELLETS 20
struct Pellet {
  int8_t x;
  bool active;
};
Pellet patrol_pellets[MAX_PELLETS];
uint8_t num_pellets = 8;

// Animation timing
const int PACMAN_ANIM_SPEED = 40;        // 40ms = 25 FPS
const int PACMAN_MOUTH_SPEED = 120;      // 120ms per mouth frame
const int PACMAN_PATROL_Y = 56;          // Bottom of screen

// Trigger tracking
bool pacman_animation_triggered = false;
int last_minute_pacman = -1;

// ========== WiFiManager ==========
WiFiManager wifiManager;

// Forward declarations
void loadSettings();
void saveSettings();
void setupWebServer();
void handleRoot();
void handleSave();
bool shouldShowColon();
bool isAnimationActive();
int getOptimalRefreshRate();
void handleReset();
void handleMetricsAPI();
void handleExportConfig();
void handleImportConfig();
void trimTrailingSpaces(char* str);
void convertCaretToSpaces(char* str);
void displaySetupInstructions();
void displayConnecting();
void displayConnected();
void displayClockWithMario();
void displayStandardClock();
void displayLargeClock();

// Security & validation helper functions
bool validateIP(const char* ip);
bool safeCopyString(char* dest, const char* src, size_t maxLen);
void assertBounds(int value, int minVal, int maxVal, const char* name);
void updateMarioAnimation(struct tm* timeinfo);
void drawMario(int x, int y, bool facingRight, int frame, bool jumping);
void calculateTargetDigits(int hour, int min);
void advanceDisplayedTime();
void updateSpecificDigit(int digitIndex, int newValue);
void updateDigitBounce();
void updateDigitBouncePong();  // Spring-based physics for Pong clock
void triggerDigitBounce(int digitIndex);
void drawTimeWithBounce();
void applyTimezone();
void configModeCallback(WiFiManager *myWiFiManager);
void saveConfigCallback();
bool connectManualWiFi(const char* ssid, const char* password);
void parseStats(const char* json);
void displayStats();
void displayMetricCompact(Metric* m);
void drawProgressBar(int x, int y, int width, Metric* m);
void displayClockWithSpace();
void updateSpaceAnimation(struct tm* timeinfo);
void handleSpacePatrolState();
void handleSpaceSlidingState();
void handleSpaceShootingState();
void handleSpaceExplodingState();
void handleSpaceMovingNextState();
void handleSpaceReturningState();
void drawSpaceCharacter(int x, int y, int frame);
void drawSpaceLaser(Laser* laser);
void updateSpaceLaser();
void fireSpaceLaser(int target_digit_idx);
void spawnSpaceExplosion(int digitIndex);
void updateSpaceFragments();
void drawSpaceFragments();
bool allSpaceFragmentsInactive();
SpaceFragment* findFreeSpaceFragment();
void displayClockWithPong();
void updatePongAnimation(struct tm* timeinfo);
void initPongAnimation();
void resetPongAnimation();
void spawnPongBall(int ballIndex);
void updatePongBall(int ballIndex);
void updateBreakoutPaddle();
void checkPongCollisions(int ballIndex);
bool checkDigitHoleCollision(int ballIndex, int digitIndex);
void triggerDigitTransition(int digitIndex, char oldChar, char newChar);
void updateDigitTransitions();
void spawnProgressiveFragments(int digitIndex, char oldChar, int hitNumber);
void spawnAssemblyFragments(int digitIndex, char newChar);
void updatePongFragments();
void updateAssemblyFragments();
void drawBreakoutPaddle();
void drawPongBall();
void drawPongFragments();
void drawPongDigits();
SpaceFragment* findFreePongFragment();
bool allPongFragmentsInactive();
void displayClockWithPacman();
void updatePacmanAnimation(struct tm* timeinfo);
void drawPacman(int x, int y, int direction, int mouthFrame);
void updatePacmanPatrol();
void updatePacmanEating();
void generatePellets();
void updatePellets();
void drawPellets();
void startEatingDigit(uint8_t digit_index, uint8_t digit_value);
void finishEatingDigit();
int8_t getPacmanDirection(float dx, float dy);

void setup() {
  Serial.begin(115200);
  delay(1000);

  // Serial.println("\n========================================");
  // Serial.println("PC Stats Monitor - Web Config Version");
  // Serial.println("========================================");

  // Load settings from flash
  loadSettings();

  Wire.begin(SDA_PIN, SCL_PIN);

  // Attempt display initialization with retries
  for (int attempt = 0; attempt < 3; attempt++) {
    #if DISPLAY_TYPE == 1
      // SH1106: Try 0x3C first (most common), then 0x3D
      byte addrToTry = (attempt == 0) ? 0x3C : 0x3D;
      display.begin(addrToTry);
      display.setContrast(255);
      displayAvailable = true;
      break;
    #else
      if(display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
        displayAvailable = true;
        break;
      }
    #endif
    delay(500);
  }

  if (!displayAvailable) {
    Serial.println("WARNING: Display not available, continuing without display");
  } else {
    display.clearDisplay();
    display.setTextColor(DISPLAY_WHITE);
    display.setTextSize(1);
    display.setCursor(10, 20);
    display.println("PC Monitor");
    display.setCursor(10, 35);
    display.println("Starting...");
    display.display();
  }

  // Check if hardcoded WiFi credentials are provided
  bool useManualWiFi = (strlen(HARDCODED_SSID) > 0);

  if (useManualWiFi) {
    // Use manual WiFi connection (for modules with faulty AP)
    Serial.println("\n*** USING HARDCODED WIFI CREDENTIALS ***");

    if (!connectManualWiFi(HARDCODED_SSID, HARDCODED_PASSWORD)) {
      Serial.println("Manual WiFi connection failed!");
      Serial.println("Falling back to WiFiManager portal...");
      useManualWiFi = false;  // Fall back to WiFiManager
    }
  }

  if (!useManualWiFi) {
    // Apply static IP configuration if enabled (before WiFiManager)
    if (settings.useStaticIP) {
      IPAddress local_IP, gateway_IP, subnet_IP, dns1_IP;

      if (local_IP.fromString(settings.staticIP) &&
          gateway_IP.fromString(settings.gateway) &&
          subnet_IP.fromString(settings.subnet) &&
          dns1_IP.fromString(settings.dns1)) {

        Serial.println("Configuring Static IP...");
        Serial.print("IP: "); Serial.println(local_IP);
        Serial.print("Gateway: "); Serial.println(gateway_IP);
        Serial.print("Subnet: "); Serial.println(subnet_IP);
        Serial.print("DNS1: "); Serial.println(dns1_IP);

        wifiManager.setSTAStaticIPConfig(local_IP, gateway_IP, subnet_IP, dns1_IP);
      } else {
        Serial.println("Invalid static IP configuration, using DHCP");
      }
    }

    // Use WiFiManager portal (default behavior)
    // Serial.println("\n\n========== WIFI MANAGER SETUP ==========");
    wifiManager.setConnectTimeout(30);
    // Serial.println("Connect timeout set to 30s");

    wifiManager.setConfigPortalTimeout(180);
    // Serial.println("Portal timeout set to 180s");

    wifiManager.setAPCallback(configModeCallback);
    wifiManager.setSaveConfigCallback(saveConfigCallback);
    wifiManager.setDebugOutput(false);  // Disable verbose WiFiManager debug output
    // Serial.println("Callbacks registered, debug enabled");

    // Serial.println("\nCalling autoConnect...");
    // Serial.flush();

    if (!wifiManager.autoConnect(AP_NAME, AP_PASSWORD)) {
      Serial.println("Failed to connect and hit timeout");
      if (displayAvailable) {
        display.clearDisplay();
        display.setCursor(10, 20);
        display.println("WiFi Timeout!");
        display.setCursor(10, 35);
        display.println("Restarting...");
        display.display();
      }
      delay(3000);
      ESP.restart();
    }
  }

  Serial.println("WiFi Connected!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  // Set WiFi TX power to maximum for better range
  WiFi.setTxPower(WIFI_POWER_19_5dBm);  // Maximum: 19.5 dBm (~89 mW)
  // Serial.println("WiFi TX Power set to maximum (19.5 dBm)");

  // Apply timezone and start NTP (async operation)
  applyTimezone();
  // Serial.println("NTP sync initiated...");
  ntpSynced = false;

  // Display sync status
  if (displayAvailable) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(10, 20);
    display.println("Syncing time...");
    display.display();
  }

  // Attempt initial sync with short retries (max 1 second total)
  struct tm timeinfo;
  for (int i = 0; i < 10; i++) {
    if (getLocalTime(&timeinfo, 100)) {
      // Verify time is reasonable (year > 2020) before accepting
      if (timeinfo.tm_year > 120) {  // tm_year is years since 1900
        ntpSynced = true;
        lastNtpSyncTime = millis();
        Serial.println("NTP time synchronized successfully");
        break;
      }
    }
    delay(100);
  }

  if (!ntpSynced) {
    Serial.println("NTP sync pending, will retry in background");
  }

  // Configure hardware watchdog timer (15 seconds)
  // NOTE: Enabled AFTER WiFi config portal to allow time for user setup
  esp_task_wdt_init(15, true);  // 15s timeout, panic on timeout
  esp_task_wdt_add(NULL);       // Add current task
  // Serial.println("Watchdog enabled (15s)");

  // Start UDP listener
  udp.begin(UDP_PORT);
  Serial.print("UDP listening on port ");
  Serial.println(UDP_PORT);

  // Initialize metricData
  metricData.count = 0;
  metricData.online = false;
  Serial.println("Waiting for PC stats data...");

  // Setup web server for configuration
  setupWebServer();
  // Serial.println("Web server started on port 80");

  // Show IP address for 5 seconds
  if (displayAvailable) {
    displayConnected();
    delay(5000);
  }

  // Serial.println("Setup complete!");
  // Serial.println("========================================");
}

void configModeCallback(WiFiManager *myWiFiManager) {
  Serial.println("Config mode entered");
  Serial.println(WiFi.softAPIP());

  if (displayAvailable) {
    displaySetupInstructions();
  }
}

void saveConfigCallback() {
  // Serial.println("Config saved");
  if (displayAvailable) {
    displayConnecting();
  }
}

// Connect to WiFi using hardcoded credentials (no WiFiManager)
bool connectManualWiFi(const char* ssid, const char* password) {
  // Serial.println("\n========== MANUAL WIFI CONNECTION ==========");
  // Serial.print("Connecting to: ");
  // Serial.println(ssid);

  if (displayAvailable) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(10, 20);
    display.println("Connecting to");
    display.setCursor(10, 35);
    display.println(ssid);
    display.display();
  }

  WiFi.mode(WIFI_STA);

  // Apply static IP configuration if enabled
  if (settings.useStaticIP) {
    IPAddress local_IP, gateway_IP, subnet_IP, dns1_IP, dns2_IP;

    if (local_IP.fromString(settings.staticIP) &&
        gateway_IP.fromString(settings.gateway) &&
        subnet_IP.fromString(settings.subnet) &&
        dns1_IP.fromString(settings.dns1)) {

      // Try to parse DNS2, but it's optional
      dns2_IP.fromString(settings.dns2);

      Serial.println("Configuring Static IP for manual WiFi...");
      if (!WiFi.config(local_IP, gateway_IP, subnet_IP, dns1_IP, dns2_IP)) {
        Serial.println("Static IP configuration failed!");
      }
    } else {
      Serial.println("Invalid static IP configuration, using DHCP");
    }
  }

  WiFi.begin(ssid, password);

  int attempts = 0;
  int maxAttempts = 30;  // 30 seconds timeout (30 x 1000ms)

  while (WiFi.status() != WL_CONNECTED && attempts < maxAttempts) {
    delay(1000);
    attempts++;
    // Serial.print(".");

    // Update display with progress
    if (displayAvailable && attempts % 5 == 0) {
      display.clearDisplay();
      display.setTextSize(1);
      display.setCursor(10, 20);
      display.println("Connecting...");
      display.setCursor(10, 35);
      display.print("Attempt: ");
      display.print(attempts);
      display.print("/");
      display.println(maxAttempts);
      display.display();
    }
  }

  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi Connected!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
    return true;
  } else {
    Serial.println("WiFi Connection Failed!");
    return false;
  }
}

void loadSettings() {
  // Try to open preferences namespace (create if doesn't exist)
  if (!preferences.begin("pcmonitor", false)) {  // Read-write mode to create if needed
    Serial.println("WARNING: Failed to open preferences, using defaults");
    // Initialize with defaults
    settings.clockStyle = 0;
    settings.gmtOffset = 1;
    settings.daylightSaving = true;
    settings.use24Hour = true;
    settings.dateFormat = 0;
    settings.clockPosition = 0;  // Center by default
    settings.clockOffset = 0;    // No offset by default
    settings.showClock = true;
    settings.displayRowMode = 0;  // Default: 5 rows with more spacing
    settings.useRpmKFormat = false;  // Default: Full RPM format (1800RPM)
    settings.useNetworkMBFormat = false;  // Default: Full KB/s format
    settings.colonBlinkMode = 1;  // Default: Blink
    settings.colonBlinkRate = 10;  // Default: 1.0 Hz (10 = 1.0Hz in tenths)
    settings.refreshRateMode = 0;  // Default: Auto
    settings.refreshRateHz = 10;  // Default manual rate: 10 Hz
    settings.boostAnimationRefresh = true;  // Default: Enable smooth animation boost
    settings.marioBounceHeight = 35;  // Default: 3.5 (35 = 3.5 in tenths)
    settings.marioBounceSpeed = 6;   // Default: 0.6 (6 = 0.6 in tenths)
    settings.pongBallSpeed = 18;      // Default: 18 (1.125 px/frame)
    settings.pongBounceStrength = 3;  // Default: 0.3 (3 = 0.3 in tenths)
    settings.pongBounceDamping = 85;  // Default: 0.85 (85 = 0.85 in hundredths)
    settings.pongPaddleWidth = 20;    // Default: 20 pixels
    settings.spaceCharacterType = 1;  // Default: Ship (1 = Ship, 0 = Invader)
    settings.spacePatrolSpeed = 5;    // Default: 0.5 (5 = 0.5 in tenths)
    settings.spaceAttackSpeed = 25;   // Default: 2.5 (25 = 2.5 in tenths)
    settings.spaceLaserSpeed = 40;    // Default: 4.0 (40 = 4.0 in tenths)
    settings.spaceExplosionGravity = 5; // Default: 0.5 (5 = 0.5 in tenths)
    // Initialize all metrics with defaults
    for (int i = 0; i < MAX_METRICS; i++) {
      settings.metricLabels[i][0] = '\0';  // Empty = use Python name
      settings.metricNames[i][0] = '\0';   // Empty = no stored name
      settings.metricOrder[i] = i;  // Default order
      settings.metricCompanions[i] = 0;  // No companion by default
      settings.metricPositions[i] = 255;  // Default: None/Hidden (user must assign position)
      settings.metricBarPositions[i] = 255;  // Default: No progress bar
      settings.metricBarMin[i] = 0;
      settings.metricBarMax[i] = 100;
      settings.metricBarWidths[i] = 60;  // Default width (60px for left, 64px for right)
      settings.metricBarOffsets[i] = 0;  // Default: no offset
    }
    Serial.println("Settings initialized with defaults");
    return;
  }

  // Check if this is a fresh namespace (no settings saved yet)
  if (!preferences.isKey("clockStyle")) {
    Serial.println("Fresh preferences namespace detected, initializing with defaults...");
    // Write defaults to NVS
    preferences.putInt("clockStyle", 0);
    preferences.putInt("gmtOffset", 1);
    preferences.putBool("dst", true);
    preferences.putBool("use24Hour", true);
    preferences.putInt("dateFormat", 0);
    preferences.putInt("clockPos", 0);  // Center
    preferences.putInt("clockOffset", 0);  // No offset
    preferences.putBool("showClock", true);
    preferences.putInt("rowMode", 0);  // Default: 5 rows
    preferences.putBool("rpmKFormat", false);  // Default: Full RPM format
    preferences.putUChar("colonBlink", 1);  // Default: Blink
    preferences.putUChar("colonRate", 10);  // Default: 1.0 Hz
    preferences.putUChar("refreshMode", 0);  // Default: Auto
    preferences.putUChar("refreshHz", 10);  // Default: 10 Hz
    preferences.putBool("boostAnim", true);  // Default: Enable animation boost
    preferences.putUChar("marioBnceH", 35);  // Default: 3.5
    preferences.putUChar("marioBnceS", 6);   // Default: 0.6
    preferences.putUChar("pongBallSpd", 18);  // Default: 18
    preferences.putUChar("pongBncStr", 3);   // Default: 0.3
    preferences.putUChar("pongBncDmp", 85);  // Default: 0.85
    preferences.putUChar("pongPadWid", 20);  // Default: 20
    preferences.putUChar("spaceChar", 1);    // Default: Ship
    preferences.putUChar("spacePatrol", 5);  // Default: 0.5
    preferences.putUChar("spaceAttack", 25); // Default: 2.5
    preferences.putUChar("spaceLaser", 40);  // Default: 4.0
    preferences.putUChar("spaceExpGrv", 5);  // Default: 0.5

    // Initialize all metrics with default values
    uint8_t defaultOrder[MAX_METRICS];
    uint8_t defaultCompanions[MAX_METRICS];
    for (int i = 0; i < MAX_METRICS; i++) {
      defaultOrder[i] = i;
      defaultCompanions[i] = 0;  // No companion
    }
    preferences.putBytes("metricOrd", defaultOrder, MAX_METRICS);
    preferences.putBytes("metricComp", defaultCompanions, MAX_METRICS);

    Serial.println("Default settings written to NVS");
  }

  settings.clockStyle = preferences.getInt("clockStyle", 0);  // Default: Mario

  // gmtOffset migration: convert old hours to new minutes format
  int loadedOffset = preferences.getInt("gmtOffset", 60);
  if (loadedOffset >= -12 && loadedOffset <= 14 && loadedOffset != 0) {
    // Old format (hours): convert to minutes
    settings.gmtOffset = loadedOffset * 60;
    preferences.putInt("gmtOffset", settings.gmtOffset);  // Save new format
  } else {
    settings.gmtOffset = loadedOffset;  // Already in minutes
  }

  settings.daylightSaving = preferences.getBool("dst", true); // Default: true
  settings.use24Hour = preferences.getBool("use24Hour", true); // Default: 24h
  settings.dateFormat = preferences.getInt("dateFormat", 0);  // Default: DD/MM/YYYY
  settings.clockPosition = preferences.getInt("clockPos", 0);  // Default: Center
  settings.clockOffset = preferences.getInt("clockOffset", 0);  // Default: No offset
  settings.showClock = preferences.getBool("showClock", true);
  settings.displayRowMode = preferences.getInt("rowMode", 0);  // Default: 5 rows
  settings.useRpmKFormat = preferences.getBool("rpmKFormat", false);  // Default: Full RPM format
  settings.useNetworkMBFormat = preferences.getBool("netMBFormat", false);  // Default: Full KB/s format
  settings.colonBlinkMode = preferences.getUChar("colonBlink", 1);  // Default: Blink
  settings.colonBlinkRate = preferences.getUChar("colonRate", 10);  // Default: 1.0 Hz
  settings.refreshRateMode = preferences.getUChar("refreshMode", 0);  // Default: Auto
  settings.refreshRateHz = preferences.getUChar("refreshHz", 10);  // Default: 10 Hz
  settings.boostAnimationRefresh = preferences.getBool("boostAnim", true);  // Default: Enable
  settings.marioBounceHeight = preferences.getUChar("marioBnceH", 35);  // Default: 3.5
  settings.marioBounceSpeed = preferences.getUChar("marioBnceS", 6);   // Default: 0.6
  settings.pongBallSpeed = preferences.getUChar("pongBallSpd", 18);     // Default: 18
  settings.pongBounceStrength = preferences.getUChar("pongBncStr", 3);  // Default: 0.3
  settings.pongBounceDamping = preferences.getUChar("pongBncDmp", 85);  // Default: 0.85
  settings.pongPaddleWidth = preferences.getUChar("pongPadWid", 20);    // Default: 20
  settings.pacmanSpeed = preferences.getUChar("pacmanSpeed", 10);       // Default: 1.0 patrol speed
  settings.pacmanEatingSpeed = preferences.getUChar("pacmanEatSpeed", 20); // Default: 2.0 eating speed
  settings.pacmanMouthSpeed = preferences.getUChar("pacmanMouthSpeed", 10); // Default: 1.0 Hz
  settings.pacmanPelletCount = preferences.getUChar("pacmanPelletCount", 8); // Default: 8
  settings.pacmanPelletRandomSpacing = preferences.getBool("pacmanPelletRand", true); // Default: true
  settings.pacmanBounceEnabled = preferences.getBool("pacmanBounce", true); // Default: true
  settings.spaceCharacterType = preferences.getUChar("spaceChar", 1);   // Default: Ship
  settings.spacePatrolSpeed = preferences.getUChar("spacePatrol", 5);   // Default: 0.5
  settings.spaceAttackSpeed = preferences.getUChar("spaceAttack", 25);  // Default: 2.5
  settings.spaceLaserSpeed = preferences.getUChar("spaceLaser", 40);    // Default: 4.0
  settings.spaceExplosionGravity = preferences.getUChar("spaceExpGrv", 5); // Default: 0.5

  // Load network configuration
  settings.useStaticIP = preferences.getBool("useStaticIP", false);  // Default: DHCP
  String loadedIP = preferences.getString("staticIP", "192.168.1.100");
  String loadedGW = preferences.getString("gateway", "192.168.1.1");
  String loadedSN = preferences.getString("subnet", "255.255.255.0");
  String loadedDNS1 = preferences.getString("dns1", "8.8.8.8");
  String loadedDNS2 = preferences.getString("dns2", "8.8.4.4");

  strncpy(settings.staticIP, loadedIP.c_str(), 15);
  settings.staticIP[15] = '\0';
  strncpy(settings.gateway, loadedGW.c_str(), 15);
  settings.gateway[15] = '\0';
  strncpy(settings.subnet, loadedSN.c_str(), 15);
  settings.subnet[15] = '\0';
  strncpy(settings.dns1, loadedDNS1.c_str(), 15);
  settings.dns1[15] = '\0';
  strncpy(settings.dns2, loadedDNS2.c_str(), 15);
  settings.dns2[15] = '\0';

  // Note: Visibility is now determined by position (255 = hidden, 0-11 = visible)

  // Load metric display order
  size_t orderSize = preferences.getBytesLength("metricOrd");
  if (orderSize == MAX_METRICS) {
    preferences.getBytes("metricOrd", settings.metricOrder, MAX_METRICS);
    Serial.println("Loaded metric order from NVS");
  } else {
    // Default sequential order if not found
    Serial.println("Initializing metric order to default (0-11)");
    for (int i = 0; i < MAX_METRICS; i++) {
      settings.metricOrder[i] = i;
    }
    preferences.putBytes("metricOrd", settings.metricOrder, MAX_METRICS);
  }

  // Load companion metrics
  size_t companionSize = preferences.getBytesLength("metricComp");
  if (companionSize == MAX_METRICS) {
    preferences.getBytes("metricComp", settings.metricCompanions, MAX_METRICS);
    Serial.println("Loaded metric companions from NVS");
  } else {
    // Default no companions if not found
    Serial.println("Initializing companions to none (0)");
    for (int i = 0; i < MAX_METRICS; i++) {
      settings.metricCompanions[i] = 0;
    }
    preferences.putBytes("metricComp", settings.metricCompanions, MAX_METRICS);
  }

  // Load metric positions
  size_t posSize = preferences.getBytesLength("metricPos");
  if (posSize == MAX_METRICS) {
    preferences.getBytes("metricPos", settings.metricPositions, MAX_METRICS);
    Serial.println("Loaded metric positions from NVS");
  } else {
    // Default: all positions set to None (255)
    Serial.println("Initializing positions to None (255)");
    for (int i = 0; i < MAX_METRICS; i++) {
      settings.metricPositions[i] = 255;  // None/Hidden by default
    }
    preferences.putBytes("metricPos", settings.metricPositions, MAX_METRICS);
  }

  // Load progress bar settings
  size_t barPosSize = preferences.getBytesLength("metricBarPos");
  if (barPosSize == MAX_METRICS) {
    preferences.getBytes("metricBarPos", settings.metricBarPositions, MAX_METRICS);
    preferences.getBytes("barMin", settings.metricBarMin, MAX_METRICS * sizeof(int));
    preferences.getBytes("barMax", settings.metricBarMax, MAX_METRICS * sizeof(int));
    preferences.getBytes("barWidths", settings.metricBarWidths, MAX_METRICS);
    preferences.getBytes("barOffsets", settings.metricBarOffsets, MAX_METRICS);
    Serial.println("Loaded progress bar settings from NVS");
  } else {
    // Default: no progress bars
    for (int i = 0; i < MAX_METRICS; i++) {
      settings.metricBarPositions[i] = 255;  // None
      settings.metricBarMin[i] = 0;
      settings.metricBarMax[i] = 100;
      settings.metricBarWidths[i] = 60;  // Default width
      settings.metricBarOffsets[i] = 0;  // Default: no offset
    }
  }

  // Load custom metric labels
  for (int i = 0; i < MAX_METRICS; i++) {
    String key = "label" + String(i);
    String label = preferences.getString(key.c_str(), "");
    if (label.length() > 0) {
      strncpy(settings.metricLabels[i], label.c_str(), METRIC_NAME_LEN - 1);
      settings.metricLabels[i][METRIC_NAME_LEN - 1] = '\0';
    } else {
      settings.metricLabels[i][0] = '\0';  // Empty = use Python name
    }
  }

  // Load metric names (for validation)
  for (int i = 0; i < MAX_METRICS; i++) {
    String key = "name" + String(i);
    String name = preferences.getString(key.c_str(), "");
    if (name.length() > 0) {
      strncpy(settings.metricNames[i], name.c_str(), METRIC_NAME_LEN - 1);
      settings.metricNames[i][METRIC_NAME_LEN - 1] = '\0';
    } else {
      settings.metricNames[i][0] = '\0';  // Empty = no stored name
    }
  }

  preferences.end();

  Serial.println("Settings loaded (v2.0 - Compact Grid Layout)");
}

void saveSettings() {
  preferences.begin("pcmonitor", false);  // Read-write
  preferences.putInt("clockStyle", settings.clockStyle);
  preferences.putInt("gmtOffset", settings.gmtOffset);
  preferences.putBool("dst", settings.daylightSaving);
  preferences.putBool("use24Hour", settings.use24Hour);
  preferences.putInt("dateFormat", settings.dateFormat);
  preferences.putInt("clockPos", settings.clockPosition);
  preferences.putInt("clockOffset", settings.clockOffset);
  preferences.putBool("showClock", settings.showClock);
  preferences.putInt("rowMode", settings.displayRowMode);
  preferences.putBool("rpmKFormat", settings.useRpmKFormat);
  preferences.putBool("netMBFormat", settings.useNetworkMBFormat);
  preferences.putUChar("colonBlink", settings.colonBlinkMode);
  preferences.putUChar("colonRate", settings.colonBlinkRate);
  preferences.putUChar("refreshMode", settings.refreshRateMode);
  preferences.putUChar("refreshHz", settings.refreshRateHz);
  preferences.putBool("boostAnim", settings.boostAnimationRefresh);
  preferences.putUChar("marioBnceH", settings.marioBounceHeight);
  preferences.putUChar("marioBnceS", settings.marioBounceSpeed);
  preferences.putUChar("pongBallSpd", settings.pongBallSpeed);
  preferences.putUChar("pongBncStr", settings.pongBounceStrength);
  preferences.putUChar("pongBncDmp", settings.pongBounceDamping);
  preferences.putUChar("pongPadWid", settings.pongPaddleWidth);
  preferences.putUChar("pacmanSpeed", settings.pacmanSpeed);
  preferences.putUChar("pacmanEatSpeed", settings.pacmanEatingSpeed);
  preferences.putUChar("pacmanMouthSpeed", settings.pacmanMouthSpeed);
  preferences.putUChar("pacmanPelletCount", settings.pacmanPelletCount);
  preferences.putBool("pacmanPelletRand", settings.pacmanPelletRandomSpacing);
  preferences.putBool("pacmanBounce", settings.pacmanBounceEnabled);
  preferences.putUChar("spaceChar", settings.spaceCharacterType);
  preferences.putUChar("spacePatrol", settings.spacePatrolSpeed);
  preferences.putUChar("spaceAttack", settings.spaceAttackSpeed);
  preferences.putUChar("spaceLaser", settings.spaceLaserSpeed);
  preferences.putUChar("spaceExpGrv", settings.spaceExplosionGravity);

  // Save network configuration
  preferences.putBool("useStaticIP", settings.useStaticIP);
  preferences.putString("staticIP", settings.staticIP);
  preferences.putString("gateway", settings.gateway);
  preferences.putString("subnet", settings.subnet);
  preferences.putString("dns1", settings.dns1);
  preferences.putString("dns2", settings.dns2);

  // Save metric display order
  preferences.putBytes("metricOrd", settings.metricOrder, MAX_METRICS);

  // Save metric companions
  preferences.putBytes("metricComp", settings.metricCompanions, MAX_METRICS);

  // Save metric positions (255 = hidden, 0-11 = visible at position)
  preferences.putBytes("metricPos", settings.metricPositions, MAX_METRICS);

  // Save progress bar settings
  preferences.putBytes("metricBarPos", settings.metricBarPositions, MAX_METRICS);
  preferences.putBytes("barMin", settings.metricBarMin, MAX_METRICS * sizeof(int));
  preferences.putBytes("barMax", settings.metricBarMax, MAX_METRICS * sizeof(int));
  preferences.putBytes("barWidths", settings.metricBarWidths, MAX_METRICS);
  preferences.putBytes("barOffsets", settings.metricBarOffsets, MAX_METRICS);

  // Save custom metric labels
  for (int i = 0; i < MAX_METRICS; i++) {
    String key = "label" + String(i);
    if (settings.metricLabels[i][0] != '\0') {
      preferences.putString(key.c_str(), settings.metricLabels[i]);
    } else {
      preferences.remove(key.c_str());  // Remove if empty
    }
  }

  // Save metric names (for validation)
  for (int i = 0; i < MAX_METRICS; i++) {
    String key = "name" + String(i);
    if (settings.metricNames[i][0] != '\0') {
      preferences.putString(key.c_str(), settings.metricNames[i]);
    } else {
      preferences.remove(key.c_str());  // Remove if empty
    }
  }

  preferences.end();

  Serial.println("Settings saved (v2.0)!");
}

void applyTimezone() {
  static long lastGmtOffset = -999999;  // Invalid initial value
  static int lastDstOffset = -1;
  static unsigned long lastConfigTime = 0;
  const unsigned long MIN_CONFIG_INTERVAL = 5000;  // Minimum 5 seconds between configTime calls

  long gmtOffset_sec = settings.gmtOffset * 60;  // gmtOffset is now in minutes
  int dstOffset_sec = settings.daylightSaving ? 3600 : 0;

  // Only call configTime if settings changed OR enough time has passed
  bool settingsChanged = (gmtOffset_sec != lastGmtOffset || dstOffset_sec != lastDstOffset);
  bool enoughTimePassed = (millis() - lastConfigTime > MIN_CONFIG_INTERVAL);

  if (settingsChanged || (lastConfigTime == 0) || enoughTimePassed) {
    configTime(gmtOffset_sec, dstOffset_sec, ntpServer);
    lastGmtOffset = gmtOffset_sec;
    lastDstOffset = dstOffset_sec;
    lastConfigTime = millis();

    if (settingsChanged) {
      Serial.println("Timezone settings changed, NTP reconfigured");
    }
  } else {
    Serial.println("Skipping configTime (rate limited, settings unchanged)");
  }
}

// Helper function to get time with short timeout
bool getTimeWithTimeout(struct tm* timeinfo, unsigned long timeout_ms = 100) {
  if (!ntpSynced) {
    if (getLocalTime(timeinfo, timeout_ms)) {
      // Verify time is reasonable (year > 2020) before accepting
      if (timeinfo->tm_year > 120) {  // tm_year is years since 1900
        ntpSynced = true;
        lastNtpSyncTime = millis();
        Serial.println("NTP successfully synchronized");
        return true;
      }
    }
    return false;
  }
  return getLocalTime(timeinfo, timeout_ms);
}

void setupWebServer() {
  server.on("/", handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/reset", handleReset);
  server.on("/metrics", handleMetricsAPI);  // New API endpoint
  server.on("/api/export", HTTP_GET, handleExportConfig);
  server.on("/api/import", HTTP_POST, handleImportConfig);

  // OTA Firmware Update handlers
  server.on("/update", HTTP_POST, []() {
    server.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
    delay(1000);
    ESP.restart();
  }, []() {
    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
      Serial.printf("Update: %s\n", upload.filename.c_str());
      if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {  // Start with max available size
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      // Write uploaded data
      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_END) {
      if (Update.end(true)) {  // true = set size to current progress
        Serial.printf("Update Success: %u bytes\nRebooting...\n", upload.totalSize);
      } else {
        Update.printError(Serial);
      }
    }
  });

  server.begin();
}

// API endpoint to return current metrics as JSON
void handleMetricsAPI() {
  String json = "{\"metrics\":[";

  for (int i = 0; i < metricData.count; i++) {
    Metric& m = metricData.metrics[i];

    if (i > 0) json += ",";
    json += "{\"id\":" + String(m.id) +
            ",\"name\":\"" + String(m.name) + "\"" +
            ",\"label\":\"" + String(m.label) + "\"" +
            ",\"unit\":\"" + String(m.unit) + "\"" +
            ",\"displayOrder\":" + String(m.displayOrder) +
            ",\"companionId\":" + String(m.companionId) +
            ",\"position\":" + String(m.position) +
            ",\"barPosition\":" + String(m.barPosition) +
            ",\"barMin\":" + String(m.barMin) +
            ",\"barMax\":" + String(m.barMax) +
            ",\"barWidth\":" + String(m.barWidth) +
            ",\"barOffsetX\":" + String(m.barOffsetX) + "}";
  }

  json += "]}";

  server.send(200, "application/json", json);
}

void handleRoot() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>PC Monitor Settings</title>
  <style>
    * { box-sizing: border-box; }
    body {
      font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Arial, sans-serif;
      margin: 0; padding: 20px;
      background: linear-gradient(135deg, #0f0c29 0%, #1a1a2e 50%, #24243e 100%);
      background-attachment: fixed;
      color: #e0e7ff;
      min-height: 100vh;
    }
    .container { max-width: 420px; margin: 0 auto; padding-bottom: 100px; }
    h1 {
      color: #fff;
      text-align: center;
      font-size: 28px;
      font-weight: 700;
      margin: 0 0 8px;
      text-shadow: 0 2px 10px rgba(0,212,255,0.3);
    }
    .card {
      background: rgba(22,33,62,0.6);
      backdrop-filter: blur(10px);
      padding: 20px;
      border-radius: 12px;
      margin-bottom: 15px;
      border: 1px solid rgba(0,212,255,0.15);
      box-shadow: 0 4px 15px rgba(0,0,0,0.2);
    }
    label {
      display: block;
      margin: 15px 0 8px;
      color: #00d4ff;
      font-size: 14px;
      font-weight: 500;
      letter-spacing: 0.3px;
    }
    select, input[type="number"], input[type="text"] {
      width: 100%;
      padding: 12px 14px;
      border: 2px solid rgba(0,212,255,0.2);
      border-radius: 8px;
      background: rgba(15,52,96,0.5);
      color: #fff;
      font-size: 15px;
      transition: all 0.3s ease;
      cursor: pointer;
    }
    select:hover, input[type="number"]:hover, input[type="text"]:hover {
      border-color: rgba(0,212,255,0.4);
      background: rgba(15,52,96,0.7);
    }
    select:focus, input:focus {
      outline: none;
      border-color: #00d4ff;
      background: rgba(15,52,96,0.8);
      box-shadow: 0 0 0 3px rgba(0,212,255,0.1);
    }
    input[type="checkbox"] {
      appearance: none;
      width: 20px;
      height: 20px;
      border: 2px solid rgba(0,212,255,0.4);
      border-radius: 5px;
      background: rgba(15,52,96,0.5);
      cursor: pointer;
      position: relative;
      transition: all 0.3s ease;
      flex-shrink: 0;
    }
    input[type="checkbox"]:hover {
      border-color: #00d4ff;
      transform: scale(1.05);
    }
    input[type="checkbox"]:checked {
      background: linear-gradient(135deg, #00d4ff 0%, #0096ff 100%);
      border-color: #00d4ff;
    }
    input[type="checkbox"]:checked::after {
      content: '✓';
      position: absolute;
      color: #0f0c29;
      font-size: 14px;
      font-weight: bold;
      top: 50%;
      left: 50%;
      transform: translate(-50%, -50%);
    }
    button {
      width: 100%;
      padding: 14px;
      margin-top: 20px;
      border: none;
      border-radius: 8px;
      font-size: 16px;
      font-weight: 600;
      cursor: pointer;
      transition: all 0.3s ease;
      text-transform: uppercase;
      letter-spacing: 0.5px;
    }
    .save-btn {
      background: linear-gradient(135deg, #00d4ff 0%, #0096ff 100%);
      color: #0f0c29;
      box-shadow: 0 4px 15px rgba(0,212,255,0.3);
    }
    .save-btn:hover {
      transform: translateY(-2px);
      box-shadow: 0 6px 20px rgba(0,212,255,0.4);
    }
    .save-btn:active { transform: translateY(0); }
    .reset-btn {
      background: linear-gradient(135deg, #ff6b6b 0%, #ee5a52 100%);
      color: #fff;
      box-shadow: 0 4px 15px rgba(255,107,107,0.2);
    }
    .reset-btn:hover {
      transform: translateY(-2px);
      box-shadow: 0 6px 20px rgba(255,107,107,0.3);
    }
    .reset-btn:active { transform: translateY(0); }
    .info { text-align: center; color: #94a3b8; font-size: 12px; margin-top: 20px; }
    .status {
      background: rgba(15,52,96,0.4);
      padding: 12px;
      border-radius: 10px;
      text-align: center;
      margin-bottom: 20px;
      border: 1px solid rgba(0,212,255,0.2);
      font-size: 14px;
    }

    /* Collapsible sections */
    .section-header {
      background: linear-gradient(135deg, rgba(15,52,96,0.6) 0%, rgba(26,77,122,0.4) 100%);
      padding: 16px 18px;
      border-radius: 10px;
      cursor: pointer;
      margin-bottom: 10px;
      user-select: none;
      display: flex;
      justify-content: space-between;
      align-items: center;
      border: 1px solid rgba(0,212,255,0.15);
      transition: all 0.3s ease;
    }
    .section-header:hover {
      background: linear-gradient(135deg, rgba(15,52,96,0.8) 0%, rgba(26,77,122,0.6) 100%);
      transform: translateX(4px);
      border-color: rgba(0,212,255,0.3);
    }
    .section-header h3 {
      margin: 0;
      color: #00d4ff;
      font-size: 16px;
      font-weight: 600;
    }
    .section-arrow {
      font-size: 14px;
      transition: transform 0.3s ease;
      color: #00d4ff;
    }
    .section-arrow.collapsed { transform: rotate(-90deg); }
    .section-content {
      max-height: 10000px;
      overflow: visible;
      transition: max-height 0.3s ease, opacity 0.3s ease;
      opacity: 1;
    }
    .section-content.collapsed {
      max-height: 0;
      overflow: hidden;
      opacity: 0;
    }

    /* Config management buttons */
    .config-buttons { display: grid; grid-template-columns: 1fr 1fr; gap: 10px; margin-bottom: 20px; }
    .export-btn {
      background: linear-gradient(135deg, #10b981 0%, #059669 100%);
      color: #fff;
      padding: 12px;
      font-size: 14px;
      margin-top: 0;
      border-radius: 8px;
      font-weight: 600;
      box-shadow: 0 4px 12px rgba(16,185,129,0.2);
      transition: all 0.3s ease;
    }
    .export-btn:hover {
      transform: translateY(-2px);
      box-shadow: 0 6px 16px rgba(16,185,129,0.3);
    }
    .import-btn {
      background: linear-gradient(135deg, #3b82f6 0%, #2563eb 100%);
      color: #fff;
      padding: 12px;
      font-size: 14px;
      margin-top: 0;
      border-radius: 8px;
      font-weight: 600;
      box-shadow: 0 4px 12px rgba(59,130,246,0.2);
      transition: all 0.3s ease;
    }
    .import-btn:hover {
      transform: translateY(-2px);
      box-shadow: 0 6px 16px rgba(59,130,246,0.3);
    }

    /* Sticky save button */
    .sticky-save {
      position: fixed;
      bottom: 0;
      left: 0;
      right: 0;
      background: linear-gradient(to top, rgba(15,12,41,0.98) 0%, rgba(15,12,41,0.95) 100%);
      backdrop-filter: blur(10px);
      padding: 12px 20px;
      box-shadow: 0 -4px 20px rgba(0,0,0,0.4);
      z-index: 1000;
      border-top: 1px solid rgba(0,212,255,0.2);
    }
    .sticky-save .container { max-width: 420px; margin: 0 auto; }
    .sticky-save button { margin-top: 0; }

    /* Hidden file input */
    #importFile { display: none; }

    /* Mobile responsiveness */
    @media (max-width: 480px) {
      body { padding: 12px; }
      .container { padding-bottom: 90px; }
      h1 { font-size: 24px; }
      .card { padding: 16px; }
      .section-header { padding: 14px 16px; }
      .section-header h3 { font-size: 15px; }
      select, input[type="number"], input[type="text"] { font-size: 16px; padding: 11px 12px; }
      button { padding: 13px; font-size: 15px; }
      .sticky-save { padding: 10px 12px; }
    }
    @media (max-width: 360px) {
      h1 { font-size: 22px; }
      .config-buttons { grid-template-columns: 1fr; gap: 8px; }
    }
  </style>
</head>
<body>
  <div class="container">
    <h1>&#128421; PC Monitor</h1>
    <div class="status">
      <strong>IP:</strong> )rawliteral" + WiFi.localIP().toString() + R"rawliteral( | <strong>UDP Port:</strong> 4210
    </div>

    <!-- Config Management -->
    <div class="config-buttons">
      <button type="button" class="export-btn" onclick="exportConfig()">&#128190; Export Config</button>
      <button type="button" class="import-btn" onclick="document.getElementById('importFile').click()">&#128229; Import Config</button>
    </div>
    <input type="file" id="importFile" accept=".json" onchange="importConfig(event)">

    <form action="/save" method="POST">
      <!-- Clock Settings Section -->
      <div class="section-header" onclick="toggleSection('clockSection')">
        <h3>&#128348; Clock Settings</h3>
        <span class="section-arrow">&#9660;</span>
      </div>
      <div id="clockSection" class="section-content collapsed">
        <div class="card">
        
        <label for="clockStyle">Idle Clock Style</label>
        <select name="clockStyle" id="clockStyle" onchange="toggleMarioSettings()">
          <option value="0" )rawliteral" + String(settings.clockStyle == 0 ? "selected" : "") + R"rawliteral(>Mario Animation</option>
          <option value="1" )rawliteral" + String(settings.clockStyle == 1 ? "selected" : "") + R"rawliteral(>Standard Clock</option>
          <option value="2" )rawliteral" + String(settings.clockStyle == 2 ? "selected" : "") + R"rawliteral(>Large Clock</option>
          <option value="3" )rawliteral" + String(settings.clockStyle == 3 ? "selected" : "") + R"rawliteral(>Space Invaders</option>
          <option value="5" )rawliteral" + String(settings.clockStyle == 5 ? "selected" : "") + R"rawliteral(>Pong Clock</option>
          <option value="6" )rawliteral" + String(settings.clockStyle == 6 ? "selected" : "") + R"rawliteral(>Pac-Man Clock</option>
        </select>

        <!-- Mario Clock Settings (only visible when Mario is selected) -->
        <div id="marioSettings" style="display: )rawliteral" + String(settings.clockStyle == 0 ? "block" : "none") + R"rawliteral(; margin-top: 20px; padding: 15px; background-color: #1a1a2e; border-radius: 8px; border: 1px solid #3b82f6;">
          <h4 style="color: #3b82f6; margin-top: 0; font-size: 14px;">&#127922; Mario Animation Settings</h4>

          <label for="marioBounceHeight">Bounce Height</label>
          <input type="range" name="marioBounceHeight" id="marioBounceHeight"
                 min="10" max="80" step="5"
                 value=")rawliteral" + String(settings.marioBounceHeight) + R"rawliteral("
                 oninput="document.getElementById('bounceHeightValue').textContent = (this.value / 10).toFixed(1)">
          <span style="color: #3b82f6; font-size: 14px; margin-left: 10px;">
            <span id="bounceHeightValue">)rawliteral" + String(settings.marioBounceHeight / 10.0, 1) + R"rawliteral(</span>
          </span>
          <p style="color: #888; font-size: 12px; margin-top: 5px;">
            How high digits bounce when Mario hits them. Higher = more dramatic bounce. Default: 3.5
          </p>

          <label for="marioBounceSpeed" style="margin-top: 15px;">Fall Speed</label>
          <input type="range" name="marioBounceSpeed" id="marioBounceSpeed"
                 min="2" max="15" step="1"
                 value=")rawliteral" + String(settings.marioBounceSpeed) + R"rawliteral("
                 oninput="document.getElementById('bounceSpeedValue').textContent = (this.value / 10).toFixed(1)">
          <span style="color: #3b82f6; font-size: 14px; margin-left: 10px;">
            <span id="bounceSpeedValue">)rawliteral" + String(settings.marioBounceSpeed / 10.0, 1) + R"rawliteral(</span>
          </span>
          <p style="color: #888; font-size: 12px; margin-top: 5px;">
            How fast digits fall back down. Higher = faster fall. Default: 0.6
          </p>
        </div>

        <!-- Pong Clock Settings (only visible when Pong is selected) -->
        <div id="pongSettings" style="display: )rawliteral" + String(settings.clockStyle == 5 ? "block" : "none") + R"rawliteral(; margin-top: 20px; padding: 15px; background-color: #1a1a2e; border-radius: 8px; border: 1px solid #3b82f6;">
          <h4 style="color: #3b82f6; margin-top: 0; font-size: 14px;">🎮 Pong Animation Settings</h4>

          <label for="pongBallSpeed">Ball Speed</label>
          <input type="range" name="pongBallSpeed" id="pongBallSpeed"
                 min="16" max="30" step="1"
                 value=")rawliteral" + String(settings.pongBallSpeed) + R"rawliteral("
                 oninput="document.getElementById('ballSpeedValue').textContent = this.value">
          <span style="color: #3b82f6; font-size: 14px; margin-left: 10px;">
            <span id="ballSpeedValue">)rawliteral" + String(settings.pongBallSpeed) + R"rawliteral(</span>
          </span>
          <p style="color: #888; font-size: 12px; margin-top: 5px;">
            How fast the ball moves. Lower = relaxed, Higher = fast & exciting. Default: 18
          </p>

          <label for="pongBounceStrength" style="margin-top: 15px;">Bounce Strength</label>
          <input type="range" name="pongBounceStrength" id="pongBounceStrength"
                 min="1" max="8" step="1"
                 value=")rawliteral" + String(settings.pongBounceStrength) + R"rawliteral("
                 oninput="document.getElementById('bounceStrengthValue').textContent = (this.value / 10).toFixed(1)">
          <span style="color: #3b82f6; font-size: 14px; margin-left: 10px;">
            <span id="bounceStrengthValue">)rawliteral" + String(settings.pongBounceStrength / 10.0, 1) + R"rawliteral(</span>
          </span>
          <p style="color: #888; font-size: 12px; margin-top: 5px;">
            How much digits wobble when hit. Lower = subtle, Higher = bouncy. Default: 0.3
          </p>

          <label for="pongBounceDamping" style="margin-top: 15px;">Bounce Damping</label>
          <input type="range" name="pongBounceDamping" id="pongBounceDamping"
                 min="50" max="95" step="5"
                 value=")rawliteral" + String(settings.pongBounceDamping) + R"rawliteral("
                 oninput="document.getElementById('bounceDampingValue').textContent = (this.value / 100).toFixed(2)">
          <span style="color: #3b82f6; font-size: 14px; margin-left: 10px;">
            <span id="bounceDampingValue">)rawliteral" + String(settings.pongBounceDamping / 100.0, 2) + R"rawliteral(</span>
          </span>
          <p style="color: #888; font-size: 12px; margin-top: 5px;">
            How quickly wobble stops. Lower = wobbles longer, Higher = stops quickly. Default: 0.85
          </p>

          <label for="pongPaddleWidth" style="margin-top: 15px;">Paddle Width</label>
          <input type="range" name="pongPaddleWidth" id="pongPaddleWidth"
                 min="10" max="40" step="2"
                 value=")rawliteral" + String(settings.pongPaddleWidth) + R"rawliteral("
                 oninput="document.getElementById('paddleWidthValue').textContent = this.value">
          <span style="color: #3b82f6; font-size: 14px; margin-left: 10px;">
            <span id="paddleWidthValue">)rawliteral" + String(settings.pongPaddleWidth) + R"rawliteral(</span> px
          </span>
          <p style="color: #888; font-size: 12px; margin-top: 5px;">
            Size of the paddle. Narrower = harder, Wider = easier. Default: 20px
          </p>
        </div>

        <!-- Pac-Man Clock Settings (only visible when Pac-Man is selected) -->
        <div id="pacmanSettings" style="display: )rawliteral" + String(settings.clockStyle == 6 ? "block" : "none") + R"rawliteral(; margin-top: 20px; padding: 15px; background-color: #1a1a2e; border-radius: 8px; border: 1px solid #f1c40f;">
          <h4 style="color: #f1c40f; margin-top: 0; font-size: 14px;">👾 Pac-Man Clock Settings</h4>

          <label for="pacmanSpeed">Patrol Speed</label>
          <input type="range" name="pacmanSpeed" id="pacmanSpeed"
                 min="5" max="30" step="1"
                 value=")rawliteral" + String(settings.pacmanSpeed) + R"rawliteral("
                 oninput="document.getElementById('pacmanSpeedValue').textContent = (this.value / 10).toFixed(1)">
          <span style="color: #f1c40f; font-size: 14px; margin-left: 10px;">
            <span id="pacmanSpeedValue">)rawliteral" + String(settings.pacmanSpeed / 10.0, 1) + R"rawliteral(</span> px/frame
          </span>
          <p style="color: #888; font-size: 12px; margin-top: 5px;">
            How fast Pac-Man moves during patrol (at bottom). Range: 0.5-3.0. Default: 1.0
          </p>

          <label for="pacmanEatingSpeed" style="margin-top: 15px;">Digit Eating Speed</label>
          <input type="range" name="pacmanEatingSpeed" id="pacmanEatingSpeed"
                 min="10" max="50" step="1"
                 value=")rawliteral" + String(settings.pacmanEatingSpeed) + R"rawliteral("
                 oninput="document.getElementById('pacmanEatingSpeedValue').textContent = (this.value / 10).toFixed(1)">
          <span style="color: #f1c40f; font-size: 14px; margin-left: 10px;">
            <span id="pacmanEatingSpeedValue">)rawliteral" + String(settings.pacmanEatingSpeed / 10.0, 1) + R"rawliteral(</span> px/frame
          </span>
          <p style="color: #888; font-size: 12px; margin-top: 5px;">
            How fast Pac-Man eats digits. Higher values = faster digit eating animation. Range: 1.0-5.0. Default: 1.5 (Recommended: 2.0-3.0 for faster eating)
          </p>

          <label for="pacmanMouthSpeed" style="margin-top: 15px;">Mouth Animation Speed</label>
          <input type="range" name="pacmanMouthSpeed" id="pacmanMouthSpeed"
                 min="5" max="20" step="1"
                 value=")rawliteral" + String(settings.pacmanMouthSpeed) + R"rawliteral("
                 oninput="document.getElementById('pacmanMouthSpeedValue').textContent = (this.value / 10).toFixed(1)">
          <span style="color: #f1c40f; font-size: 14px; margin-left: 10px;">
            <span id="pacmanMouthSpeedValue">)rawliteral" + String(settings.pacmanMouthSpeed / 10.0, 1) + R"rawliteral(</span> Hz
          </span>
          <p style="color: #888; font-size: 12px; margin-top: 5px;">
            How fast Pac-Man's mouth opens and closes (waka-waka). Default: 1.0 Hz
          </p>

          <label for="pacmanPelletCount" style="margin-top: 15px;">Number of Pellets</label>
          <input type="range" name="pacmanPelletCount" id="pacmanPelletCount"
                 min="0" max="20" step="1"
                 value=")rawliteral" + String(settings.pacmanPelletCount) + R"rawliteral("
                 oninput="document.getElementById('pacmanPelletCountValue').textContent = this.value">
          <span style="color: #f1c40f; font-size: 14px; margin-left: 10px;">
            <span id="pacmanPelletCountValue">)rawliteral" + String(settings.pacmanPelletCount) + R"rawliteral(</span>
          </span>
          <p style="color: #888; font-size: 12px; margin-top: 5px;">
            How many pellets appear during patrol mode. 0 = no pellets, 20 = maximum. Default: 8
          </p>

          <label style="margin-top: 15px;">
            <input type="checkbox" name="pacmanPelletRandomSpacing"
                   )rawliteral" + String(settings.pacmanPelletRandomSpacing ? "checked" : "") + R"rawliteral(>
            Randomize Pellet Spacing
          </label>
          <p style="color: #888; font-size: 12px; margin-top: 5px;">
            When enabled, pellets appear at random positions. When disabled, pellets are evenly spaced. Default: Enabled
          </p>

          <label style="margin-top: 15px;">
            <input type="checkbox" name="pacmanBounceEnabled"
                   )rawliteral" + String(settings.pacmanBounceEnabled ? "checked" : "") + R"rawliteral(>
            Bounce Animation for New Digits
          </label>
          <p style="color: #888; font-size: 12px; margin-top: 5px;">
            When enabled, new digits bounce into place after being eaten. Disable for instant digit change. Default: Enabled
          </p>
        </div>

        <!-- Space Clock Settings (visible when Invader or Ship is selected) -->
        <div id="spaceSettings" style="display: )rawliteral" + String((settings.clockStyle == 3 || settings.clockStyle == 4) ? "block" : "none") + R"rawliteral(; margin-top: 20px; padding: 15px; background-color: #1a1a2e; border-radius: 8px; border: 1px solid #3b82f6;">
          <h4 style="color: #3b82f6; margin-top: 0; font-size: 14px;">🚀 Space Clock Animation Settings</h4>

          <label for="spaceCharacterType">Character Type</label>
          <select name="spaceCharacterType" id="spaceCharacterType">
            <option value="0" )rawliteral" + String(settings.spaceCharacterType == 0 ? "selected" : "") + R"rawliteral(>Space Invader</option>
            <option value="1" )rawliteral" + String(settings.spaceCharacterType == 1 ? "selected" : "") + R"rawliteral(>Space Ship (Default)</option>
          </select>
          <p style="color: #888; font-size: 12px; margin-top: 5px;">
            Choose the character that patrols and attacks the time digits. Default: Space Ship
          </p>

          <label for="spacePatrolSpeed" style="margin-top: 15px;">Patrol Speed</label>
          <input type="range" name="spacePatrolSpeed" id="spacePatrolSpeed"
                 min="2" max="15" step="1"
                 value=")rawliteral" + String(settings.spacePatrolSpeed) + R"rawliteral("
                 oninput="document.getElementById('patrolSpeedValue').textContent = (this.value / 10).toFixed(1)">
          <span style="color: #3b82f6; font-size: 14px; margin-left: 10px;">
            <span id="patrolSpeedValue">)rawliteral" + String(settings.spacePatrolSpeed / 10.0, 1) + R"rawliteral(</span>
          </span>
          <p style="color: #888; font-size: 12px; margin-top: 5px;">
            How fast the character drifts during patrol. Lower = zen-like, Higher = zippy. Default: 0.5
          </p>

          <label for="spaceAttackSpeed" style="margin-top: 15px;">Attack Speed</label>
          <input type="range" name="spaceAttackSpeed" id="spaceAttackSpeed"
                 min="10" max="40" step="5"
                 value=")rawliteral" + String(settings.spaceAttackSpeed) + R"rawliteral("
                 oninput="document.getElementById('attackSpeedValue').textContent = (this.value / 10).toFixed(1)">
          <span style="color: #3b82f6; font-size: 14px; margin-left: 10px;">
            <span id="attackSpeedValue">)rawliteral" + String(settings.spaceAttackSpeed / 10.0, 1) + R"rawliteral(</span>
          </span>
          <p style="color: #888; font-size: 12px; margin-top: 5px;">
            How fast the character slides to attack position. Higher = more aggressive. Default: 2.5
          </p>

          <label for="spaceLaserSpeed" style="margin-top: 15px;">Laser Speed</label>
          <input type="range" name="spaceLaserSpeed" id="spaceLaserSpeed"
                 min="20" max="80" step="5"
                 value=")rawliteral" + String(settings.spaceLaserSpeed) + R"rawliteral("
                 oninput="document.getElementById('laserSpeedValue').textContent = (this.value / 10).toFixed(1)">
          <span style="color: #3b82f6; font-size: 14px; margin-left: 10px;">
            <span id="laserSpeedValue">)rawliteral" + String(settings.spaceLaserSpeed / 10.0, 1) + R"rawliteral(</span>
          </span>
          <p style="color: #888; font-size: 12px; margin-top: 5px;">
            How fast the laser extends downward. Higher = dramatic quick strikes. Default: 4.0
          </p>

          <label for="spaceExplosionGravity" style="margin-top: 15px;">Explosion Intensity</label>
          <input type="range" name="spaceExplosionGravity" id="spaceExplosionGravity"
                 min="3" max="10" step="1"
                 value=")rawliteral" + String(settings.spaceExplosionGravity) + R"rawliteral("
                 oninput="document.getElementById('explosionGravityValue').textContent = (this.value / 10).toFixed(1)">
          <span style="color: #3b82f6; font-size: 14px; margin-left: 10px;">
            <span id="explosionGravityValue">)rawliteral" + String(settings.spaceExplosionGravity / 10.0, 1) + R"rawliteral(</span>
          </span>
          <p style="color: #888; font-size: 12px; margin-top: 5px;">
            Controls fragment gravity (how fast debris falls). Lower = floating, Higher = heavy chunks. Default: 0.5
          </p>
        </div>

        <label for="use24Hour">Time Format</label>
        <select name="use24Hour" id="use24Hour">
          <option value="1" )rawliteral" + String(settings.use24Hour ? "selected" : "") + R"rawliteral(>24-Hour (14:30)</option>
          <option value="0" )rawliteral" + String(!settings.use24Hour ? "selected" : "") + R"rawliteral(>12-Hour (2:30 PM)</option>
        </select>
        
        <label for="dateFormat">Date Format</label>
        <select name="dateFormat" id="dateFormat">
          <option value="0" )rawliteral" + String(settings.dateFormat == 0 ? "selected" : "") + R"rawliteral(>DD/MM/YYYY</option>
          <option value="1" )rawliteral" + String(settings.dateFormat == 1 ? "selected" : "") + R"rawliteral(>MM/DD/YYYY</option>
          <option value="2" )rawliteral" + String(settings.dateFormat == 2 ? "selected" : "") + R"rawliteral(>YYYY-MM-DD</option>
        </select>
        </div>
      </div>

      <!-- Display Performance Section -->
      <div class="section-header" onclick="toggleSection('displayPerfSection')">
        <h3>&#9889; Display Performance</h3>
        <span class="section-arrow">&#9660;</span>
      </div>
      <div id="displayPerfSection" class="section-content collapsed">
        <div class="card">

        <label for="colonBlinkMode">Clock Colon Display</label>
        <select name="colonBlinkMode" id="colonBlinkMode">
          <option value="0" )rawliteral" + String(settings.colonBlinkMode == 0 ? "selected" : "") + R"rawliteral(>Always On (Static)</option>
          <option value="1" )rawliteral" + String(settings.colonBlinkMode == 1 ? "selected" : "") + R"rawliteral(>Blinking (Recommended)</option>
          <option value="2" )rawliteral" + String(settings.colonBlinkMode == 2 ? "selected" : "") + R"rawliteral(>Always Off (Hidden)</option>
        </select>

        <label for="colonBlinkRate">Blink Rate (Hz)</label>
        <input type="range" name="colonBlinkRate" id="colonBlinkRate"
               min="5" max="50" step="5"
               value=")rawliteral" + String(settings.colonBlinkRate) + R"rawliteral("
               oninput="document.getElementById('blinkRateValue').textContent = (this.value / 10).toFixed(1)">
        <span style="color: #3b82f6; font-size: 14px; margin-left: 10px;">
          <span id="blinkRateValue">)rawliteral" + String(settings.colonBlinkRate / 10.0, 1) + R"rawliteral(</span> Hz
        </span>
        <p style="color: #888; font-size: 12px; margin-top: 5px;">
          Blink frequency for clock colon. Higher values blink faster. 1.0 Hz (once per second) recommended.
        </p>

        <label for="refreshRateMode" style="margin-top: 15px;">Refresh Rate Mode</label>
        <select name="refreshRateMode" id="refreshRateMode" onchange="toggleRefreshRateFields()">
          <option value="0" )rawliteral" + String(settings.refreshRateMode == 0 ? "selected" : "") + R"rawliteral(>Auto (Recommended)</option>
          <option value="1" )rawliteral" + String(settings.refreshRateMode == 1 ? "selected" : "") + R"rawliteral(>Manual</option>
        </select>

        <div id="refreshRateFields" style="display: )rawliteral" + String(settings.refreshRateMode == 1 ? "block" : "none") + R"rawliteral(;">
          <label for="refreshRateHz">Manual Refresh Rate (Hz)</label>
          <input type="range" name="refreshRateHz" id="refreshRateHz"
                 min="1" max="60" step="1"
                 value=")rawliteral" + String(settings.refreshRateHz) + R"rawliteral("
                 oninput="document.getElementById('refreshRateValue').textContent = this.value">
          <span style="color: #3b82f6; font-size: 14px; margin-left: 10px;">
            <span id="refreshRateValue">)rawliteral" + String(settings.refreshRateHz) + R"rawliteral(</span> Hz
          </span>
          <p style="color: #888; font-size: 12px; margin-top: 5px;">
            Display updates per second. Higher = smoother but more power usage. Range: 1-60 Hz.
          </p>
        </div>

        <div style="margin-top: 15px;">
          <label style="display: flex; align-items: center; cursor: pointer;">
            <input type="checkbox" name="boostAnim" id="boostAnim" style="margin-right: 10px;" )rawliteral" + String(settings.boostAnimationRefresh ? "checked" : "") + R"rawliteral(>
            <span style="font-size: 14px;">
              <strong>Enable Smooth Animations</strong> (Boost refresh during action)
            </span>
          </label>
          <p style="color: #888; font-size: 12px; margin-top: 5px; margin-left: 30px;">
            Temporarily increases refresh rate to 40 Hz when animations are active (Mario bouncing, invader shooting, explosions).
            Returns to normal rate when idle. Provides silky-smooth motion during action while maintaining power efficiency.
          </p>
        </div>

        <div style="margin-top: 15px; padding: 10px; background: #0f172a; border-radius: 5px; border-left: 3px solid #3b82f6;">
          <p style="color: #93c5fd; font-size: 12px; margin: 0;">
            <strong>&#128161; Auto Mode:</strong> Adapts refresh rate based on content.<br>
            • Static Clocks: 2 Hz (saves power)<br>
            • Idle Animations: 20 Hz (character movement)<br>
            • Active Animations: 40 Hz (with boost enabled, during bounces/explosions)<br>
            • PC Metrics: 10 Hz (balanced)<br>
            <br>
            <strong>Benefits:</strong> Blinking colon extends OLED life 2×. Dynamic refresh rates balance smoothness with power efficiency.
          </p>
        </div>

        <p style="color: #fbbf24; font-size: 12px; margin-top: 10px; background: #0f172a; padding: 10px; border-radius: 5px; border-left: 3px solid #fbbf24;">
          <strong>&#9888; Note:</strong> Very low refresh rates (&lt;5 Hz) may cause visible flicker. Very high rates (&gt;30 Hz) increase heat and power consumption with minimal visual benefit.
        </p>
        </div>
      </div>

      <!-- Timezone Section -->
      <div class="section-header" onclick="toggleSection('timezoneSection')">
        <h3>&#127760; Timezone</h3>
        <span class="section-arrow">&#9660;</span>
      </div>
      <div id="timezoneSection" class="section-content collapsed">
        <div class="card">

        <label for="gmtOffset">GMT Offset</label>
        <select name="gmtOffset" id="gmtOffset">
)rawliteral";

  // Generate timezone options (30-minute increments)
  for (int minutes = -720; minutes <= 840; minutes += 30) {
    String selected = (settings.gmtOffset == minutes) ? "selected" : "";

    // Format: GMT+5:30, GMT-5:00, etc.
    int hours = minutes / 60;
    int mins = abs(minutes % 60);
    String label = "GMT" + String(hours >= 0 ? "+" : "") + String(hours);
    if (mins > 0) {
      String minStr = (mins < 10) ? "0" + String(mins) : String(mins);
      label += ":" + minStr;
    } else {
      label += ":00";
    }

    html += "<option value=\"" + String(minutes) + "\" " + selected + ">" + label + "</option>\n";
  }

  html += R"rawliteral(
        </select>

        <label for="dst">Daylight Saving Time</label>
        <select name="dst" id="dst">
          <option value="1" )rawliteral" + String(settings.daylightSaving ? "selected" : "") + R"rawliteral(>Enabled (+1 hour)</option>
          <option value="0" )rawliteral" + String(!settings.daylightSaving ? "selected" : "") + R"rawliteral(>Disabled</option>
        </select>
        </div>
      </div>

      <!-- Network Configuration Section -->
      <div class="section-header" onclick="toggleSection('networkSection')">
        <h3>&#127760; Network Configuration</h3>
        <span class="section-arrow">&#9660;</span>
      </div>
      <div id="networkSection" class="section-content collapsed">
        <div class="card">

        <label for="useStaticIP">IP Address Mode</label>
        <select name="useStaticIP" id="useStaticIP" onchange="toggleStaticIPFields()">
          <option value="0" )rawliteral" + String(!settings.useStaticIP ? "selected" : "") + R"rawliteral(>DHCP (Automatic)</option>
          <option value="1" )rawliteral" + String(settings.useStaticIP ? "selected" : "") + R"rawliteral(>Static IP</option>
        </select>

        <div id="staticIPFields" style="display: )rawliteral" + String(settings.useStaticIP ? "block" : "none") + R"rawliteral(;">
          <label for="staticIP" style="margin-top: 15px;">Static IP Address</label>
          <input type="text" name="staticIP" id="staticIP" value=")rawliteral" + String(settings.staticIP) + R"rawliteral(" placeholder="192.168.1.100" pattern="^(?:[0-9]{1,3}\.){3}[0-9]{1,3}$">

          <label for="gateway">Gateway</label>
          <input type="text" name="gateway" id="gateway" value=")rawliteral" + String(settings.gateway) + R"rawliteral(" placeholder="192.168.1.1" pattern="^(?:[0-9]{1,3}\.){3}[0-9]{1,3}$">

          <label for="subnet">Subnet Mask</label>
          <input type="text" name="subnet" id="subnet" value=")rawliteral" + String(settings.subnet) + R"rawliteral(" placeholder="255.255.255.0" pattern="^(?:[0-9]{1,3}\.){3}[0-9]{1,3}$">

          <label for="dns1">Primary DNS</label>
          <input type="text" name="dns1" id="dns1" value=")rawliteral" + String(settings.dns1) + R"rawliteral(" placeholder="8.8.8.8" pattern="^(?:[0-9]{1,3}\.){3}[0-9]{1,3}$">

          <label for="dns2">Secondary DNS</label>
          <input type="text" name="dns2" id="dns2" value=")rawliteral" + String(settings.dns2) + R"rawliteral(" placeholder="8.8.4.4" pattern="^(?:[0-9]{1,3}\.){3}[0-9]{1,3}$">
        </div>

        <p style="color: #888; font-size: 12px; margin-top: 15px; background: #0f172a; padding: 10px; border-radius: 5px; border-left: 3px solid #fbbf24;">
          <strong>&#9888; Warning:</strong> Changing to Static IP will require a device restart. Make sure the IP address does not conflict with other devices on your network.
        </p>
        </div>
      </div>

      <!-- Display Layout Section -->
      <div class="section-header" onclick="toggleSection('layoutSection')">
        <h3>&#128202; Display Layout (PC Monitor only)</h3>
        <span class="section-arrow">&#9660;</span>
      </div>
      <div id="layoutSection" class="section-content collapsed">
        <div class="card">
        <label for="clockPosition">Clock Position</label>
        <select name="clockPosition" id="clockPosition">
          <option value="0" )rawliteral" + String(settings.clockPosition == 0 ? "selected" : "") + R"rawliteral(>Center (Top)</option>
          <option value="1" )rawliteral" + String(settings.clockPosition == 1 ? "selected" : "") + R"rawliteral(>Left Column (Row 1)</option>
          <option value="2" )rawliteral" + String(settings.clockPosition == 2 ? "selected" : "") + R"rawliteral(>Right Column (Row 1)</option>
        </select>

        <label for="clockOffset" style="margin-top: 15px; display: block;">Clock Offset (pixels)</label>
        <input type="number" name="clockOffset" id="clockOffset" value=")rawliteral" + String(settings.clockOffset) + R"rawliteral(" min="-20" max="20" style="width: 100%; padding: 8px; box-sizing: border-box;">

        <p style="color: #888; font-size: 12px; margin-top: 10px;">
          Position clock to optimize space for metrics. Use offset to fine-tune horizontal position (-20 to +20 pixels).
        </p>

        <div style="display: flex; align-items: center; margin-top: 15px;">
          <input type="checkbox" name="showClock" id="showClock" value="1" )rawliteral" + String(settings.showClock ? "checked" : "") + R"rawliteral( style="width: 20px; margin: 0;">
          <label for="showClock" style="margin: 0 0 0 10px; text-align: left; color: #00d4ff;">Show Clock/Time in metrics display</label>
        </div>

        <hr style="margin: 20px 0; border: none; border-top: 1px solid #333;">

        <label for="rowMode">Display Row Mode</label>
        <select name="rowMode" id="rowMode" onchange="updateRowMode()">
          <option value="0" )rawliteral" + String(settings.displayRowMode == 0 ? "selected" : "") + R"rawliteral(>5 Rows (13px spacing - optimized)</option>
          <option value="1" )rawliteral" + String(settings.displayRowMode == 1 ? "selected" : "") + R"rawliteral(>6 Rows (10px spacing - compact)</option>
        </select>

        <p style="color: #888; font-size: 12px; margin-top: 10px;">
          5-row mode provides maximum readability with optimized 13px spacing (11px with centered clock). 6-row mode fits more metrics. Row 6 positions (10-11) are hidden in 5-row mode.
        </p>

        <div style="margin-top: 20px;">
          <label>
            <input type="checkbox" name="rpmKFormat" id="rpmKFormat" )rawliteral" + String(settings.useRpmKFormat ? "checked" : "") + R"rawliteral(>
            Use K-format for RPM values (e.g., 1.8K instead of 1800RPM)
          </label>
          <p style="color: #888; font-size: 12px; margin-top: 5px;">
            Applies to all fan and pump speed metrics with RPM unit.
          </p>
        </div>

        <div style="margin-top: 20px;">
          <label>
            <input type="checkbox" name="netMBFormat" id="netMBFormat" )rawliteral" + String(settings.useNetworkMBFormat ? "checked" : "") + R"rawliteral(>
            Use M-format for network speeds (e.g., 1.2M instead of 1200KB/s)
          </label>
          <p style="color: #888; font-size: 12px; margin-top: 5px;">
            Applies to all network speed metrics with KB/s unit.
          </p>
        </div>
        </div>
      </div>

      <!-- Visible Metrics Section -->
      <div class="section-header" onclick="toggleSection('metricsSection')">
        <h3>&#128195; Visible Metrics (PC Monitor only)</h3>
        <span class="section-arrow">&#9660;</span>
      </div>
      <div id="metricsSection" class="section-content collapsed">
        <div class="card">
        <p style="color: #888; font-size: 14px; margin-top: 0; text-align: left;">
          Select which metrics to show on OLED
        </p>

        <p style="color: #888; font-size: 12px; margin-top: 10px; background: #0f172a; padding: 10px; border-radius: 5px; border-left: 3px solid #00d4ff;">
          <strong>&#128161; Tip:</strong> Use <code style="background: #1e293b; padding: 2px 6px; border-radius: 3px;">^</code> character for spacing.<br>
          Example: <code style="background: #1e293b; padding: 2px 6px; border-radius: 3px;">CPU^^</code> displays as <code style="background: #1e293b; padding: 2px 6px; border-radius: 3px;">CPU:  45C</code> (2 spaces after colon)
        </p>

        <div id="metricsContainer">
          <p style="color: #888;">Loading metrics...</p>
        </div>

        <p style="color: #888; font-size: 12px; margin-top: 15px;">
          <strong>Note:</strong> Metrics are configured in Python script.<br>
          Select up to 20 in pc_stats_monitor_v2.py (use companion metrics to fit more)
        </p>

        <script>
          let metricsData = [];
          let MAX_ROWS = )rawliteral" + String((settings.displayRowMode == 0) ? 5 : 6) + R"rawliteral(;  // Dynamic based on row mode

          function saveFormState() {
            // Save all form values before re-rendering
            metricsData.forEach(metric => {
              // Save label value
              const labelInput = document.querySelector(`input[name="label_${metric.id}"]`);
              if (labelInput) {
                metric.label = labelInput.value;
              }

              // Save position value
              const posDropdown = document.getElementById('pos_' + metric.id);
              if (posDropdown) {
                metric.position = parseInt(posDropdown.value);
              }

              // Save companion value
              const compDropdown = document.getElementById('comp_' + metric.id);
              if (compDropdown) {
                metric.companionId = parseInt(compDropdown.value);
              }

              // Save progress bar settings
              const barPosDropdown = document.getElementById('barPos_' + metric.id);
              if (barPosDropdown) {
                metric.barPosition = parseInt(barPosDropdown.value);
              }

              const barMinInput = document.querySelector(`input[name="barMin_${metric.id}"]`);
              if (barMinInput) {
                metric.barMin = parseInt(barMinInput.value) || 0;
              }

              const barMaxInput = document.querySelector(`input[name="barMax_${metric.id}"]`);
              if (barMaxInput) {
                metric.barMax = parseInt(barMaxInput.value) || 100;
              }

              const barWidthInput = document.querySelector(`input[name="barWidth_${metric.id}"]`);
              if (barWidthInput) {
                metric.barWidth = parseInt(barWidthInput.value) || 60;
              }

              const barOffsetInput = document.querySelector(`input[name="barOffset_${metric.id}"]`);
              if (barOffsetInput) {
                metric.barOffsetX = parseInt(barOffsetInput.value) || 0;
              }
            });
          }

          function updatePosition(metricId) {
            saveFormState();  // Save all changes before re-rendering
            renderMetrics();
          }

          function updateCompanion(metricId) {
            saveFormState();  // Save all changes before re-rendering
            renderMetrics();
          }

          function updateRowMode() {
            const rowMode = parseInt(document.getElementById('rowMode').value);
            const oldMaxRows = MAX_ROWS;
            MAX_ROWS = (rowMode === 0) ? 5 : 6;

            // Warn if switching to 5-row mode with metrics on Row 6
            if (MAX_ROWS === 5 && oldMaxRows === 6) {
              const row6Metrics = metricsData.filter(m =>
                m.position === 10 || m.position === 11 ||
                m.barPosition === 10 || m.barPosition === 11
              );
              if (row6Metrics.length > 0) {
                const metricNames = row6Metrics.map(m => m.name).join(', ');
                if (!confirm(`Warning: ${row6Metrics.length} metric(s) on Row 6 (${metricNames}) will be hidden. Continue?`)) {
                  document.getElementById('rowMode').value = '1';
                  MAX_ROWS = 6;
                  return;
                }
              }
            }

            // Hide Row 6 metrics when switching to 5-row mode
            if (MAX_ROWS === 5) {
              metricsData.forEach(metric => {
                if (metric.position === 10 || metric.position === 11) {
                  metric.position = 255;
                }
                if (metric.barPosition === 10 || metric.barPosition === 11) {
                  metric.barPosition = 255;
                }
              });
            }

            renderMetrics();
          }

          function renderMetrics() {
            const container = document.getElementById('metricsContainer');
            container.innerHTML = '';

            // Sort metrics by displayOrder
            const sortedMetrics = [...metricsData].sort((a, b) => a.displayOrder - b.displayOrder);

            // Header explaining the layout
            const header = document.createElement('div');
            header.style.cssText = 'background: #1e293b; padding: 12px; border-radius: 6px; margin-bottom: 15px; border: 2px solid #00d4ff;';
            header.innerHTML = `
              <div style="color: #00d4ff; font-weight: bold; font-size: 14px; margin-bottom: 5px;">&#128247; OLED Display Preview (` + MAX_ROWS + ` Rows Max)</div>
              <div style="color: #888; font-size: 12px;">Assign each metric to a specific position using the dropdown</div>
            `;
            container.appendChild(header);

            // Build 6 rows
            for (let rowIndex = 0; rowIndex < MAX_ROWS; rowIndex++) {
              // Find metrics for this row
              const leftPos = rowIndex * 2;      // 0, 2, 4, 6, 8, 10
              const rightPos = rowIndex * 2 + 1; // 1, 3, 5, 7, 9, 11

              const leftMetric = sortedMetrics.find(m => m.position === leftPos) || null;
              const rightMetric = sortedMetrics.find(m => m.position === rightPos) || null;

              const rowDiv = document.createElement('div');
              rowDiv.style.cssText = 'background: #0f172a; border: 1px solid #334155; border-radius: 6px; margin-bottom: 10px; overflow: hidden;';

              // Row header
              const rowHeader = document.createElement('div');
              rowHeader.style.cssText = 'background: #1e293b; padding: 6px 10px; color: #00d4ff; font-weight: bold; font-size: 12px; border-bottom: 1px solid #334155;';
              rowHeader.textContent = `Row ${rowIndex + 1}`;
              rowDiv.appendChild(rowHeader);

              // Row content - two columns
              const rowContent = document.createElement('div');
              rowContent.style.cssText = 'display: grid; grid-template-columns: 1fr 1fr; gap: 1px; background: #334155;';

              // Left slot
              const leftSlot = createMetricSlot(leftMetric, 'left', leftPos);
              rowContent.appendChild(leftSlot);

              // Right slot
              const rightSlot = createMetricSlot(rightMetric, 'right', rightPos);
              rowContent.appendChild(rightSlot);

              rowDiv.appendChild(rowContent);
              container.appendChild(rowDiv);
            }

            // Metrics list (for configuration)
            const metricsListDiv = document.createElement('div');
            metricsListDiv.style.cssText = 'background: #1e293b; border: 1px solid #334155; border-radius: 6px; padding: 15px; margin-top: 20px;';
            metricsListDiv.innerHTML = '<div style="color: #00d4ff; font-weight: bold; font-size: 14px; margin-bottom: 10px;">&#9881; All Metrics Configuration</div>';

            sortedMetrics.forEach(metric => {
              const metricDiv = createMetricConfig(metric);
              metricsListDiv.appendChild(metricDiv);
            });

            container.appendChild(metricsListDiv);
          }

          function createMetricSlot(metric, side, position) {
            const slot = document.createElement('div');
            slot.style.cssText = 'background: #0f172a; padding: 15px; min-height: 60px;';

            if (!metric) {
              // Empty slot
              slot.innerHTML = `
                <div style="color: #555; font-size: 12px; text-align: center; padding: 10px;">
                  ${side === 'left' ? '&#8592;' : '&#8594;'} Empty<br>
                  <span style="font-size: 10px;">No metric assigned</span>
                </div>
              `;
              return slot;
            }

            const companionName = metric.companionId > 0 ?
              (metricsData.find(m => m.id === metric.companionId)?.name || 'Unknown') : 'None';

            slot.innerHTML = `
              <div style="margin-bottom: 4px;">
                <div style="color: #00d4ff; font-weight: bold; font-size: 13px; margin-bottom: 2px;">
                  ${metric.name}
                </div>
                <div style="color: #888; font-size: 10px;">
                  Label: ${metric.label || metric.name}
                </div>
                ${metric.companionId > 0 ?
                  `<div style="color: #888; font-size: 10px;">Paired with: ${companionName}</div>` :
                  ''}
              </div>
            `;

            return slot;
          }

          function createMetricConfig(metric) {
            const div = document.createElement('div');
            div.style.cssText = 'background: #0f172a; padding: 12px; border-radius: 6px; margin-bottom: 8px; border: 1px solid #334155;';

            // Build position dropdown options
            let positionOptions = '<option value="255">None (Hidden)</option>';
            for (let row = 0; row < MAX_ROWS; row++) {
              const leftPos = row * 2;
              const rightPos = row * 2 + 1;
              positionOptions += `<option value="${leftPos}" ${metric.position === leftPos ? 'selected' : ''}>Row ${row + 1} - &#8592; Left</option>`;
              positionOptions += `<option value="${rightPos}" ${metric.position === rightPos ? 'selected' : ''}>Row ${row + 1} - Right &#8594;</option>`;
            }

            // Build bar position dropdown options (same as position but for progress bar placement)
            let barPositionOptions = '<option value="255">None</option>';
            for (let row = 0; row < MAX_ROWS; row++) {
              const leftPos = row * 2;
              const rightPos = row * 2 + 1;
              barPositionOptions += `<option value="${leftPos}" ${metric.barPosition === leftPos ? 'selected' : ''}>Row ${row + 1} - &#8592; Left</option>`;
              barPositionOptions += `<option value="${rightPos}" ${metric.barPosition === rightPos ? 'selected' : ''}>Row ${row + 1} - Right &#8594;</option>`;
            }

            // Build companion dropdown options
            let companionOptions = '<option value="0">None</option>';
            metricsData.forEach(m => {
              if (m.id !== metric.id) {
                const selected = (metric.companionId === m.id) ? 'selected' : '';
                companionOptions += `<option value="${m.id}" ${selected}>${m.name} (${m.unit})</option>`;
              }
            });

            div.innerHTML = `
              <div style="margin-bottom: 8px;">
                <div style="color: #00d4ff; font-weight: bold; font-size: 13px;">
                  ${metric.name} (${metric.unit})
                </div>
              </div>
              <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 10px;">
                <div>
                  <label style="color: #888; font-size: 10px; display: block; margin-bottom: 3px;">Position:</label>
                  <select name="position_${metric.id}" id="pos_${metric.id}" onchange="updatePosition(${metric.id})"
                          style="width: 100%; padding: 6px; background: #16213e; border: 1px solid #334155;
                                 color: #eee; border-radius: 3px; font-size: 11px;">
                    ${positionOptions}
                  </select>
                </div>
                <div>
                  <label style="color: #888; font-size: 10px; display: block; margin-bottom: 3px;">Pair with:</label>
                  <select name="companion_${metric.id}" id="comp_${metric.id}" onchange="updateCompanion(${metric.id})"
                          style="width: 100%; padding: 6px; background: #16213e; border: 1px solid #334155;
                                 color: #eee; border-radius: 3px; font-size: 11px;">
                    ${companionOptions}
                  </select>
                </div>
              </div>
              <div style="margin-top: 8px;">
                <label style="color: #888; font-size: 10px; display: block; margin-bottom: 3px;">Custom Label (10 chars max):</label>
                <input type="text" name="label_${metric.id}"
                       value="${metric.label}" maxlength="10" placeholder="${metric.name}"
                       style="width: 100%; padding: 6px; background: #16213e; border: 1px solid #334155;
                              color: #eee; border-radius: 3px; font-size: 11px; box-sizing: border-box;">
              </div>
              <div style="margin-top: 10px; padding-top: 8px; border-top: 1px solid #334155;">
                <label style="color: #888; font-size: 10px; display: block; margin-bottom: 3px;">Progress Bar Position:</label>
                <select name="barPosition_${metric.id}" id="barPos_${metric.id}"
                        style="width: 100%; padding: 6px; background: #16213e; border: 1px solid #334155;
                               color: #eee; border-radius: 3px; font-size: 11px; margin-bottom: 8px;">
                  ${barPositionOptions}
                </select>
                <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 10px;">
                  <div>
                    <label style="color: #888; font-size: 9px; display: block; margin-bottom: 2px;">Min Value:</label>
                    <input type="number" name="barMin_${metric.id}" value="${metric.barMin || 0}"
                           style="width: 100%; padding: 4px; background: #16213e; border: 1px solid #334155;
                                  color: #eee; border-radius: 3px; font-size: 10px; box-sizing: border-box;">
                  </div>
                  <div>
                    <label style="color: #888; font-size: 9px; display: block; margin-bottom: 2px;">Max Value:</label>
                    <input type="number" name="barMax_${metric.id}" value="${metric.barMax || 100}"
                           style="width: 100%; padding: 4px; background: #16213e; border: 1px solid #334155;
                                  color: #eee; border-radius: 3px; font-size: 10px; box-sizing: border-box;">
                  </div>
                </div>
                <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 10px; margin-top: 8px;">
                  <div>
                    <label style="color: #888; font-size: 9px; display: block; margin-bottom: 2px;">Width (px):</label>
                    <input type="number" name="barWidth_${metric.id}" value="${metric.barWidth || 60}" min="10" max="64"
                           style="width: 100%; padding: 4px; background: #16213e; border: 1px solid #334155;
                                  color: #eee; border-radius: 3px; font-size: 10px; box-sizing: border-box;">
                  </div>
                  <div>
                    <label style="color: #888; font-size: 9px; display: block; margin-bottom: 2px;">Offset X (px):</label>
                    <input type="number" name="barOffset_${metric.id}" value="${metric.barOffsetX || 0}" min="0" max="54"
                           style="width: 100%; padding: 4px; background: #16213e; border: 1px solid #334155;
                                  color: #eee; border-radius: 3px; font-size: 10px; box-sizing: border-box;">
                  </div>
                </div>
              </div>
              <input type="hidden" name="order_${metric.id}" value="${metric.displayOrder}">
            `;

            return div;
          }

          // Load metrics on page load
          fetch('/metrics')
            .then(res => res.json())
            .then(data => {
              if (data.metrics && data.metrics.length > 0) {
                metricsData = data.metrics;
                renderMetrics();
              } else {
                document.getElementById('metricsContainer').innerHTML =
                  '<p style="color: #ff6666;">No metrics received yet. Start Python script.</p>';
              }
            })
            .catch(err => {
              document.getElementById('metricsContainer').innerHTML =
                '<p style="color: #ff6666;">Error loading metrics</p>';
            });
        </script>
        </div>
      </div>
    </form>

    <!-- Firmware Update Section (Outside main form) -->
    <div class="section-header" onclick="toggleSection('firmwareSection')">
      <h3>&#128190; Firmware Update</h3>
      <span class="section-arrow">&#9660;</span>
    </div>
    <div id="firmwareSection" class="section-content collapsed">
      <div class="card">
      <p style="color: #888; font-size: 14px; margin-top: 0;">
        Upload new firmware (.bin file) to update the device
      </p>

      <form id="uploadForm" method="POST" action="/update" enctype="multipart/form-data" style="margin-top: 15px;">
        <input type="file" id="firmwareFile" name="firmware" accept=".bin" style="width: 100%; padding: 10px; margin-bottom: 10px; background: #16213e; border: 1px solid #334155; color: #eee; border-radius: 5px;">

        <button type="submit" style="width: 100%; padding: 14px; background: linear-gradient(135deg, #f59e0b 0%, #d97706 100%); color: #fff; border: none; border-radius: 8px; font-size: 16px; font-weight: 600; cursor: pointer; margin-top: 10px;">
          &#128190; Upload & Update Firmware
        </button>
      </form>

      <div id="uploadProgress" style="display: none; margin-top: 15px;">
        <div style="background: #1e293b; border-radius: 8px; overflow: hidden; height: 30px; margin-bottom: 10px;">
          <div id="progressBar" style="background: linear-gradient(135deg, #00d4ff 0%, #0096ff 100%); height: 100%; width: 0%; transition: width 0.3s; display: flex; align-items: center; justify-content: center; color: #0f0c29; font-weight: bold; font-size: 14px;">
            0%
          </div>
        </div>
        <p id="uploadStatus" style="text-align: center; color: #00d4ff; font-size: 14px;">Uploading...</p>
      </div>

      <p style="color: #888; font-size: 12px; margin-top: 15px; background: #0f172a; padding: 10px; border-radius: 5px; border-left: 3px solid #ef4444;">
        <strong>&#9888; Warning:</strong> Do not disconnect power during firmware update! Device will restart automatically after update completes.
      </p>
      </div>
    </div>

    <form action="/reset" method="GET" onsubmit="return confirm('Reset WiFi settings? Device will restart in AP mode.');">
      <button type="submit" class="reset-btn">&#128260; Reset WiFi Settings</button>
    </form>

    <div class="info">
      PC Stats Monitor v2.0<br>
      Configure Python script with IP shown above
    </div>
  </div>

  <!-- Sticky Save Button -->
  <div class="sticky-save">
    <div class="container">
      <button type="button" class="save-btn" onclick="saveSettings()">&#128190; Save Settings</button>
      <span id="saveMessage" style="margin-left: 15px; color: #4CAF50; font-weight: bold; display: none;">&#10004; Settings Saved!</span>
    </div>
  </div>

  <script>
    // Collapsible section toggle
    function toggleSection(sectionId) {
      const content = document.getElementById(sectionId);
      const arrow = event.currentTarget.querySelector('.section-arrow');
      content.classList.toggle('collapsed');
      arrow.classList.toggle('collapsed');

      // Save expanded sections to localStorage
      const isCollapsed = content.classList.contains('collapsed');
      if (!isCollapsed) {
        localStorage.setItem('lastExpandedSection', sectionId);
      }
    }

    // Toggle static IP fields visibility
    function toggleStaticIPFields() {
      const useStaticIP = document.getElementById('useStaticIP').value === '1';
      const staticIPFields = document.getElementById('staticIPFields');
      staticIPFields.style.display = useStaticIP ? 'block' : 'none';
    }

    // Toggle refresh rate fields visibility
    function toggleRefreshRateFields() {
      const refreshRateMode = document.getElementById('refreshRateMode').value === '1';
      const refreshRateFields = document.getElementById('refreshRateFields');
      refreshRateFields.style.display = refreshRateMode ? 'block' : 'none';
    }

    // Toggle Mario settings visibility
    function toggleMarioSettings() {
      const clockStyle = document.getElementById('clockStyle').value;
      const marioSettings = document.getElementById('marioSettings');
      const pongSettings = document.getElementById('pongSettings');
      const pacmanSettings = document.getElementById('pacmanSettings');
      const spaceSettings = document.getElementById('spaceSettings');
      marioSettings.style.display = (clockStyle === '0') ? 'block' : 'none';
      pongSettings.style.display = (clockStyle === '5') ? 'block' : 'none';
      pacmanSettings.style.display = (clockStyle === '6') ? 'block' : 'none';
      spaceSettings.style.display = (clockStyle === '3' || clockStyle === '4') ? 'block' : 'none';
    }

    // Export configuration
    function exportConfig() {
      fetch('/api/export')
        .then(response => response.json())
        .then(data => {
          const blob = new Blob([JSON.stringify(data, null, 2)], { type: 'application/json' });
          const url = URL.createObjectURL(blob);
          const a = document.createElement('a');
          a.href = url;
          a.download = 'pc-monitor-config.json';
          document.body.appendChild(a);
          a.click();
          document.body.removeChild(a);
          URL.revokeObjectURL(url);
          alert('Configuration exported successfully!');
        })
        .catch(err => alert('Error exporting configuration: ' + err));
    }

    // Import configuration
    function importConfig(event) {
      const file = event.target.files[0];
      if (!file) return;

      const reader = new FileReader();
      reader.onload = function(e) {
        try {
          const config = JSON.parse(e.target.result);

          // Send to server
          fetch('/api/import', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(config)
          })
          .then(response => response.json())
          .then(data => {
            if (data.success) {
              alert('Configuration imported successfully! Reloading page...');
              location.reload();
            } else {
              alert('Error importing configuration: ' + data.message);
            }
          })
          .catch(err => alert('Error importing configuration: ' + err));
        } catch (err) {
          alert('Invalid configuration file: ' + err);
        }
      };
      reader.readAsText(file);
    }

    // Save settings via AJAX (no page reload)
    function saveSettings() {
      const form = document.querySelector('form[action="/save"]');
      const formData = new FormData(form);
      const saveMessage = document.getElementById('saveMessage');
      const saveBtn = document.querySelector('.save-btn');

      // Convert FormData to URL-encoded format (ESP32 WebServer expects this)
      const urlEncoded = new URLSearchParams(formData);

      // Disable button during save
      saveBtn.disabled = true;
      saveBtn.textContent = '💾 Saving...';

      fetch('/save', {
        method: 'POST',
        headers: {
          'Content-Type': 'application/x-www-form-urlencoded'
        },
        body: urlEncoded
      })
      .then(response => response.json())
      .then(data => {
        if (data.success) {
          // Show success message
          saveMessage.style.display = 'inline';
          setTimeout(() => {
            saveMessage.style.display = 'none';
          }, 3000);

          // Re-enable button
          saveBtn.disabled = false;
          saveBtn.textContent = '💾 Save Settings';

          // If network settings changed, warn user and reload
          if (data.networkChanged) {
            alert('Network settings changed! Device is restarting. You may need to reconnect to the new IP address.');
            setTimeout(() => {
              window.location.href = '/';
            }, 3000);
          }
        } else {
          alert('Error saving settings');
          saveBtn.disabled = false;
          saveBtn.textContent = '💾 Save Settings';
        }
      })
      .catch(err => {
        alert('Error saving settings: ' + err);
        saveBtn.disabled = false;
        saveBtn.textContent = '💾 Save Settings';
      });
    }

    // Firmware upload handler
    document.getElementById('uploadForm').addEventListener('submit', function(e) {
      e.preventDefault();

      const fileInput = document.getElementById('firmwareFile');
      const file = fileInput.files[0];

      if (!file) {
        alert('Please select a firmware file (.bin)');
        return;
      }

      if (!file.name.endsWith('.bin')) {
        alert('Please select a valid .bin firmware file');
        return;
      }

      // Show progress
      document.getElementById('uploadProgress').style.display = 'block';
      document.querySelector('#uploadForm button').disabled = true;

      const xhr = new XMLHttpRequest();

      // Track upload progress
      xhr.upload.addEventListener('progress', function(e) {
        if (e.lengthComputable) {
          const percent = Math.round((e.loaded / e.total) * 100);
          document.getElementById('progressBar').style.width = percent + '%';
          document.getElementById('progressBar').textContent = percent + '%';
          document.getElementById('uploadStatus').textContent = 'Uploading: ' + percent + '%';
        }
      });

      // Handle completion
      xhr.addEventListener('load', function() {
        if (xhr.status === 200) {
          document.getElementById('progressBar').style.width = '100%';
          document.getElementById('progressBar').textContent = '100%';
          document.getElementById('uploadStatus').textContent = 'Update successful! Device is rebooting...';
          document.getElementById('uploadStatus').style.color = '#10b981';

          // Redirect after delay
          setTimeout(function() {
            window.location.href = '/';
          }, 8000);
        } else {
          document.getElementById('uploadStatus').textContent = 'Upload failed! Please try again.';
          document.getElementById('uploadStatus').style.color = '#ef4444';
          document.querySelector('#uploadForm button').disabled = false;
        }
      });

      // Handle errors
      xhr.addEventListener('error', function() {
        document.getElementById('uploadStatus').textContent = 'Upload error! Please try again.';
        document.getElementById('uploadStatus').style.color = '#ef4444';
        document.querySelector('#uploadForm button').disabled = false;
      });

      // Send the file
      const formData = new FormData();
      formData.append('firmware', file);

      xhr.open('POST', '/update');
      xhr.send(formData);
    });

    // On page load, restore previously expanded section
    window.addEventListener('DOMContentLoaded', function() {
      // Initialize toggle fields
      toggleStaticIPFields();
      toggleRefreshRateFields();

      // Restore last expanded section
      const lastExpandedSection = localStorage.getItem('lastExpandedSection');
      if (lastExpandedSection) {
        const content = document.getElementById(lastExpandedSection);
        const headers = document.querySelectorAll('.section-header');

        if (content) {
          // Find the corresponding header
          for (let header of headers) {
            if (header.getAttribute('onclick') && header.getAttribute('onclick').includes(lastExpandedSection)) {
              const arrow = header.querySelector('.section-arrow');
              content.classList.remove('collapsed');
              if (arrow) arrow.classList.remove('collapsed');
              break;
            }
          }
        }
      }
    });
  </script>
</body>
</html>
)rawliteral";

  server.send(200, "text/html", html);
}

void handleSave() {
  if (server.hasArg("clockStyle")) {
    settings.clockStyle = server.arg("clockStyle").toInt();
  }
  if (server.hasArg("gmtOffset")) {
    settings.gmtOffset = server.arg("gmtOffset").toInt();
  }
  if (server.hasArg("dst")) {
    settings.daylightSaving = server.arg("dst").toInt() == 1;
  }
  if (server.hasArg("use24Hour")) {
    settings.use24Hour = server.arg("use24Hour").toInt() == 1;
  }
  if (server.hasArg("dateFormat")) {
    settings.dateFormat = server.arg("dateFormat").toInt();
  }

  // Save clock position
  if (server.hasArg("clockPosition")) {
    settings.clockPosition = server.arg("clockPosition").toInt();
  }

  // Save clock offset
  if (server.hasArg("clockOffset")) {
    settings.clockOffset = server.arg("clockOffset").toInt();
  }

  // Save clock visibility checkbox
  settings.showClock = server.hasArg("showClock");

  // Save row mode selection
  if (server.hasArg("rowMode")) {
    settings.displayRowMode = server.arg("rowMode").toInt();
  }

  // Save RPM format preference
  settings.useRpmKFormat = server.hasArg("rpmKFormat");

  // Save network speed format preference
  settings.useNetworkMBFormat = server.hasArg("netMBFormat");

  // Save colon blink settings
  if (server.hasArg("colonBlinkMode")) {
    settings.colonBlinkMode = server.arg("colonBlinkMode").toInt();
  }
  if (server.hasArg("colonBlinkRate")) {
    settings.colonBlinkRate = server.arg("colonBlinkRate").toInt();
  }

  // Save refresh rate settings
  if (server.hasArg("refreshRateMode")) {
    settings.refreshRateMode = server.arg("refreshRateMode").toInt();
  }
  if (server.hasArg("refreshRateHz")) {
    settings.refreshRateHz = server.arg("refreshRateHz").toInt();
  }

  // Save animation boost checkbox
  settings.boostAnimationRefresh = server.hasArg("boostAnim");

  // Save Mario bounce settings
  if (server.hasArg("marioBounceHeight")) {
    settings.marioBounceHeight = server.arg("marioBounceHeight").toInt();
  }
  if (server.hasArg("marioBounceSpeed")) {
    settings.marioBounceSpeed = server.arg("marioBounceSpeed").toInt();
  }

  // Save Pong settings
  if (server.hasArg("pongBallSpeed")) {
    settings.pongBallSpeed = server.arg("pongBallSpeed").toInt();
  }
  if (server.hasArg("pongBounceStrength")) {
    settings.pongBounceStrength = server.arg("pongBounceStrength").toInt();
  }
  if (server.hasArg("pongBounceDamping")) {
    settings.pongBounceDamping = server.arg("pongBounceDamping").toInt();
  }
  if (server.hasArg("pongPaddleWidth")) {
    settings.pongPaddleWidth = server.arg("pongPaddleWidth").toInt();
  }

  // Save Pac-Man settings
  if (server.hasArg("pacmanSpeed")) {
    settings.pacmanSpeed = server.arg("pacmanSpeed").toInt();
  }
  if (server.hasArg("pacmanEatingSpeed")) {
    settings.pacmanEatingSpeed = server.arg("pacmanEatingSpeed").toInt();
  }
  if (server.hasArg("pacmanMouthSpeed")) {
    settings.pacmanMouthSpeed = server.arg("pacmanMouthSpeed").toInt();
  }
  if (server.hasArg("pacmanPelletCount")) {
    settings.pacmanPelletCount = server.arg("pacmanPelletCount").toInt();
  }
  settings.pacmanPelletRandomSpacing = server.hasArg("pacmanPelletRandomSpacing");
  settings.pacmanBounceEnabled = server.hasArg("pacmanBounceEnabled");

  // Save Space Clock settings
  if (server.hasArg("spaceCharacterType")) {
    settings.spaceCharacterType = server.arg("spaceCharacterType").toInt();
  }
  if (server.hasArg("spacePatrolSpeed")) {
    settings.spacePatrolSpeed = server.arg("spacePatrolSpeed").toInt();
  }
  if (server.hasArg("spaceAttackSpeed")) {
    settings.spaceAttackSpeed = server.arg("spaceAttackSpeed").toInt();
  }
  if (server.hasArg("spaceLaserSpeed")) {
    settings.spaceLaserSpeed = server.arg("spaceLaserSpeed").toInt();
  }
  if (server.hasArg("spaceExplosionGravity")) {
    settings.spaceExplosionGravity = server.arg("spaceExplosionGravity").toInt();
  }

  // Save network configuration
  bool previousStaticIPSetting = settings.useStaticIP;
  if (server.hasArg("useStaticIP")) {
    settings.useStaticIP = server.arg("useStaticIP").toInt() == 1;
  }
  if (server.hasArg("staticIP")) {
    String ipStr = server.arg("staticIP");
    if (ipStr.length() > 0 && validateIP(ipStr.c_str())) {
      safeCopyString(settings.staticIP, ipStr.c_str(), sizeof(settings.staticIP));
    } else if (ipStr.length() > 0) {
      Serial.println("WARNING: Invalid static IP format, ignoring");
    }
  }
  if (server.hasArg("gateway")) {
    String ipStr = server.arg("gateway");
    if (ipStr.length() > 0 && validateIP(ipStr.c_str())) {
      safeCopyString(settings.gateway, ipStr.c_str(), sizeof(settings.gateway));
    } else if (ipStr.length() > 0) {
      Serial.println("WARNING: Invalid gateway format, ignoring");
    }
  }
  if (server.hasArg("subnet")) {
    String ipStr = server.arg("subnet");
    if (ipStr.length() > 0 && validateIP(ipStr.c_str())) {
      safeCopyString(settings.subnet, ipStr.c_str(), sizeof(settings.subnet));
    } else if (ipStr.length() > 0) {
      Serial.println("WARNING: Invalid subnet format, ignoring");
    }
  }
  if (server.hasArg("dns1")) {
    String ipStr = server.arg("dns1");
    if (ipStr.length() > 0 && validateIP(ipStr.c_str())) {
      safeCopyString(settings.dns1, ipStr.c_str(), sizeof(settings.dns1));
    } else if (ipStr.length() > 0) {
      Serial.println("WARNING: Invalid DNS1 format, ignoring");
    }
  }
  if (server.hasArg("dns2")) {
    String ipStr = server.arg("dns2");
    if (ipStr.length() > 0 && validateIP(ipStr.c_str())) {
      safeCopyString(settings.dns2, ipStr.c_str(), sizeof(settings.dns2));
    } else if (ipStr.length() > 0) {
      Serial.println("WARNING: Invalid DNS2 format, ignoring");
    }
  }

  // Save custom labels
  for (int i = 0; i < MAX_METRICS; i++) {
    String labelArg = "label_" + String(i + 1);
    if (server.hasArg(labelArg)) {
      String label = server.arg(labelArg);
      label.trim();
      if (label.length() > 0) {
        strncpy(settings.metricLabels[i], label.c_str(), METRIC_NAME_LEN - 1);
        settings.metricLabels[i][METRIC_NAME_LEN - 1] = '\0';
      } else {
        settings.metricLabels[i][0] = '\0';  // Empty = use Python name
      }
    }
  }

  // Save metric display order
  for (int i = 0; i < MAX_METRICS; i++) {
    String orderArg = "order_" + String(i + 1);
    if (server.hasArg(orderArg)) {
      settings.metricOrder[i] = server.arg(orderArg).toInt();
    }
  }

  // Save metric companions
  for (int i = 0; i < MAX_METRICS; i++) {
    String companionArg = "companion_" + String(i + 1);
    if (server.hasArg(companionArg)) {
      settings.metricCompanions[i] = server.arg(companionArg).toInt();
    } else {
      settings.metricCompanions[i] = 0;  // No companion
    }
  }

  // Save metric positions
  for (int i = 0; i < MAX_METRICS; i++) {
    String positionArg = "position_" + String(i + 1);
    if (server.hasArg(positionArg)) {
      settings.metricPositions[i] = server.arg(positionArg).toInt();
    } else {
      settings.metricPositions[i] = 255;  // Default: None/Hidden
    }
  }

  // Save progress bar settings
  for (int i = 0; i < MAX_METRICS; i++) {
    String barPosArg = "barPosition_" + String(i + 1);
    String minArg = "barMin_" + String(i + 1);
    String maxArg = "barMax_" + String(i + 1);
    String widthArg = "barWidth_" + String(i + 1);
    String offsetArg = "barOffset_" + String(i + 1);

    if (server.hasArg(barPosArg)) {
      settings.metricBarPositions[i] = server.arg(barPosArg).toInt();
    } else {
      settings.metricBarPositions[i] = 255;  // Default: No bar
    }

    if (server.hasArg(minArg)) {
      settings.metricBarMin[i] = server.arg(minArg).toInt();
    }
    if (server.hasArg(maxArg)) {
      settings.metricBarMax[i] = server.arg(maxArg).toInt();
    }
    if (server.hasArg(widthArg)) {
      settings.metricBarWidths[i] = server.arg(widthArg).toInt();
    } else {
      settings.metricBarWidths[i] = 60;  // Default width
    }
    if (server.hasArg(offsetArg)) {
      settings.metricBarOffsets[i] = server.arg(offsetArg).toInt();
    } else {
      settings.metricBarOffsets[i] = 0;  // Default: no offset
    }
  }

  // Hide Row 6 metrics when switching to 5-row mode
  if (settings.displayRowMode == 0) {  // 5-row mode
    for (int i = 0; i < MAX_METRICS; i++) {
      if (settings.metricPositions[i] == 10 || settings.metricPositions[i] == 11) {
        settings.metricPositions[i] = 255;  // Hide
      }
      if (settings.metricBarPositions[i] == 10 || settings.metricBarPositions[i] == 11) {
        settings.metricBarPositions[i] = 255;  // Hide bars
      }
    }
  }

  // Apply labels, order, companions, positions, and progress bars to current metrics in memory
  for (int i = 0; i < metricData.count; i++) {
    Metric& m = metricData.metrics[i];
    if (m.id > 0 && m.id <= MAX_METRICS) {
      // Apply custom label if set
      if (settings.metricLabels[m.id - 1][0] != '\0') {
        strncpy(m.label, settings.metricLabels[m.id - 1], METRIC_NAME_LEN - 1);
        m.label[METRIC_NAME_LEN - 1] = '\0';
      } else {
        strncpy(m.label, m.name, METRIC_NAME_LEN - 1);
        m.label[METRIC_NAME_LEN - 1] = '\0';
      }

      // Apply display order
      m.displayOrder = settings.metricOrder[m.id - 1];

      // Apply companion metric
      m.companionId = settings.metricCompanions[m.id - 1];

      // Apply position assignment
      m.position = settings.metricPositions[m.id - 1];

      // Apply progress bar settings
      m.barPosition = settings.metricBarPositions[m.id - 1];
      m.barMin = settings.metricBarMin[m.id - 1];
      m.barMax = settings.metricBarMax[m.id - 1];
      m.barWidth = settings.metricBarWidths[m.id - 1];
      m.barOffsetX = settings.metricBarOffsets[m.id - 1];

      // Store/update the metric name for future validation
      strncpy(settings.metricNames[m.id - 1], m.name, METRIC_NAME_LEN - 1);
      settings.metricNames[m.id - 1][METRIC_NAME_LEN - 1] = '\0';
    }
  }

  // Validate settings bounds before saving
  assertBounds(settings.clockStyle, 0, 6, "clockStyle");
  assertBounds(settings.gmtOffset, -720, 840, "gmtOffset");  // -12h to +14h in minutes
  assertBounds(settings.clockPosition, 0, 2, "clockPosition");
  assertBounds(settings.displayRowMode, 0, 1, "displayRowMode");
  assertBounds(settings.colonBlinkMode, 0, 2, "colonBlinkMode");
  assertBounds(settings.colonBlinkRate, 5, 50, "colonBlinkRate");
  assertBounds(settings.refreshRateMode, 0, 1, "refreshRateMode");
  assertBounds(settings.refreshRateHz, 1, 60, "refreshRateHz");
  assertBounds(settings.marioBounceHeight, 10, 80, "marioBounceHeight");
  assertBounds(settings.marioBounceSpeed, 2, 15, "marioBounceSpeed");
  assertBounds(settings.pongBallSpeed, 16, 30, "pongBallSpeed");
  assertBounds(settings.pongBounceStrength, 1, 8, "pongBounceStrength");
  assertBounds(settings.pongBounceDamping, 50, 95, "pongBounceDamping");
  assertBounds(settings.pongPaddleWidth, 10, 40, "pongPaddleWidth");
  assertBounds(settings.pacmanSpeed, 5, 30, "pacmanSpeed");
  assertBounds(settings.pacmanEatingSpeed, 10, 50, "pacmanEatingSpeed");
  assertBounds(settings.pacmanMouthSpeed, 5, 20, "pacmanMouthSpeed");
  assertBounds(settings.pacmanPelletCount, 0, 20, "pacmanPelletCount");
  assertBounds(settings.spaceCharacterType, 0, 1, "spaceCharacterType");
  assertBounds(settings.spacePatrolSpeed, 2, 15, "spacePatrolSpeed");
  assertBounds(settings.spaceAttackSpeed, 10, 40, "spaceAttackSpeed");
  assertBounds(settings.spaceLaserSpeed, 20, 80, "spaceLaserSpeed");
  assertBounds(settings.spaceExplosionGravity, 3, 10, "spaceExplosionGravity");

  saveSettings();
  applyTimezone();
  ntpSynced = false;  // Force NTP resync after timezone change

  // Reset Mario animation state when switching modes
  mario_state = MARIO_IDLE;
  mario_x = -15;
  animation_triggered = false;
  time_overridden = false;
  last_minute = -1;

  // Reset Space clock animation state when switching modes
  space_state = SPACE_PATROL;
  space_x = 64;  // Center of screen (space_y is const at 56)

  // Reset Pong animation state when switching modes
  resetPongAnimation();

  // Reset Pac-Man animation state when switching modes
  pacman_state = PACMAN_PATROL;
  pacman_x = 30.0;
  pacman_y = PACMAN_PATROL_Y;
  pacman_direction = 1;
  pacman_animation_triggered = false;
  last_minute_pacman = -1;
  for (int i = 0; i < 5; i++) {
    digit_being_eaten[i] = false;
    digit_eaten_rows_left[i] = 0;
    digit_eaten_rows_right[i] = 0;
  }
  generatePellets();

  // Check if network settings changed - if so, restart is required
  bool networkChanged = (previousStaticIPSetting != settings.useStaticIP);

  // Return JSON response for AJAX
  String json = "{\"success\":true,\"networkChanged\":" + String(networkChanged ? "true" : "false") + "}";
  server.send(200, "application/json", json);

  // If network settings changed, restart after a delay
  if (networkChanged) {
    delay(1000);  // Give time for response to be sent
    Serial.println("Network settings changed, restarting...");
    ESP.restart();
  }
}

void handleReset() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>Resetting...</title>
  <style>
    body { font-family: Arial; background: #1a1a2e; color: #e94560; display: flex; justify-content: center; align-items: center; height: 100vh; margin: 0; }
    .msg { text-align: center; }
  </style>
</head>
<body>
  <div class="msg">
    <h1>&#128260;</h1>
    <p>Resetting WiFi settings...<br>Connect to "PCMonitor-Setup" to reconfigure.</p>
  </div>
</body>
</html>
)rawliteral";
  
  server.send(200, "text/html", html);
  delay(1000);
  
  wifiManager.resetSettings();
  ESP.restart();
}

// Export configuration as JSON
void handleExportConfig() {
  String json = "{";

  // Clock settings
  json += "\"clockStyle\":" + String(settings.clockStyle) + ",";
  json += "\"gmtOffset\":" + String(settings.gmtOffset) + ",";
  json += "\"daylightSaving\":" + String(settings.daylightSaving ? "true" : "false") + ",";
  json += "\"use24Hour\":" + String(settings.use24Hour ? "true" : "false") + ",";
  json += "\"dateFormat\":" + String(settings.dateFormat) + ",";
  json += "\"clockPosition\":" + String(settings.clockPosition) + ",";
  json += "\"clockOffset\":" + String(settings.clockOffset) + ",";
  json += "\"showClock\":" + String(settings.showClock ? "true" : "false") + ",";
  json += "\"displayRowMode\":" + String(settings.displayRowMode) + ",";
  json += "\"useRpmKFormat\":" + String(settings.useRpmKFormat ? "true" : "false") + ",";
  json += "\"useNetworkMBFormat\":" + String(settings.useNetworkMBFormat ? "true" : "false") + ",";

  // Metric labels
  json += "\"metricLabels\":[";
  for (int i = 0; i < MAX_METRICS; i++) {
    if (i > 0) json += ",";
    json += "\"" + String(settings.metricLabels[i]) + "\"";
  }
  json += "],";

  // Metric names
  json += "\"metricNames\":[";
  for (int i = 0; i < MAX_METRICS; i++) {
    if (i > 0) json += ",";
    json += "\"" + String(settings.metricNames[i]) + "\"";
  }
  json += "],";

  // Metric order
  json += "\"metricOrder\":[";
  for (int i = 0; i < MAX_METRICS; i++) {
    if (i > 0) json += ",";
    json += String(settings.metricOrder[i]);
  }
  json += "],";

  // Metric companions
  json += "\"metricCompanions\":[";
  for (int i = 0; i < MAX_METRICS; i++) {
    if (i > 0) json += ",";
    json += String(settings.metricCompanions[i]);
  }
  json += "],";

  // Metric positions
  json += "\"metricPositions\":[";
  for (int i = 0; i < MAX_METRICS; i++) {
    if (i > 0) json += ",";
    json += String(settings.metricPositions[i]);
  }
  json += "],";

  // Progress bar settings
  json += "\"metricBarPositions\":[";
  for (int i = 0; i < MAX_METRICS; i++) {
    if (i > 0) json += ",";
    json += String(settings.metricBarPositions[i]);
  }
  json += "],";

  json += "\"metricBarMin\":[";
  for (int i = 0; i < MAX_METRICS; i++) {
    if (i > 0) json += ",";
    json += String(settings.metricBarMin[i]);
  }
  json += "],";

  json += "\"metricBarMax\":[";
  for (int i = 0; i < MAX_METRICS; i++) {
    if (i > 0) json += ",";
    json += String(settings.metricBarMax[i]);
  }
  json += "],";

  json += "\"metricBarWidths\":[";
  for (int i = 0; i < MAX_METRICS; i++) {
    if (i > 0) json += ",";
    json += String(settings.metricBarWidths[i]);
  }
  json += "],";

  json += "\"metricBarOffsets\":[";
  for (int i = 0; i < MAX_METRICS; i++) {
    if (i > 0) json += ",";
    json += String(settings.metricBarOffsets[i]);
  }
  json += "]";

  json += "}";

  server.send(200, "application/json", json);
}

// Import configuration from JSON
void handleImportConfig() {
  if (server.hasArg("plain")) {
    String body = server.arg("plain");

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, body);

    if (error) {
      server.send(400, "application/json", "{\"success\":false,\"message\":\"Invalid JSON\"}");
      return;
    }

    // Import clock settings
    if (!doc["clockStyle"].isNull()) settings.clockStyle = doc["clockStyle"];
    if (!doc["gmtOffset"].isNull()) settings.gmtOffset = doc["gmtOffset"];
    if (!doc["daylightSaving"].isNull()) settings.daylightSaving = doc["daylightSaving"];
    if (!doc["use24Hour"].isNull()) settings.use24Hour = doc["use24Hour"];
    if (!doc["dateFormat"].isNull()) settings.dateFormat = doc["dateFormat"];
    if (!doc["clockPosition"].isNull()) settings.clockPosition = doc["clockPosition"];
    if (!doc["clockOffset"].isNull()) settings.clockOffset = doc["clockOffset"];
    if (!doc["showClock"].isNull()) settings.showClock = doc["showClock"];
    if (!doc["displayRowMode"].isNull()) settings.displayRowMode = doc["displayRowMode"];
    if (!doc["useRpmKFormat"].isNull()) settings.useRpmKFormat = doc["useRpmKFormat"];
    if (!doc["useNetworkMBFormat"].isNull()) settings.useNetworkMBFormat = doc["useNetworkMBFormat"];

    // Import metric labels
    if (!doc["metricLabels"].isNull()) {
      JsonArray labels = doc["metricLabels"];
      for (int i = 0; i < MAX_METRICS && i < labels.size(); i++) {
        const char* label = labels[i];
        if (label) {
          strncpy(settings.metricLabels[i], label, METRIC_NAME_LEN - 1);
          settings.metricLabels[i][METRIC_NAME_LEN - 1] = '\0';
        }
      }
    }

    // Import metric names
    if (!doc["metricNames"].isNull()) {
      JsonArray names = doc["metricNames"];
      for (int i = 0; i < MAX_METRICS && i < names.size(); i++) {
        const char* name = names[i];
        if (name) {
          strncpy(settings.metricNames[i], name, METRIC_NAME_LEN - 1);
          settings.metricNames[i][METRIC_NAME_LEN - 1] = '\0';
        }
      }
    }

    // Import metric order
    if (!doc["metricOrder"].isNull()) {
      JsonArray order = doc["metricOrder"];
      for (int i = 0; i < MAX_METRICS && i < order.size(); i++) {
        settings.metricOrder[i] = order[i];
      }
    }

    // Import metric companions
    if (!doc["metricCompanions"].isNull()) {
      JsonArray companions = doc["metricCompanions"];
      for (int i = 0; i < MAX_METRICS && i < companions.size(); i++) {
        settings.metricCompanions[i] = companions[i];
      }
    }

    // Import metric positions
    if (!doc["metricPositions"].isNull()) {
      JsonArray positions = doc["metricPositions"];
      for (int i = 0; i < MAX_METRICS && i < positions.size(); i++) {
        settings.metricPositions[i] = positions[i];
      }
    }

    // Import progress bar settings
    if (!doc["metricBarPositions"].isNull()) {
      JsonArray barPositions = doc["metricBarPositions"];
      for (int i = 0; i < MAX_METRICS && i < barPositions.size(); i++) {
        settings.metricBarPositions[i] = barPositions[i];
      }
    }

    if (!doc["metricBarMin"].isNull()) {
      JsonArray barMin = doc["metricBarMin"];
      for (int i = 0; i < MAX_METRICS && i < barMin.size(); i++) {
        settings.metricBarMin[i] = barMin[i];
      }
    }

    if (!doc["metricBarMax"].isNull()) {
      JsonArray barMax = doc["metricBarMax"];
      for (int i = 0; i < MAX_METRICS && i < barMax.size(); i++) {
        settings.metricBarMax[i] = barMax[i];
      }
    }

    if (!doc["metricBarWidths"].isNull()) {
      JsonArray barWidths = doc["metricBarWidths"];
      for (int i = 0; i < MAX_METRICS && i < barWidths.size(); i++) {
        settings.metricBarWidths[i] = barWidths[i];
      }
    }

    if (!doc["metricBarOffsets"].isNull()) {
      JsonArray barOffsets = doc["metricBarOffsets"];
      for (int i = 0; i < MAX_METRICS && i < barOffsets.size(); i++) {
        settings.metricBarOffsets[i] = barOffsets[i];
      }
    }

    // Hide Row 6 metrics when importing 5-row mode config
    if (settings.displayRowMode == 0) {
      for (int i = 0; i < MAX_METRICS; i++) {
        if (settings.metricPositions[i] == 10 || settings.metricPositions[i] == 11) {
          settings.metricPositions[i] = 255;
        }
        if (settings.metricBarPositions[i] == 10 || settings.metricBarPositions[i] == 11) {
          settings.metricBarPositions[i] = 255;
        }
      }
    }

    // Save imported settings
    saveSettings();
    applyTimezone();
    ntpSynced = false;  // Force NTP resync after config import

    server.send(200, "application/json", "{\"success\":true,\"message\":\"Configuration imported successfully\"}");
  } else {
    server.send(400, "application/json", "{\"success\":false,\"message\":\"No data received\"}");
  }
}

void displaySetupInstructions() {
  display.clearDisplay();
  display.setTextSize(1);
  
  display.setCursor(20, 0);
  display.println("WiFi Setup");
  display.drawLine(0, 10, 128, 10, DISPLAY_WHITE);
  
  display.setCursor(0, 14);
  display.println("1.Connect to WiFi:");
  
  display.setCursor(0, 26);
  display.print("  ");
  display.println(AP_NAME);
  
  display.setCursor(0, 38);
  display.print("  Pass: ");
  display.println(AP_PASSWORD);
  
  display.setCursor(0, 50);
  display.println("2.Open 192.168.4.1");
  
  display.display();
}

void displayConnecting() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(20, 25);
  display.println("Connecting to");
  display.setCursor(30, 40);
  display.println("WiFi...");
  display.display();
}

void displayConnected() {
  display.clearDisplay();
  display.setTextSize(1);
  
  display.setCursor(25, 4);
  display.println("Connected!");
  
  display.setCursor(8, 18);
  display.println("IP (for Python):");
  
  String ip = WiFi.localIP().toString();
  int ip_width = ip.length() * 6;
  int ip_x = (SCREEN_WIDTH - ip_width) / 2;
  display.setCursor(ip_x, 30);
  display.println(ip);
  
  display.drawLine(0, 42, 128, 42, DISPLAY_WHITE);
  
  display.setCursor(4, 48);
  display.println("Open IP in browser");
  display.setCursor(12, 56);
  display.println("to change settings");
  
  display.display();
}

void loop() {
  // Feed watchdog to prevent reset
  esp_task_wdt_reset();

  // Handle web server requests
  server.handleClient();

  // WiFi connection management with reconnection logic
  if (WiFi.status() != WL_CONNECTED) {
    if (wifiDisconnectTime == 0) {
      wifiDisconnectTime = millis();
      Serial.println("WiFi disconnected, attempting reconnection...");
      WiFi.reconnect();
    }

    // Check if reconnection timeout exceeded
    if (millis() - wifiDisconnectTime > WIFI_RECONNECT_TIMEOUT) {
      Serial.println("WiFi reconnection failed, restarting...");
      ESP.restart();
    }

    // Show reconnection status (blink every 500ms)
    if (displayAvailable && (millis() / 500) % 2 == 0) {
      display.clearDisplay();
      display.setTextSize(1);
      display.setCursor(10, 20);
      display.println("WiFi Lost");
      display.setCursor(10, 35);
      display.println("Reconnecting...");
      display.display();
    }

    return;  // Skip normal processing while reconnecting
  } else {
    // WiFi connected - reset disconnect timer
    if (wifiDisconnectTime != 0) {
      Serial.println("WiFi reconnected successfully!");
      wifiDisconnectTime = 0;
      ntpSynced = false;  // Force NTP resync after WiFi reconnection
    }
  }

  // Periodic NTP verification - force resync every hour to prevent drift
  if (ntpSynced && (millis() - lastNtpSyncTime > NTP_RESYNC_INTERVAL)) {
    Serial.println("Periodic NTP resync triggered");
    ntpSynced = false;  // Force NTP to resync
  }

  int packetSize = udp.parsePacket();
  if (packetSize) {
    // Static buffer to avoid stack overflow (ESP32-C3 has limited stack)
    static char buffer[2048];  // Increased for MAX_METRICS=20 edge case
    int len = udp.read(buffer, sizeof(buffer) - 1);
    if (len > 0) {
      buffer[len] = '\0';

      // Debug: Show packet info
      Serial.print("UDP packet: ");
      Serial.print(packetSize);
      Serial.print(" bytes, read: ");
      Serial.print(len);
      Serial.println(" bytes");

      if (packetSize > sizeof(buffer) - 1) {
        Serial.printf("ERROR: Packet %d bytes exceeds buffer %d bytes! Data truncated.\n",
                      packetSize, (int)sizeof(buffer));
        udp.flush();  // Clear remaining data to prevent buffer corruption
        lastReceived = millis();  // Still update timestamp to prevent timeout
        return;  // Skip processing this packet
      }

      parseStats(buffer);
      lastReceived = millis();
    }
  }
  
  metricData.online = (millis() - lastReceived) < TIMEOUT;

  // ========== Adaptive Refresh Rate Control ==========
  // Smooth refresh rate system: schedules next update time to prevent frame drops
  // when transitioning between refresh rates (e.g., 40 Hz animation → 20 Hz idle)

  unsigned long now = millis();

  // Check if it's time to update the display
  if (now >= nextDisplayUpdate) {
    if (displayAvailable) {
      display.clearDisplay();

      if (metricData.online) {
        displayStats();
      } else {
        if (settings.clockStyle == 0) {
          displayClockWithMario();
        } else if (settings.clockStyle == 1) {
          displayStandardClock();
        } else if (settings.clockStyle == 2) {
          displayLargeClock();
        } else if (settings.clockStyle == 3 || settings.clockStyle == 4) {
          displayClockWithSpace();
        } else if (settings.clockStyle == 5) {
          displayClockWithPong();
        } else if (settings.clockStyle == 6) {
          displayClockWithPacman();
        }
      }

      display.display();

      // Schedule next update based on current optimal refresh rate
      // This prevents frame drops when refresh rate changes mid-animation
      int refreshHz = getOptimalRefreshRate();
      unsigned long refreshInterval = 1000 / refreshHz;
      nextDisplayUpdate = now + refreshInterval;
    } else {
      // Display not available - attempt to reinitialize
      static unsigned long lastRetry = 0;
      if (now - lastRetry > 5000) {  // Retry every 5 seconds
        lastRetry = now;
        Serial.println("Attempting display reinitialization...");
        displayAvailable = display.begin(0x3C, false);
        if (displayAvailable) {
          Serial.println("Display reinitialized successfully!");
          display.clearDisplay();
          display.display();
        } else {
          Serial.println("Display reinitialization failed, will retry...");
        }
      }
    }
  }

  // Small delay to prevent CPU spinning, much shorter than before
  delay(5);  // 5ms delay allows up to 200 Hz theoretical max, CPU usage ~2-5%
}

// Helper function to trim trailing whitespace (only from Python names, not custom labels)
void trimTrailingSpaces(char* str) {
  int len = strlen(str);
  while (len > 0 && (str[len - 1] == ' ' || str[len - 1] == '\t')) {
    str[len - 1] = '\0';
    len--;
  }
}

// Helper function to convert '^' to spaces in labels for custom spacing
// Example: "CPU^^" becomes "CPU  " for display alignment
void convertCaretToSpaces(char* str) {
  int len = strlen(str);
  for (int i = 0; i < len; i++) {
    if (str[i] == '^') {
      str[i] = ' ';
    }
  }
}

// ========== Security & Validation Helpers ==========

// Validate IP address format (prevents invalid IPs from crashing the device)
bool validateIP(const char* ip) {
  if (!ip || strlen(ip) == 0 || strlen(ip) > 15) {
    return false;
  }

  int octets[4];
  int result = sscanf(ip, "%d.%d.%d.%d", &octets[0], &octets[1], &octets[2], &octets[3]);

  if (result != 4) {
    return false;
  }

  // Validate each octet is in range 0-255
  for (int i = 0; i < 4; i++) {
    if (octets[i] < 0 || octets[i] > 255) {
      return false;
    }
  }

  return true;
}

// Safe string copy with null terminator (prevents buffer overflows)
bool safeCopyString(char* dest, const char* src, size_t maxLen) {
  if (!dest || !src || maxLen == 0) {
    return false;
  }

  size_t srcLen = strlen(src);
  if (srcLen >= maxLen) {
    // Source is too long, truncate
    strncpy(dest, src, maxLen - 1);
    dest[maxLen - 1] = '\0';
    return false;  // Indicate truncation occurred
  }

  strcpy(dest, src);
  return true;  // Success, no truncation
}

// Bounds checking for settings (logs errors if out of range)
void assertBounds(int value, int minVal, int maxVal, const char* name) {
  if (value < minVal || value > maxVal) {
    Serial.printf("ERROR: %s out of bounds: %d not in [%d,%d]\n",
                  name ? name : "value", value, minVal, maxVal);
  }
}

// ========== Colon Blink Helper ==========
// Determines whether to show the colon based on blink settings
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

// ========== Animation Detection Helper ==========
// Detects if any animation is currently active for refresh rate boosting
bool isAnimationActive() {
  // Only check for animations in clock mode (when offline)
  if (metricData.online) {
    return false;  // No animations in metrics mode
  }

  // Check Mario clock animations (clockStyle == 0)
  if (settings.clockStyle == 0) {
    // Check if Mario is jumping
    if (mario_state == MARIO_JUMPING) {
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
    // Check if space character is in action state (not just patrolling)
    if (space_state != SPACE_PATROL) {
      return true;
    }
    // Check if laser is active
    if (space_laser.active) {
      return true;
    }
    // Check if explosion fragments are active
    if (!allSpaceFragmentsInactive()) {
      return true;
    }
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

// ========== Optimal Refresh Rate Helper ==========
// Returns optimal refresh rate in Hz based on current display mode
int getOptimalRefreshRate() {
  if (settings.refreshRateMode == 1) {
    // Manual mode - use user-specified rate
    return settings.refreshRateHz;
  }

  // Auto mode - adaptive based on content
  if (!metricData.online) {
    // Clock mode (offline)

    // Check for animation boost (smooth animations during active motion)
    if (settings.boostAnimationRefresh && isAnimationActive()) {
      // Animation is happening - boost to 40 Hz for silky smooth motion!
      // (Mario bouncing, invader shooting, explosion fragments, etc.)
      return 40;
    }

    if (settings.clockStyle == 0 || settings.clockStyle == 3 || settings.clockStyle == 4 || settings.clockStyle == 5) {
      // Animated clocks (Mario, Space Invaders, Space Ship, Pong)
      // Base rate when idle (patrolling/walking/playing but not bouncing/shooting)
      return 20;  // 20 Hz keeps character movement smooth
    } else {
      // Static clocks (Standard, Large)
      return 2;  // 2 Hz is plenty for clock that updates once/second
    }
  } else {
    // Metrics mode (online)
    return 10;  // 10 Hz for PC stats (updates every 500ms from Python)
  }
}

// Parse v2.0 protocol (dynamic metrics)
void parseStatsV2(JsonDocument& doc) {
  // Extract timestamp
  const char* ts = doc["timestamp"];
  if (ts) {
    strncpy(metricData.timestamp, ts, 5);
    metricData.timestamp[5] = '\0';
  }

  // Extract metrics array
  JsonArray metricsArray = doc["metrics"];
  metricData.count = 0;

  for (JsonObject metricObj : metricsArray) {
    if (metricData.count >= MAX_METRICS) break;

    Metric& m = metricData.metrics[metricData.count];

    m.id = metricObj["id"] | 0;

    const char* name = metricObj["name"];
    if (name) {
      strncpy(m.name, name, METRIC_NAME_LEN - 1);
      m.name[METRIC_NAME_LEN - 1] = '\0';
      trimTrailingSpaces(m.name);
    }

    const char* unit = metricObj["unit"];
    if (unit) {
      strncpy(m.unit, unit, METRIC_UNIT_LEN - 1);
      m.unit[METRIC_UNIT_LEN - 1] = '\0';
    }

    m.value = metricObj["value"] | 0;

    // Load visibility from settings (ID-based indexing)
    if (m.id > 0 && m.id <= MAX_METRICS) {
      // Check if stored metric name matches current metric name
      // If no stored name exists OR name matches, apply settings
      // If name exists but doesn't match, this is a different sensor - use defaults
      bool nameMatches = (settings.metricNames[m.id - 1][0] == '\0' ||  // No stored name
                          strcmp(settings.metricNames[m.id - 1], m.name) == 0);  // Name matches

      if (nameMatches) {
        // Apply stored settings
        // Apply custom label if set (preserve '^' character - it will be converted during display)
        if (settings.metricLabels[m.id - 1][0] != '\0') {
          strncpy(m.label, settings.metricLabels[m.id - 1], METRIC_NAME_LEN - 1);
          m.label[METRIC_NAME_LEN - 1] = '\0';
        } else {
          // No custom label, copy name to label
          strncpy(m.label, m.name, METRIC_NAME_LEN - 1);
          m.label[METRIC_NAME_LEN - 1] = '\0';
        }

        // Load display order
        m.displayOrder = settings.metricOrder[m.id - 1];

        // Load companion metric
        m.companionId = settings.metricCompanions[m.id - 1];

        // Load position assignment (255 = hidden, 0-11 = visible)
        m.position = settings.metricPositions[m.id - 1];

        // Load progress bar settings
        m.barPosition = settings.metricBarPositions[m.id - 1];
        m.barMin = settings.metricBarMin[m.id - 1];
        m.barMax = settings.metricBarMax[m.id - 1];
        m.barWidth = settings.metricBarWidths[m.id - 1];
        m.barOffsetX = settings.metricBarOffsets[m.id - 1];

        // Store/update the metric name for future validation
        strncpy(settings.metricNames[m.id - 1], m.name, METRIC_NAME_LEN - 1);
        settings.metricNames[m.id - 1][METRIC_NAME_LEN - 1] = '\0';
      } else {
        // Name mismatch - this is a different sensor now, use defaults
        Serial.printf("Metric ID %d name changed: '%s' -> '%s', using defaults\n",
                      m.id, settings.metricNames[m.id - 1], m.name);

        strncpy(m.label, m.name, METRIC_NAME_LEN - 1);
        m.label[METRIC_NAME_LEN - 1] = '\0';
        m.displayOrder = metricData.count;
        m.companionId = 0;
        m.position = 255;  // Default: None/Hidden
        m.barPosition = 255;  // Default: No bar
        m.barMin = 0;
        m.barMax = 100;
        m.barWidth = 60;
        m.barOffsetX = 0;

        // Update stored name to the new sensor
        strncpy(settings.metricNames[m.id - 1], m.name, METRIC_NAME_LEN - 1);
        settings.metricNames[m.id - 1][METRIC_NAME_LEN - 1] = '\0';

        // Clear stored label since it's for a different sensor
        settings.metricLabels[m.id - 1][0] = '\0';
      }
    } else {
      // Default values for new metrics
      strncpy(m.label, m.name, METRIC_NAME_LEN - 1);
      m.label[METRIC_NAME_LEN - 1] = '\0';
      m.displayOrder = metricData.count;
      m.companionId = 0;  // No companion for new metrics
      m.position = 255;  // Default: None/Hidden
      m.barPosition = 255;  // Default: No bar
      m.barMin = 0;
      m.barMax = 100;
      m.barWidth = 60;  // Default width
      m.barOffsetX = 0;  // Default: no offset
    }

    metricData.count++;
  }

  metricData.online = true;

  // Debug output
  Serial.print("Received ");
  Serial.print(metricData.count);
  Serial.print(" metrics, ");
  int visibleCount = 0;
  for (int i = 0; i < metricData.count; i++) {
    if (metricData.metrics[i].position != 255) visibleCount++;
  }
  Serial.print(visibleCount);
  Serial.println(" visible (position assigned)");
}

// Main parsing function with version detection
void parseStats(const char* json) {
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, json);

  if (error) {
    Serial.print("JSON parse error: ");
    Serial.println(error.c_str());
    return;
  }

  parseStatsV2(doc);
}

// Compact grid layout - position-based (6 rows max)
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
    display.print(WiFi.localIP().toString());
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

// ========== Mario Clock Functions ==========
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

// Spring-based bounce for Pong clock (delta-time independent oscillating physics)
void updateDigitBouncePong() {
  static unsigned long lastPhysicsUpdate = 0;
  unsigned long currentTime = millis();

  // Calculate delta time in seconds
  float deltaTime = (currentTime - lastPhysicsUpdate) / 1000.0;

  // Cap delta time to prevent huge jumps
  if (deltaTime > 0.1 || lastPhysicsUpdate == 0) {
    deltaTime = 0.025;  // Default to 25ms (40 Hz) for first frame
  }

  lastPhysicsUpdate = currentTime;

  // Scale physics to match original 50ms timing
  float physicsScale = deltaTime / 0.05;

  // Use user-configured bounce physics (stored as tenths/hundredths, convert to floats)
  const float SPRING_STRENGTH = settings.pongBounceStrength / 10.0;  // Pull back to center
  const float DAMPING = settings.pongBounceDamping / 100.0;          // Damping factor

  for (int i = 0; i < 5; i++) {
    if (digit_offset_y[i] != 0 || digit_velocity[i] != 0) {
      // Spring force: pulls digit back to rest position (0)
      float spring_force = -digit_offset_y[i] * SPRING_STRENGTH;

      // Apply spring force and damping (scaled by physics rate)
      digit_velocity[i] += spring_force * physicsScale;
      digit_velocity[i] *= pow(DAMPING, physicsScale);  // Exponential damping scaling

      // Update position
      digit_offset_y[i] += digit_velocity[i] * physicsScale;

      // Clamp to visible movement range (allow up to 4 pixels)
      if (digit_offset_y[i] > 4) digit_offset_y[i] = 4;
      if (digit_offset_y[i] < -4) digit_offset_y[i] = -4;

      // Stop when very close to rest position
      if (abs(digit_offset_y[i]) < 0.1 && abs(digit_velocity[i]) < 0.1) {
        digit_offset_y[i] = 0;
        digit_velocity[i] = 0;
      }
    }
  }
}

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
  
  if (!time_overridden) {
    displayed_hour = timeinfo.tm_hour;
    displayed_min = timeinfo.tm_min;
  }
  
  if (time_overridden && timeinfo.tm_hour == displayed_hour && timeinfo.tm_min == displayed_min) {
    time_overridden = false;
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
  
  int date_x = (SCREEN_WIDTH - 60) / 2;
  display.setCursor(date_x, 4);
  display.print(dateStr);
  
  updateDigitBounce();
  drawTimeWithBounce();
  
  updateMarioAnimation(&timeinfo);
  
  int mario_draw_y = mario_base_y + (int)mario_jump_y;
  bool isJumping = (mario_state == MARIO_JUMPING);
  drawMario((int)mario_x, mario_draw_y, mario_facing_right, mario_walk_frame, isJumping);
}

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
}

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
}

void calculateTargetDigits(int hour, int min) {
  num_targets = 0;

  int next_min = (min + 1) % 60;
  int next_hour = hour;
  if (next_min == 0) {
    next_hour = (hour + 1) % 24;
  }

  int curr_digits[4] = {hour / 10, hour % 10, min / 10, min % 10};
  int next_digits[4] = {next_hour / 10, next_hour % 10, next_min / 10, next_min % 10};

  // Add targets from LEFT to RIGHT (hour first, then minutes)
  // Text size 3 digits are 15px wide (5*3), center is at +7
  if (curr_digits[0] != next_digits[0]) {
    target_x_positions[num_targets] = DIGIT_X[0] + 7;
    target_digit_index[num_targets] = 0;
    target_digit_values[num_targets] = next_digits[0];
    num_targets++;
  }
  if (curr_digits[1] != next_digits[1]) {
    target_x_positions[num_targets] = DIGIT_X[1] + 7;
    target_digit_index[num_targets] = 1;
    target_digit_values[num_targets] = next_digits[1];
    num_targets++;
  }
  if (curr_digits[2] != next_digits[2]) {
    target_x_positions[num_targets] = DIGIT_X[3] + 7;
    target_digit_index[num_targets] = 3;
    target_digit_values[num_targets] = next_digits[2];
    num_targets++;
  }
  if (curr_digits[3] != next_digits[3]) {
    target_x_positions[num_targets] = DIGIT_X[4] + 7;
    target_digit_index[num_targets] = 4;
    target_digit_values[num_targets] = next_digits[3];
    num_targets++;
  }
}

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
  
  if (seconds >= 55 && !animation_triggered && mario_state == MARIO_IDLE) {
    animation_triggered = true;
    calculateTargetDigits(displayed_hour, displayed_min);
    if (num_targets > 0) {
      current_target_index = 0;
      mario_x = -15;
      mario_state = MARIO_WALKING;
      mario_facing_right = true;
      digit_bounce_triggered = false;
    }
  }
  
  switch (mario_state) {
    case MARIO_IDLE:
      mario_walk_frame = 0;
      mario_x = -15;
      break;
      
    case MARIO_WALKING:
      if (current_target_index < num_targets) {
        int target = target_x_positions[current_target_index];
        
        if (abs(mario_x - target) > 3) {
          if (mario_x < target) {
            mario_x += 2.5;
            mario_facing_right = true;
          } else {
            mario_x -= 2.5;
            mario_facing_right = false;
          }
          mario_walk_frame = (mario_walk_frame + 1) % 2;
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

          jump_velocity = 2.0;
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
      mario_x += 2.5;
      mario_walk_frame = (mario_walk_frame + 1) % 2;
      
      if (mario_x > SCREEN_WIDTH + 15) {
        mario_state = MARIO_IDLE;
        mario_x = -15;
      }
      break;
  }
}

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

    if (facingRight) {
      display.drawPixel(sx + 1, sy + 4, DISPLAY_WHITE);
      display.drawPixel(sx + 6, sy + 3 + (frame % 2), DISPLAY_WHITE);
    } else {
      display.drawPixel(sx + 6, sy + 4, DISPLAY_WHITE);
      display.drawPixel(sx + 1, sy + 3 + (frame % 2), DISPLAY_WHITE);
    }

    if (frame == 0) {
      display.fillRect(sx + 2, sy + 6, 2, 3, DISPLAY_WHITE);
      display.fillRect(sx + 4, sy + 6, 2, 3, DISPLAY_WHITE);
    } else {
      display.fillRect(sx + 1, sy + 6, 2, 3, DISPLAY_WHITE);
      display.fillRect(sx + 5, sy + 6, 2, 3, DISPLAY_WHITE);
    }
  }
}

// ========== Space Clock Animation Functions (Clock Style 3 - Unified) ==========

// Draw Space character sprite (Invader or Ship based on settings.spaceCharacterType)
void drawSpaceCharacter(int x, int y, int frame) {
  // Bounds check
  if (x < -12 || x > SCREEN_WIDTH + 12) return;
  if (y < -10 || y > SCREEN_HEIGHT + 10) return;

  if (settings.spaceCharacterType == 0) {
    // Draw Invader sprite (11x11 pixels, classic invader design)
    int sx = x - 5;
    int sy = y - 4;

    // Antennae
    display.drawPixel(sx + 2, sy, DISPLAY_WHITE);
    display.drawPixel(sx + 8, sy, DISPLAY_WHITE);

    // Head
    display.fillRect(sx + 3, sy + 1, 5, 1, DISPLAY_WHITE);

    // Body
    display.fillRect(sx + 2, sy + 2, 7, 1, DISPLAY_WHITE);
    display.fillRect(sx + 1, sy + 3, 9, 1, DISPLAY_WHITE);

    // Eyes
    display.fillRect(sx, sy + 4, 3, 1, DISPLAY_WHITE);
    display.drawPixel(sx + 5, sy + 4, DISPLAY_WHITE);
    display.fillRect(sx + 8, sy + 4, 3, 1, DISPLAY_WHITE);

    // Mouth
    display.fillRect(sx, sy + 5, 11, 1, DISPLAY_WHITE);

    // Legs (frame-dependent)
    if (frame == 0) {
      // Legs down
      display.drawPixel(sx + 1, sy + 6, DISPLAY_WHITE);
      display.fillRect(sx + 4, sy + 6, 3, 1, DISPLAY_WHITE);
      display.drawPixel(sx + 9, sy + 6, DISPLAY_WHITE);
      display.fillRect(sx, sy + 7, 2, 1, DISPLAY_WHITE);
      display.drawPixel(sx + 5, sy + 7, DISPLAY_WHITE);
      display.fillRect(sx + 9, sy + 7, 2, 1, DISPLAY_WHITE);
    } else {
      // Legs up
      display.fillRect(sx + 2, sy + 6, 7, 1, DISPLAY_WHITE);
      display.drawPixel(sx + 1, sy + 7, DISPLAY_WHITE);
      display.drawPixel(sx + 9, sy + 7, DISPLAY_WHITE);
      display.fillRect(sx, sy + 8, 2, 1, DISPLAY_WHITE);
      display.fillRect(sx + 9, sy + 8, 2, 1, DISPLAY_WHITE);
    }
  } else {
    // Draw Ship sprite (11x7 pixels, classic ship design)
    int sx = x - 5;
    int sy = y - 3;

    // Top point
    display.drawPixel(sx + 5, sy, DISPLAY_WHITE);

    // Upper body
    display.fillRect(sx + 4, sy + 1, 3, 1, DISPLAY_WHITE);
    display.fillRect(sx + 3, sy + 2, 5, 1, DISPLAY_WHITE);

    // Main body
    display.fillRect(sx + 1, sy + 3, 9, 1, DISPLAY_WHITE);
    display.fillRect(sx, sy + 4, 11, 1, DISPLAY_WHITE);

    // Wings - animate between two frames for thruster effect
    if (frame == 0) {
      // Wings down
      display.fillRect(sx, sy + 5, 3, 1, DISPLAY_WHITE);
      display.fillRect(sx + 8, sy + 5, 3, 1, DISPLAY_WHITE);
      display.drawPixel(sx, sy + 6, DISPLAY_WHITE);
      display.drawPixel(sx + 10, sy + 6, DISPLAY_WHITE);
    } else {
      // Wings up (thruster pulse)
      display.fillRect(sx + 1, sy + 5, 2, 1, DISPLAY_WHITE);
      display.fillRect(sx + 8, sy + 5, 2, 1, DISPLAY_WHITE);
      display.drawPixel(sx + 1, sy + 6, DISPLAY_WHITE);
      display.drawPixel(sx + 9, sy + 6, DISPLAY_WHITE);
    }
  }
}

// Handle patrol state - slow left-right drift
void handleSpacePatrolState() {
  space_x += (settings.spacePatrolSpeed / 10.0) * space_patrol_direction;

  // Reverse direction at boundaries
  if (space_x <= SPACE_PATROL_LEFT) {
    space_x = SPACE_PATROL_LEFT;
    space_patrol_direction = 1;
  } else if (space_x >= SPACE_PATROL_RIGHT) {
    space_x = SPACE_PATROL_RIGHT;
    space_patrol_direction = -1;
  }
}

// Handle sliding to target position - fast horizontal movement
void handleSpaceSlidingState() {
  float target_x = target_x_positions[current_target_index];

  // Slide horizontally to target
  if (abs(space_x - target_x) > 1.0) {
    if (space_x < target_x) {
      space_x += (settings.spaceAttackSpeed / 10.0);
      if (space_x > target_x) space_x = target_x;
    } else {
      space_x -= (settings.spaceAttackSpeed / 10.0);
      if (space_x < target_x) space_x = target_x;
    }
  } else {
    // Reached target position - start shooting
    space_x = target_x;
    space_state = SPACE_SHOOTING;
    fireSpaceLaser(target_digit_index[current_target_index]);
  }
}

// Handle shooting state - laser update handles transition
void handleSpaceShootingState() {
  // Laser update handles transition to EXPLODING_DIGIT
}

// Handle exploding state - move away quickly after 5 frames
void handleSpaceExplodingState() {
  space_explosion_timer++;
  // Move away quickly - don't wait for explosion to finish
  if (space_explosion_timer >= 5) {
    current_target_index++;
    if (current_target_index < num_targets) {
      space_state = SPACE_MOVING_NEXT;
    } else {
      space_state = SPACE_RETURNING;
    }
  }
}

// Handle moving to next target - slide to next digit
void handleSpaceMovingNextState() {
  float target_x = target_x_positions[current_target_index];

  if (abs(space_x - target_x) > 1.0) {
    if (space_x < target_x) {
      space_x += (settings.spaceAttackSpeed / 10.0);
      if (space_x > target_x) space_x = target_x;
    } else {
      space_x -= (settings.spaceAttackSpeed / 10.0);
      if (space_x < target_x) space_x = target_x;
    }
  } else {
    space_x = target_x;
    space_state = SPACE_SHOOTING;
    fireSpaceLaser(target_digit_index[current_target_index]);
  }
}

// Handle returning to patrol - slide back to center
void handleSpaceReturningState() {
  float center_x = 64;

  if (abs(space_x - center_x) > 1.0) {
    if (space_x < center_x) {
      space_x += (settings.spacePatrolSpeed / 10.0);
      if (space_x > center_x) space_x = center_x;
    } else {
      space_x -= (settings.spacePatrolSpeed / 10.0);
      if (space_x < center_x) space_x = center_x;
    }
  } else {
    space_x = center_x;
    space_state = SPACE_PATROL;
    time_overridden = false;  // Allow time to resync
  }
}

// Draw space laser beam (upward)
void drawSpaceLaser(Laser* laser) {
  if (!laser->active) return;

  // Vertical laser beam shooting UPWARD
  for (int i = 0; i < (int)laser->length; i += 2) {
    int ly = (int)laser->y - i;  // Subtract to go upward
    if (ly >= 0 && ly < SCREEN_HEIGHT) {
      display.drawPixel((int)laser->x, ly, DISPLAY_WHITE);
      display.drawPixel((int)laser->x + 1, ly, DISPLAY_WHITE);
    }
  }

  // Impact flash at end (top of beam)
  int end_y = (int)(laser->y - laser->length);
  if (end_y >= 0 && end_y < SCREEN_HEIGHT) {
    display.drawPixel((int)laser->x - 1, end_y, DISPLAY_WHITE);
    display.drawPixel((int)laser->x + 2, end_y, DISPLAY_WHITE);
  }
}

// Update space laser
void updateSpaceLaser() {
  if (!space_laser.active) return;

  space_laser.length += (settings.spaceLaserSpeed / 10.0);

  // Check if reached digit (bottom of time digits)
  const int SPACE_TIME_Y = 16;
  int digit_bottom_y = SPACE_TIME_Y + 24;
  int laser_end_y = space_laser.y - space_laser.length;

  if (laser_end_y <= digit_bottom_y) {
    space_laser.active = false;
    spawnSpaceExplosion(space_laser.target_digit_idx);
    updateSpecificDigit(target_digit_index[current_target_index],
                       target_digit_values[current_target_index]);
    space_explosion_timer = 0;
    space_state = SPACE_EXPLODING_DIGIT;
  }

  if (space_laser.length > LASER_MAX_LENGTH) {
    space_laser.length = LASER_MAX_LENGTH;
  }
}

// Fire space laser
void fireSpaceLaser(int target_digit_idx) {
  space_laser.x = space_x;
  space_laser.y = space_y - 4;  // Start from top of character
  space_laser.length = 0;
  space_laser.active = true;
  space_laser.target_digit_idx = target_digit_idx;
}

// Spawn space explosion fragments
void spawnSpaceExplosion(int digitIndex) {
  const int SPACE_TIME_Y = 16;
  int digit_x = DIGIT_X[digitIndex] + 9;
  int digit_y = SPACE_TIME_Y + 12;

  int frag_count = 10;
  float angle_step = (2 * PI) / frag_count;

  for (int i = 0; i < frag_count; i++) {
    SpaceFragment* f = findFreeSpaceFragment();
    if (!f) break;

    float angle = i * angle_step + random(-30, 30) / 100.0;
    float speed = 3.0 + random(-50, 50) / 100.0;  // Base speed ~3.0

    f->x = digit_x + random(-4, 4);
    f->y = digit_y + random(-6, 6);
    f->vx = cos(angle) * speed;
    f->vy = sin(angle) * speed - 1.0;
    f->active = true;
  }
}

// Update space fragments
void updateSpaceFragments() {
  for (int i = 0; i < MAX_SPACE_FRAGMENTS; i++) {
    if (space_fragments[i].active) {
      space_fragments[i].vy += (settings.spaceExplosionGravity / 10.0);
      space_fragments[i].x += space_fragments[i].vx;
      space_fragments[i].y += space_fragments[i].vy;

      if (space_fragments[i].y > 70 ||
          space_fragments[i].x < -5 ||
          space_fragments[i].x > 133) {
        space_fragments[i].active = false;
      }
    }
  }
}

// Draw space fragments
void drawSpaceFragments() {
  for (int i = 0; i < MAX_SPACE_FRAGMENTS; i++) {
    if (space_fragments[i].active) {
      display.fillRect((int)space_fragments[i].x,
                      (int)space_fragments[i].y, 2, 2, DISPLAY_WHITE);
    }
  }
}

// Check if all space fragments are inactive
bool allSpaceFragmentsInactive() {
  for (int i = 0; i < MAX_SPACE_FRAGMENTS; i++) {
    if (space_fragments[i].active) return false;
  }
  return true;
}

// Find free space fragment
SpaceFragment* findFreeSpaceFragment() {
  for (int i = 0; i < MAX_SPACE_FRAGMENTS; i++) {
    if (!space_fragments[i].active) return &space_fragments[i];
  }
  return nullptr;
}

// Main space animation update
void updateSpaceAnimation(struct tm* timeinfo) {
  unsigned long currentMillis = millis();

  const int SPACE_ANIM_SPEED = 50;  // 50ms = 20 FPS
  const int SPRITE_TOGGLE_SPEED = 200;  // Slow retro animation

  if (currentMillis - last_space_update < SPACE_ANIM_SPEED) return;
  last_space_update = currentMillis;

  int seconds = timeinfo->tm_sec;
  int current_minute = timeinfo->tm_min;

  // Reset trigger
  if (current_minute != last_minute) {
    last_minute = current_minute;
    animation_triggered = false;
  }

  // Toggle sprite
  if (currentMillis - last_space_sprite_toggle >= SPRITE_TOGGLE_SPEED) {
    space_anim_frame = 1 - space_anim_frame;
    last_space_sprite_toggle = currentMillis;
  }

  // Trigger at 55 seconds - transition from PATROL to SLIDING
  if (seconds >= 55 && !animation_triggered && space_state == SPACE_PATROL) {
    animation_triggered = true;
    time_overridden = true;
    calculateTargetDigits(displayed_hour, displayed_min);

    if (num_targets > 0) {
      current_target_index = 0;
      space_state = SPACE_SLIDING;
    }
  }

  updateSpaceFragments();
  updateSpaceLaser();

  switch (space_state) {
    case SPACE_PATROL:
      handleSpacePatrolState();
      break;
    case SPACE_SLIDING:
      handleSpaceSlidingState();
      break;
    case SPACE_SHOOTING:
      handleSpaceShootingState();
      break;
    case SPACE_EXPLODING_DIGIT:
      handleSpaceExplodingState();
      break;
    case SPACE_MOVING_NEXT:
      handleSpaceMovingNextState();
      break;
    case SPACE_RETURNING:
      handleSpaceReturningState();
      break;
  }
}

// Display clock with space animation
void displayClockWithSpace() {
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

  // Update animation FIRST so time advances before drawing
  updateSpaceAnimation(&timeinfo);

  // Time management
  if (!time_overridden) {
    displayed_hour = timeinfo.tm_hour;
    displayed_min = timeinfo.tm_min;
  }

  // Reset time_overridden when real time catches up AND space character is in PATROL state
  if (time_overridden && timeinfo.tm_hour == displayed_hour &&
      timeinfo.tm_min == displayed_min && space_state == SPACE_PATROL) {
    time_overridden = false;
  }

  // Date (at top, Y=4)
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

  // Time digits
  const int SPACE_TIME_Y = 16;
  display.setTextSize(3);
  char digits[5];
  digits[0] = '0' + (displayed_hour / 10);
  digits[1] = '0' + (displayed_hour % 10);
  digits[2] = shouldShowColon() ? ':' : ' ';  // Blinking colon
  digits[3] = '0' + (displayed_min / 10);
  digits[4] = '0' + (displayed_min % 10);

  for (int i = 0; i < 5; i++) {
    display.setCursor(DIGIT_X[i], SPACE_TIME_Y);
    display.print(digits[i]);
  }

  // Render space character (ALWAYS visible - either patrolling or attacking)
  drawSpaceCharacter((int)space_x, (int)space_y, space_anim_frame);

  // Render laser if active
  if (space_laser.active) {
    drawSpaceLaser(&space_laser);
  }

  // Render explosion fragments
  drawSpaceFragments();
}

// ========== Pong Clock Functions (Clock Style 5) ==========

// Fragment pool helper functions
SpaceFragment* findFreePongFragment() {
  for (int i = 0; i < MAX_PONG_FRAGMENTS; i++) {
    if (!pong_fragments[i].active) return &pong_fragments[i];
  }
  return nullptr;
}

bool allPongFragmentsInactive() {
  for (int i = 0; i < MAX_PONG_FRAGMENTS; i++) {
    if (pong_fragments[i].active) return false;
  }
  return true;
}

// Initialize Pong animation
void initPongAnimation() {
  // Reset ball 0 above paddle, traveling upward
  pong_balls[0].x = 64 * 16;  // Center X
  pong_balls[0].y = (BREAKOUT_PADDLE_Y - 4) * 16;  // Just above paddle

  // Always start upward (negative Y) with random X direction
  if (random(0, 2) == 0) {
    pong_balls[0].vx = settings.pongBallSpeed;  // Right
  } else {
    pong_balls[0].vx = -settings.pongBallSpeed;  // Left
  }
  pong_balls[0].vy = -settings.pongBallSpeed;  // Up

  pong_balls[0].state = PONG_BALL_SPAWNING;
  pong_balls[0].spawn_timer = millis();
  pong_balls[0].active = true;
  pong_balls[0].inside_digit = -1;  // Not inside any digit

  // Deactivate ball 1
  pong_balls[1].active = false;
  pong_balls[1].inside_digit = -1;

  // Reset paddle to center bottom
  breakout_paddle.x = 64;
  breakout_paddle.target_x = 64;
  breakout_paddle.width = settings.pongPaddleWidth;  // Use user-configured width

  // Clear digit transitions
  for (int i = 0; i < 5; i++) {
    digit_transitions[i].state = DIGIT_NORMAL;
    digit_transitions[i].hit_count = 0;
    digit_transitions[i].fragments_spawned = 0;
    digit_transitions[i].assembly_progress = 0.0;
  }

  // Clear fragments and targets
  for (int i = 0; i < MAX_PONG_FRAGMENTS; i++) {
    pong_fragments[i].active = false;
    fragment_targets[i].target_digit = -1;
  }

  // Initialize displayed time from current time
  struct tm timeinfo;
  if (getTimeWithTimeout(&timeinfo)) {
    displayed_hour = timeinfo.tm_hour;
    displayed_min = timeinfo.tm_min;
  } else {
    // Fallback if time not available
    displayed_hour = 0;
    displayed_min = 0;
  }

  last_pong_update = millis();
}

// Reset Pong animation
void resetPongAnimation() {
  initPongAnimation();

  // Force re-initialization next time displayClockWithPong is called
  // (This is a workaround since we can't access the static var directly)
}

// Spawn ball with upward direction (Breakout style)
void spawnPongBall(int ballIndex) {
  pong_balls[ballIndex].x = breakout_paddle.x * 16;  // Spawn on paddle
  pong_balls[ballIndex].y = (BREAKOUT_PADDLE_Y - 4) * 16;  // Just above paddle

  // Random X direction, always upward
  if (random(0, 2) == 0) {
    pong_balls[ballIndex].vx = settings.pongBallSpeed;
  } else {
    pong_balls[ballIndex].vx = -settings.pongBallSpeed;
  }
  pong_balls[ballIndex].vy = -settings.pongBallSpeed;  // Always up

  pong_balls[ballIndex].state = PONG_BALL_NORMAL;
  pong_balls[ballIndex].active = true;
  pong_balls[ballIndex].inside_digit = -1;
}

// Update breakout paddle (auto-tracking with smooth movement)
void updateBreakoutPaddle() {
  // Find target X position (track closest active ball)
  int closest_ball = -1;
  int closest_dist = 999;

  for (int i = 0; i < MAX_PONG_BALLS; i++) {
    if (pong_balls[i].active) {
      int ball_x = pong_balls[i].x / 16;
      int dist = abs(ball_x - breakout_paddle.x);
      if (dist < closest_dist) {
        closest_dist = dist;
        closest_ball = i;
      }
    }
  }

  // Set target to ball X position (perfect tracking)
  if (closest_ball >= 0) {
    int ball_x = pong_balls[closest_ball].x / 16;
    breakout_paddle.target_x = ball_x;
  }

  // Move toward target with smooth movement
  int dx = breakout_paddle.target_x - breakout_paddle.x;

  // Smooth acceleration based on distance
  int move_speed = breakout_paddle.speed;
  if (abs(dx) > 20) {
    move_speed = 5;  // Faster when far away
  } else if (abs(dx) > 10) {
    move_speed = 4;  // Medium speed
  } else if (abs(dx) > 3) {
    move_speed = 3;  // Normal speed
  } else {
    move_speed = 2;  // Slow when close (prevents overshoot)
  }

  if (abs(dx) > 1) {
    // 10% chance to move in wrong direction (adds unpredictability)
    bool move_wrong_way = (random(0, 100) < PADDLE_WRONG_DIRECTION_CHANCE);

    if (move_wrong_way) {
      // Intentionally move opposite direction
      if (dx > 0) {
        breakout_paddle.x -= move_speed;
      } else {
        breakout_paddle.x += move_speed;
      }
    } else {
      // Normal tracking
      if (dx > 0) {
        breakout_paddle.x += move_speed;
      } else {
        breakout_paddle.x -= move_speed;
      }
    }
  } else {
    breakout_paddle.x = breakout_paddle.target_x;  // Snap if very close
  }

  // Clamp paddle to screen bounds
  int paddle_half = breakout_paddle.width / 2;
  if (breakout_paddle.x - paddle_half < 0) {
    breakout_paddle.x = paddle_half;
  }
  if (breakout_paddle.x + paddle_half > 127) {
    breakout_paddle.x = 127 - paddle_half;
  }

  // Track paddle position for momentum-based ball release (updated after movement)
  paddle_last_x = breakout_paddle.x;
}

// Draw breakout paddle at bottom
void drawBreakoutPaddle() {
  int paddle_left = breakout_paddle.x - (breakout_paddle.width / 2);
  int paddle_right = paddle_left + breakout_paddle.width;

  display.fillRect(paddle_left, BREAKOUT_PADDLE_Y, breakout_paddle.width, BREAKOUT_PADDLE_HEIGHT, DISPLAY_WHITE);
}

// Update fragment physics (gravity, boundaries)
void updatePongFragments() {
  for (int i = 0; i < MAX_PONG_FRAGMENTS; i++) {
    if (!pong_fragments[i].active) continue;

    pong_fragments[i].vy += PONG_FRAG_GRAVITY;  // Apply gravity
    pong_fragments[i].x += pong_fragments[i].vx;
    pong_fragments[i].y += pong_fragments[i].vy;

    // Deactivate if off-screen
    if (pong_fragments[i].y > SCREEN_HEIGHT + 5 ||
        pong_fragments[i].x < -5 ||
        pong_fragments[i].x > SCREEN_WIDTH + 5) {
      pong_fragments[i].active = false;
    }
  }
}

// Draw fragments as 2×2 pixels
void drawPongFragments() {
  for (int i = 0; i < MAX_PONG_FRAGMENTS; i++) {
    if (pong_fragments[i].active) {
      int fx = (int)pong_fragments[i].x;
      int fy = (int)pong_fragments[i].y;
      display.fillRect(fx, fy, 2, 2, DISPLAY_WHITE);
    }
  }
}

// Spawn 2-4 fragments on ball collision
void spawnPongBallHitFragments(int x, int y) {
  int frag_count = 2 + random(0, 3);  // 2-4 fragments

  for (int i = 0; i < frag_count; i++) {
    SpaceFragment* f = findFreePongFragment();
    if (!f) break;

    f->x = x + random(-2, 3);
    f->y = y + random(-2, 3);

    float angle = random(0, 360) * PI / 180.0;
    float speed = PONG_FRAG_SPEED + random(-20, 20) / 100.0;

    f->vx = cos(angle) * speed;
    f->vy = sin(angle) * speed;
    f->active = true;
  }
}

// Spawn pixel-accurate fragments from breaking digit (TRUE PIXEL SAMPLING)
void spawnDigitBreakFragments(int digitIndex, char oldChar) {
  uint8_t* buffer = display.getBuffer();
  if (!buffer) return;  // Safety check

  int digit_x = DIGIT_X[digitIndex];
  int digit_y = PONG_TIME_Y;

  // Sample every 2nd pixel to limit fragment count
  // Use random sampling: only spawn fragment for ~37.5% of lit pixels
  for (int dy = 0; dy < 24; dy += 2) {
    for (int dx = 0; dx < 15; dx += 2) {
      int px = digit_x + dx;
      int py = digit_y + dy;

      // Read pixel from buffer (1 bit per pixel, organized in 8-pixel pages)
      int page = py / 8;
      int bit = py % 8;
      int index = px + (page * 128);
      bool pixel_lit = (buffer[index] >> bit) & 0x01;

      if (pixel_lit && random(0, 8) < 3) {  // 37.5% spawn rate
        SpaceFragment* f = findFreePongFragment();
        if (!f) break;

        f->x = px;
        f->y = py;

        // Velocity: outward from digit center + randomization
        float dx_center = px - (digit_x + 7);
        float dy_center = py - (digit_y + 12);
        float angle = atan2(dy_center, dx_center) + random(-30, 30) / 100.0;
        float speed = PONG_FRAG_SPEED + random(-50, 50) / 100.0;

        f->vx = cos(angle) * speed;
        f->vy = sin(angle) * speed - 0.5;  // Slight upward bias
        f->active = true;
      }
    }
  }
}

// Spawn progressive fragments (25%, 50%, or 25% based on hit number)
void spawnProgressiveFragments(int digitIndex, char oldChar, int hitNumber) {
  uint8_t* buffer = display.getBuffer();
  if (!buffer) return;

  int digit_x = DIGIT_X[digitIndex];
  int digit_y = PONG_TIME_Y;

  // Calculate how many fragments to spawn based on hit number (0, 1, or 2)
  float spawn_percent = FRAGMENT_SPAWN_PERCENT[hitNumber];
  int spawn_chance = (int)(spawn_percent * 8);  // 0-8 scale for random check

  // Sample pixels and spawn fragments
  for (int dy = 0; dy < 24; dy += 2) {
    for (int dx = 0; dx < 15; dx += 2) {
      int px = digit_x + dx;
      int py = digit_y + dy;

      int page = py / 8;
      int bit = py % 8;
      int index = px + (page * 128);
      bool pixel_lit = (buffer[index] >> bit) & 0x01;

      if (pixel_lit && random(0, 8) < spawn_chance) {
        SpaceFragment* f = findFreePongFragment();
        if (!f) break;

        f->x = px;
        f->y = py;

        // Velocity: outward from digit center
        float dx_center = px - (digit_x + 7);
        float dy_center = py - (digit_y + 12);
        float angle = atan2(dy_center, dx_center) + random(-30, 30) / 100.0;
        float speed = PONG_FRAG_SPEED + random(-50, 50) / 100.0;

        f->vx = cos(angle) * speed;
        f->vy = sin(angle) * speed - 0.5;
        f->active = true;
      }
    }
  }
}

// Spawn assembly fragments (fragments that will converge to form new digit)
void spawnAssemblyFragments(int digitIndex, char newChar) {
  // Temporarily render new digit to sample its pixels
  uint8_t* buffer = display.getBuffer();
  if (!buffer) return;

  int digit_x = DIGIT_X[digitIndex];
  int digit_y = PONG_TIME_Y;

  // Sample every 2nd pixel from new digit
  for (int dy = 0; dy < 24; dy += 2) {
    for (int dx = 0; dx < 15; dx += 2) {
      int px = digit_x + dx;
      int py = digit_y + dy;

      int page = py / 8;
      int bit = py % 8;
      int index = px + (page * 128);
      bool pixel_lit = (buffer[index] >> bit) & 0x01;

      if (pixel_lit && random(0, 8) < 4) {  // 50% spawn rate
        SpaceFragment* f = findFreePongFragment();
        if (!f) break;

        // Start fragment from random position off-screen edges
        int start_side = random(0, 4);
        switch (start_side) {
          case 0: f->x = random(0, 128); f->y = -5; break;  // Top
          case 1: f->x = 133; f->y = random(0, 64); break;  // Right
          case 2: f->x = random(0, 128); f->y = 69; break;  // Bottom
          case 3: f->x = -5; f->y = random(0, 64); break;   // Left
        }

        // Store target position for this fragment
        for (int i = 0; i < MAX_PONG_FRAGMENTS; i++) {
          if (&pong_fragments[i] == f) {
            fragment_targets[i].target_digit = digitIndex;
            fragment_targets[i].target_x = px;
            fragment_targets[i].target_y = py;
            break;
          }
        }

        // Initial velocity toward target (will be updated in updateAssemblyFragments)
        float dx_target = px - f->x;
        float dy_target = py - f->y;
        float dist = sqrt(dx_target * dx_target + dy_target * dy_target);
        if (dist > 0) {
          f->vx = (dx_target / dist) * PONG_FRAG_SPEED * 2;
          f->vy = (dy_target / dist) * PONG_FRAG_SPEED * 2;
        }

        f->active = true;
      }
    }
  }
}

// Update assembly fragments (move toward target positions)
void updateAssemblyFragments() {
  for (int i = 0; i < MAX_PONG_FRAGMENTS; i++) {
    if (!pong_fragments[i].active) continue;
    if (fragment_targets[i].target_digit < 0) continue;  // Not an assembly fragment

    // Calculate direction to target
    float dx = fragment_targets[i].target_x - pong_fragments[i].x;
    float dy = fragment_targets[i].target_y - pong_fragments[i].y;
    float dist = sqrt(dx * dx + dy * dy);

    if (dist < 2.0) {
      // Reached target - snap to position and stop
      pong_fragments[i].x = fragment_targets[i].target_x;
      pong_fragments[i].y = fragment_targets[i].target_y;
      pong_fragments[i].vx = 0;
      pong_fragments[i].vy = 0;
      // Fragment stays active at target position
    } else {
      // Move toward target with acceleration
      float speed = PONG_FRAG_SPEED * 3;
      pong_fragments[i].vx = (dx / dist) * speed;
      pong_fragments[i].vy = (dy / dist) * speed;

      pong_fragments[i].x += pong_fragments[i].vx;
      pong_fragments[i].y += pong_fragments[i].vy;
    }
  }
}

// Check if ball is inside a digit hole (stub for now - complex feature)
bool checkDigitHoleCollision(int ballIndex, int digitIndex) {
  // TODO: Implement digit hole detection using pixel sampling
  // For now, return false (no hole collision)
  return false;
}

// Update ball position and physics
void updatePongBall(int ballIndex) {
  if (pong_balls[ballIndex].state == PONG_BALL_SPAWNING) {
    if (millis() - pong_balls[ballIndex].spawn_timer >= BALL_SPAWN_DELAY) {
      pong_balls[ballIndex].state = PONG_BALL_NORMAL;
    }
    return;  // Don't move while spawning
  }

  // Handle ball stuck to paddle (appears to bounce normally but locked to paddle X)
  if (ball_stuck_to_paddle[ballIndex]) {
    // Lock ball to paddle position (moves with paddle)
    int ball_px = breakout_paddle.x + ball_stuck_x_offset[ballIndex];
    int ball_py = BREAKOUT_PADDLE_Y - PONG_BALL_SIZE;
    pong_balls[ballIndex].x = ball_px * 16;
    pong_balls[ballIndex].y = ball_py * 16;

    // Check if it's time to release
    if (millis() >= ball_stick_release_time[ballIndex]) {
      // Release ball with momentum-based trajectory
      ball_stuck_to_paddle[ballIndex] = false;

      // Calculate paddle velocity (movement since last frame)
      int paddle_velocity = breakout_paddle.x - paddle_last_x;

      // Set horizontal velocity based on paddle movement direction
      if (paddle_velocity > 0) {
        // Paddle moving right - launch ball right
        pong_balls[ballIndex].vx = settings.pongBallSpeed + (paddle_velocity * PADDLE_MOMENTUM_MULTIPLIER);
      } else if (paddle_velocity < 0) {
        // Paddle moving left - launch ball left
        pong_balls[ballIndex].vx = -settings.pongBallSpeed + (paddle_velocity * PADDLE_MOMENTUM_MULTIPLIER);
      } else {
        // Paddle stationary - random direction
        pong_balls[ballIndex].vx = random(0, 2) == 0 ? settings.pongBallSpeed : -settings.pongBallSpeed;
      }

      // Always launch upward
      pong_balls[ballIndex].vy = -settings.pongBallSpeed;

      // Add small random variation for natural movement
      pong_balls[ballIndex].vx += random(-BALL_RELEASE_RANDOM_VARIATION, BALL_RELEASE_RANDOM_VARIATION + 1);
      pong_balls[ballIndex].vy += random(-BALL_RELEASE_RANDOM_VARIATION, BALL_RELEASE_RANDOM_VARIATION + 1);
    }

    return;  // Don't do normal movement while stuck
  }

  // Update position (fixed-point math - straight-line bounces only)
  pong_balls[ballIndex].x += pong_balls[ballIndex].vx;
  pong_balls[ballIndex].y += pong_balls[ballIndex].vy;

  // Convert to pixel coordinates for collision checks
  int ball_px = pong_balls[ballIndex].x / 16;
  int ball_py = pong_balls[ballIndex].y / 16;

  // Top wall collision (bounce down)
  if (ball_py <= PONG_PLAY_AREA_TOP) {
    ball_py = PONG_PLAY_AREA_TOP;
    pong_balls[ballIndex].y = ball_py * 16;
    pong_balls[ballIndex].vy = abs(pong_balls[ballIndex].vy);  // Force downward
  }

  // Bottom paddle collision (bounce up)
  if (ball_py + PONG_BALL_SIZE >= BREAKOUT_PADDLE_Y) {
    // Check if ball is within paddle width
    int paddle_left = breakout_paddle.x - (breakout_paddle.width / 2);
    int paddle_right = breakout_paddle.x + (breakout_paddle.width / 2);

    if (ball_px + PONG_BALL_SIZE >= paddle_left && ball_px <= paddle_right) {
      // Hit paddle - STICK TO PADDLE with random delay (prevents vertical loop)
      ball_py = BREAKOUT_PADDLE_Y - PONG_BALL_SIZE;
      pong_balls[ballIndex].y = ball_py * 16;

      // Activate stick mechanic
      ball_stuck_to_paddle[ballIndex] = true;

      // Random delay before release (0-300ms, customizable above)
      int stick_delay = random(PADDLE_STICK_MIN_DELAY, PADDLE_STICK_MAX_DELAY + 1);
      ball_stick_release_time[ballIndex] = millis() + stick_delay;

      // Store ball's X offset from paddle center (so it moves with paddle)
      ball_stuck_x_offset[ballIndex] = ball_px - breakout_paddle.x;
    } else {
      // Missed paddle
      if (ballIndex == 0) {
        // Ball 0: respawn on paddle
        spawnPongBall(ballIndex);
      } else {
        // Ball 1: deactivate (don't respawn, wait for next :55)
        pong_balls[ballIndex].active = false;
      }
      return;
    }
  }

  // Left/right wall collisions (bounce horizontally)
  if (ball_px < 0) {
    ball_px = 0;
    pong_balls[ballIndex].x = ball_px * 16;
    pong_balls[ballIndex].vx = abs(pong_balls[ballIndex].vx);  // Force right
  }
  if (ball_px + PONG_BALL_SIZE > SCREEN_WIDTH) {
    ball_px = SCREEN_WIDTH - PONG_BALL_SIZE;
    pong_balls[ballIndex].x = ball_px * 16;
    pong_balls[ballIndex].vx = -abs(pong_balls[ballIndex].vx);  // Force left
  }

  // Clamp velocities to reasonable limits (prevent too shallow or steep angles)
  if (abs(pong_balls[ballIndex].vx) < 8) {
    pong_balls[ballIndex].vx = (pong_balls[ballIndex].vx > 0) ? 8 : -8;
  }
  if (abs(pong_balls[ballIndex].vx) > 40) {
    pong_balls[ballIndex].vx = (pong_balls[ballIndex].vx > 0) ? 40 : -40;
  }
  if (abs(pong_balls[ballIndex].vy) < 8) {
    pong_balls[ballIndex].vy = (pong_balls[ballIndex].vy > 0) ? 8 : -8;
  }
  if (abs(pong_balls[ballIndex].vy) > 40) {
    pong_balls[ballIndex].vy = (pong_balls[ballIndex].vy > 0) ? 40 : -40;
  }
}

// Helper: Check if pixel is lit in display buffer (for pixel-perfect collision)
bool isPixelLit(int x, int y) {
  if (x < 0 || x >= SCREEN_WIDTH || y < 0 || y >= SCREEN_HEIGHT) {
    return false;
  }

  uint8_t* buffer = display.getBuffer();
  if (!buffer) return false;

  int page = y / 8;
  int bit = y % 8;
  int index = x + (page * SCREEN_WIDTH);

  return (buffer[index] >> bit) & 0x01;
}

// Check ball-digit collisions (pixel-perfect with progressive fragmentation)
void checkPongCollisions(int ballIndex) {
  int ball_px = pong_balls[ballIndex].x / 16;
  int ball_py = pong_balls[ballIndex].y / 16;

  // Check collision with each digit (textSize 3: ~15px wide × ~24px tall)
  for (int d = 0; d < 5; d++) {
    // Skip colon (index 2) - it shouldn't bounce or be hit
    if (d == 2) continue;

    // Skip digits that are assembling (fragments are converging)
    if (digit_transitions[d].state == DIGIT_ASSEMBLING) continue;

    // Tighter AABB collision box (reduces false positives while ensuring hits work)
    int dx1 = DIGIT_X[d] + 1;      // 1px padding on left
    int dx2 = DIGIT_X[d] + 14;     // 1px padding on right
    int dy1 = PONG_TIME_Y + 1;     // 1px padding on top
    int dy2 = PONG_TIME_Y + 23;    // 1px padding on bottom

    // AABB collision check
    if (ball_px + PONG_BALL_SIZE >= dx1 && ball_px <= dx2 &&
        ball_py + PONG_BALL_SIZE >= dy1 && ball_py <= dy2) {

      // Calculate ball hit side for push direction
      int ball_cx = ball_px + PONG_BALL_SIZE / 2;
      int ball_cy = ball_py + PONG_BALL_SIZE / 2;
      int digit_cx = (dx1 + dx2) / 2;
      int digit_cy = (dy1 + dy2) / 2;

      // Apply visible push to digit - always push AWAY from ball
      float push_strength = 3.0;  // Strong enough to see (2-3 pixel visible movement)

      // Simple logic: push digit away from ball's vertical position
      if (ball_cy < digit_cy) {
        // Ball is above digit center → push digit DOWN
        digit_velocity[d] = push_strength;
      } else {
        // Ball is below digit center → push digit UP
        digit_velocity[d] = -push_strength;
      }

      // Check if this is a breaking digit (target for hits)
      if (digit_transitions[d].state == DIGIT_BREAKING) {
        // Increment hit count and spawn progressive fragments
        int hit_num = digit_transitions[d].hit_count;

        if (hit_num < BALL_HIT_THRESHOLD) {
          digit_transitions[d].hit_count++;

          // Spawn fragments based on hit number (0=25%, 1=50%, 2=25%)
          spawnProgressiveFragments(d, digit_transitions[d].old_char, hit_num);
        }
      }

      // Reflect ball and push it out of collision to prevent sticking
      if (abs(ball_cx - digit_cx) > 4) {
        // Side hit
        pong_balls[ballIndex].vx = -pong_balls[ballIndex].vx;
        // Add random angle variation for natural movement
        pong_balls[ballIndex].vy += random(-BALL_COLLISION_ANGLE_VARIATION, BALL_COLLISION_ANGLE_VARIATION + 1);

        // Push ball out horizontally
        if (ball_cx < digit_cx) {
          ball_px = dx1 - PONG_BALL_SIZE - 1;  // Push left
        } else {
          ball_px = dx2 + 1;  // Push right
        }
        pong_balls[ballIndex].x = ball_px * 16;
      } else {
        // Top/bottom hit
        pong_balls[ballIndex].vy = -pong_balls[ballIndex].vy;
        // Add random angle variation for natural movement
        pong_balls[ballIndex].vx += random(-BALL_COLLISION_ANGLE_VARIATION, BALL_COLLISION_ANGLE_VARIATION + 1);

        // Push ball out vertically
        if (ball_cy < digit_cy) {
          ball_py = dy1 - PONG_BALL_SIZE - 1;  // Push up
        } else {
          ball_py = dy2 + 1;  // Push down
        }
        pong_balls[ballIndex].y = ball_py * 16;
      }

      break;  // Only process one collision per frame
    }
  }
}

// Trigger digit transition (start breaking process - wait for ball hits)
void triggerDigitTransition(int digitIndex, char oldChar, char newChar) {
  digit_transitions[digitIndex].state = DIGIT_BREAKING;
  digit_transitions[digitIndex].old_char = oldChar;
  digit_transitions[digitIndex].new_char = newChar;
  digit_transitions[digitIndex].state_timer = millis();
  digit_transitions[digitIndex].hit_count = 0;
  digit_transitions[digitIndex].fragments_spawned = 0;
  digit_transitions[digitIndex].assembly_progress = 0.0;

  // Don't spawn fragments yet - wait for ball hits
}

// Update digit transition state machine
void updateDigitTransitions() {
  for (int i = 0; i < 5; i++) {
    if (digit_transitions[i].state == DIGIT_NORMAL) continue;

    unsigned long elapsed = millis() - digit_transitions[i].state_timer;

    if (digit_transitions[i].state == DIGIT_BREAKING) {
      // Check if fully broken (threshold hits reached) OR timeout exceeded
      if (digit_transitions[i].hit_count >= BALL_HIT_THRESHOLD || elapsed >= DIGIT_TRANSITION_TIMEOUT) {
        // Transition to ASSEMBLING state
        digit_transitions[i].state = DIGIT_ASSEMBLING;
        digit_transitions[i].state_timer = millis();
        digit_transitions[i].assembly_progress = 0.0;

        // Spawn assembly fragments for new digit
        spawnAssemblyFragments(i, digit_transitions[i].new_char);
      }
    } else if (digit_transitions[i].state == DIGIT_ASSEMBLING) {
      // Animate assembly (fragments converging)
      float progress = (float)elapsed / DIGIT_ASSEMBLY_DURATION;
      if (progress >= 1.0) {
        digit_transitions[i].state = DIGIT_NORMAL;
        digit_transitions[i].assembly_progress = 1.0;
      } else {
        digit_transitions[i].assembly_progress = progress;
      }
    }
  }
}

// Draw Pong ball(s)
void drawPongBall() {
  for (int i = 0; i < MAX_PONG_BALLS; i++) {
    if (!pong_balls[i].active) continue;

    int ball_px = pong_balls[i].x / 16;
    int ball_py = pong_balls[i].y / 16;

    if (pong_balls[i].state == PONG_BALL_SPAWNING) {
      // Flash ball during spawn (blink effect)
      if ((millis() / 100) % 2 == 0) {
        display.fillRect(ball_px, ball_py, PONG_BALL_SIZE, PONG_BALL_SIZE, DISPLAY_WHITE);
      }
    } else {
      display.fillRect(ball_px, ball_py, PONG_BALL_SIZE, PONG_BALL_SIZE, DISPLAY_WHITE);
    }
  }
}

// Draw Pong clock digits with custom pop-in animation
void drawPongDigits() {
  display.setTextSize(3);
  display.setTextColor(DISPLAY_WHITE);

  // Build digit string
  char digits[6];
  digits[0] = '0' + (displayed_hour / 10);
  digits[1] = '0' + (displayed_hour % 10);
  digits[2] = shouldShowColon() ? ':' : ' ';
  digits[3] = '0' + (displayed_min / 10);
  digits[4] = '0' + (displayed_min % 10);
  digits[5] = '\0';

  for (int i = 0; i < 5; i++) {
    if (digit_transitions[i].state == DIGIT_BREAKING) {
      // Show old digit fading out as it gets hit
      // Render with reduced brightness based on hit count
      float fade = 1.0 - ((float)digit_transitions[i].hit_count / BALL_HIT_THRESHOLD);

      // Flicker based on hits: more hits = more flicker
      if (digit_transitions[i].hit_count > 0) {
        int flicker_speed = 100 - (digit_transitions[i].hit_count * 20);
        if ((millis() / flicker_speed) % 2 == 0) continue;  // Skip rendering = flicker
      }

      // Draw old digit
      int y = PONG_TIME_Y + (int)digit_offset_y[i];
      display.setCursor(DIGIT_X[i], y);
      char old_digit = digit_transitions[i].old_char;
      display.print(old_digit);

    } else if (digit_transitions[i].state == DIGIT_ASSEMBLING) {
      // Show new digit assembling from fragments
      // Don't draw the digit itself - fragments will converge to form it
      // Once assembly is complete (progress >= 0.8), start showing the solid digit
      if (digit_transitions[i].assembly_progress >= 0.8) {
        int y = PONG_TIME_Y + (int)digit_offset_y[i];
        display.setCursor(DIGIT_X[i], y);
        char new_digit = digit_transitions[i].new_char;
        display.print(new_digit);
      }

    } else {
      // Normal rendering with bounce offset from ball hits
      int y = PONG_TIME_Y + (int)digit_offset_y[i];
      display.setCursor(DIGIT_X[i], y);
      display.print(digits[i]);
    }
  }
}

// Main Pong animation update loop
void updatePongAnimation(struct tm* timeinfo) {
  unsigned long currentMillis = millis();

  // Throttle updates to PONG_UPDATE_INTERVAL (50 FPS)
  if (currentMillis - last_pong_update < PONG_UPDATE_INTERVAL) {
    return;
  }
  last_pong_update = currentMillis;

  // Update displayed time and detect digit changes
  if (!time_overridden) {
    int new_hour = timeinfo->tm_hour;
    int new_min = timeinfo->tm_min;

    // Detect minute change (trigger digit transitions)
    if (new_min != displayed_min || new_hour != displayed_hour) {
      // Check each digit for changes
      int old_hour_tens = displayed_hour / 10;
      int old_hour_ones = displayed_hour % 10;
      int old_min_tens = displayed_min / 10;
      int old_min_ones = displayed_min % 10;

      int new_hour_tens = new_hour / 10;
      int new_hour_ones = new_hour % 10;
      int new_min_tens = new_min / 10;
      int new_min_ones = new_min % 10;

      if (old_hour_tens != new_hour_tens) {
        triggerDigitTransition(0, '0' + old_hour_tens, '0' + new_hour_tens);
      }
      if (old_hour_ones != new_hour_ones) {
        triggerDigitTransition(1, '0' + old_hour_ones, '0' + new_hour_ones);
      }
      if (old_min_tens != new_min_tens) {
        triggerDigitTransition(3, '0' + old_min_tens, '0' + new_min_tens);
      }
      if (old_min_ones != new_min_ones) {
        triggerDigitTransition(4, '0' + old_min_ones, '0' + new_min_ones);
      }

      displayed_hour = new_hour;
      displayed_min = new_min;
    }
  }

  int seconds = timeinfo->tm_sec;

  // Check if any digit is currently breaking
  bool any_digit_breaking = false;
  int breaking_digit_index = -1;
  for (int i = 0; i < 5; i++) {
    if (digit_transitions[i].state == DIGIT_BREAKING) {
      any_digit_breaking = true;
      breaking_digit_index = i;
      break;
    }
  }

  // Multi-ball mode: ONLY when digit is breaking (not just at :55)
  if (any_digit_breaking && seconds >= MULTIBALL_ACTIVATE_SECOND) {
    // Multi-ball mode: spawn 2nd ball and reposition paddle
    // 1st ball continues normal play, 2nd ball is the "breaker"

    // Use the breaking digit as target
    int target_digit = breaking_digit_index;

    // Spawn 2nd ball if not active
    if (!pong_balls[1].active) {
      spawnPongBall(1);
      pong_balls[1].state = PONG_BALL_NORMAL;  // Immediately active (no spawn delay)
    }

    // Reposition paddle to be under the target digit
    if (target_digit >= 0) {
      breakout_paddle.target_x = DIGIT_X[target_digit] + 7;  // Center of digit
    }

    // Apply speed boost to both balls
    for (int i = 0; i < MAX_PONG_BALLS; i++) {
      if (pong_balls[i].active && pong_balls[i].state == PONG_BALL_NORMAL) {
        int speed = PONG_BALL_SPEED_BOOST;
        if (pong_balls[i].vx > 0) pong_balls[i].vx = speed;
        else pong_balls[i].vx = -speed;
        if (pong_balls[i].vy > 0) pong_balls[i].vy = speed;
        else pong_balls[i].vy = -speed;
      }
    }

  } else {
    // Normal mode: single ball, normal speed, paddle auto-tracks
    pong_balls[1].active = false;  // Deactivate 2nd ball

    // Normal speed for ball 0
    if (pong_balls[0].active && pong_balls[0].state == PONG_BALL_NORMAL) {
      int speed = settings.pongBallSpeed;
      if (pong_balls[0].vx > 0) pong_balls[0].vx = speed;
      else pong_balls[0].vx = -speed;
      if (pong_balls[0].vy > 0) pong_balls[0].vy = speed;
      else pong_balls[0].vy = -speed;
    }

    // Paddle returns to auto-tracking mode (handled by updateBreakoutPaddle)
  }

  // Update all active balls
  for (int i = 0; i < MAX_PONG_BALLS; i++) {
    if (pong_balls[i].active) {
      updatePongBall(i);
      checkPongCollisions(i);
    }
  }

  // Update game systems
  updateBreakoutPaddle();
  updateDigitTransitions();
  updatePongFragments();
  updateAssemblyFragments();  // Update assembling digit fragments
  updateDigitBouncePong();  // Spring-based bounce physics for Pong
}

// Main display function for Pong Clock
void displayClockWithPong() {
  // Initialize Pong on first call
  static bool pong_initialized = false;
  if (!pong_initialized) {
    initPongAnimation();
    pong_initialized = true;
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

  // Update animation
  updatePongAnimation(&timeinfo);

  // RENDERING ORDER (back to front):

  // 1. Date at top (textSize 1)
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

  // 2. Digits (with transitions and bounce)
  drawPongDigits();

  // 3. Fragments (digit breaks + ball hits)
  drawPongFragments();

  // 4. Paddles
  drawBreakoutPaddle();

  // 5. Ball(s) (on top)
  drawPongBall();
}

// ========== Pac-Man Clock Implementation ==========

void displayClockWithPacman() {
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

  // Update animation first
  updatePacmanAnimation(&timeinfo);

  // Time management
  if (!time_overridden) {
    displayed_hour = timeinfo.tm_hour;
    displayed_min = timeinfo.tm_min;
  }

  if (time_overridden && timeinfo.tm_hour == displayed_hour &&
      timeinfo.tm_min == displayed_min && pacman_state == PACMAN_PATROL) {
    time_overridden = false;
  }

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