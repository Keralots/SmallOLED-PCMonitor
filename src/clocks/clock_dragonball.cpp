/*
 * SmallOLED-PCMonitor - Dragon Ball Clock (clockStyle 19)
 *
 * The time hangs at the top of the screen and Goku lives on the ground below
 * it, seen from the side. He walks about and, with idle tricks on, also runs
 * karate kata, spars with his own afterimage, peppers a digit with small ki
 * blasts, fires blasts or a small Kamehameha straight ahead, rides the Flying
 * Nimbus, vanishes in a zanzoken blink, turns to the camera and powers up
 * inside a flickering aura while pebbles lift off the ground, or fights a
 * visitor: Vegeta flies in, Piccolo flickers in, they trade blows, clash
 * fists, swap ki blasts that collide or get swatted away, and lock beams -
 * the visitor's beam drives Goku back across the ground until he flares up
 * and blasts the visitor tumbling off screen. Rarely he raises a Genki Dama
 * from motes of energy all over the screen and hurls it over the horizon.
 * Dragon Balls turn up on the ground; he picks them up, and with all seven
 * Shenron rises from them and coils across the screen in front of the time.
 *
 * At :56, for one or two changed digits, he gets beside each one (blink, or a
 * run with tricks off), pulls his cupped hands back to his hip while a ki ball
 * swells, then fires a Kamehameha diagonally up into the digit from below. The
 * beam eats it from the bottom row up and bursts inside it, so it never
 * reaches the neighbours, and the new digit condenses out of scattered light.
 *
 * When three or four digits change he goes Super Saiyan instead: hair flaring,
 * lightning, everything shaking, the old digits crumble to rubble, and a bolt
 * from him strikes each new digit into place before he powers down.
 *
 * Sprites are text bitmaps facing right, mirrored for left. All state is
 * file-local; resetDragonBallAnimation() returns it to baseline.
 */

#include "../config/config.h"
#include "../display/display.h"
#include "clocks.h"
#include "clock_globals.h"
#include <math.h>
#include <string.h>

extern bool httpForceClock;

#define DB_DIGIT_Y 2
#define DB_DIGIT_BOTTOM 22
#define DB_GROUND_Y 63
#define DB_FEET_Y 62
#define DB_TRIGGER_SECOND 56
#define DB_SIDE_W 20
#define DB_SIDE_H 24
#define DB_FRONT_W 14
#define DB_FRONT_H 24
#define DB_HAND_X 15             // fire sprite hands, facing right
#define DB_HAND_Y 13
#define DB_FIST_X 15             // punch/kick sprite fist and foot
#define DB_FIST_Y 14
#define DB_KAME_X 1              // ki ball at the back hip, facing right
#define DB_KAME_Y 16
#define DB_AIM_DX 24             // hands stand this far beside the digit centre
#define DB_TARGET_Y 12           // the beam bursts at the digit centre
#define DB_BEAM_SPEED 220.0f
#define DB_WALK_SPEED 18.0f
#define DB_RUN_SPEED 90.0f
#define DB_KI_SPEED 150.0f
#define DB_MAX_SPARKS 40
#define DB_MAX_KI 8
#define DB_MAX_RUBBLE 100
#define DB_PHASE_TIMEOUT 4.0f
#define DB_SSJ_MIN_CHANGES 3

static const uint8_t DB_GLYPHS[10][5] = {
  {0x3E, 0x51, 0x49, 0x45, 0x3E}, {0x00, 0x42, 0x7F, 0x40, 0x00},
  {0x72, 0x49, 0x49, 0x49, 0x46}, {0x21, 0x41, 0x49, 0x4D, 0x33},
  {0x18, 0x14, 0x12, 0x7F, 0x10}, {0x27, 0x45, 0x45, 0x45, 0x39},
  {0x3C, 0x4A, 0x49, 0x49, 0x31}, {0x41, 0x21, 0x11, 0x09, 0x07},
  {0x36, 0x49, 0x49, 0x49, 0x36}, {0x46, 0x49, 0x49, 0x29, 0x1E},
};

// ---- Side view, 20x24, facing right ----
static const char *const SPR_STAND[] = {
  "...X.X..............", "X..XXXX.X...........",
  "XX.XXXXXXX..........", ".XXXXXXXXXX.........",
  "XXXXXXXXXXXX........", ".XXXXXXXXXXX........",
  "XXXXXXXXXXXXX.......", "..XXXXXXX.XX........",
  ".XXXXXX...X.........", "...XXX..X..X........",
  "....XX.....X........", ".....X...XX.........",
  "......XXX...........", ".....XXXXX..........",
  "....XXXXXXX.........", "....XXX.XXX.........",
  "....XXX.XXX.........", ".....XX.XXX.........",
  ".....X....X.........", ".....XXXXX..........",
  ".....XXXXX..........", ".....XX.XX..........",
  ".....XX.XX..........", "....XXX.XXX........."};

static const char *const SPR_WALK_A[] = {
  "...X.X..............", "X..XXXX.X...........",
  "XX.XXXXXXX..........", ".XXXXXXXXXX.........",
  "XXXXXXXXXXXX........", ".XXXXXXXXXXX........",
  "XXXXXXXXXXXXX.......", "..XXXXXXX.XX........",
  ".XXXXXX...X.........", "...XXX..X..X........",
  "....XX.....X........", ".....X...XX.........",
  "......XXX...........", ".....XXXXX..........",
  "....XXXXXXX.........", "....XXXXXXXX........",
  "....XXXX..XXX.......", ".....XX....XX.......",
  ".....X....X.........", ".....XXXXX..........",
  "....XXX.XXX.........", "...XXX...XXX........",
  "..XX.......XX.......", ".XXX.......XXX......"};

static const char *const SPR_WALK_B[] = {
  "...X.X..............", "X..XXXX.X...........",
  "XX.XXXXXXX..........", ".XXXXXXXXXX.........",
  "XXXXXXXXXXXX........", ".XXXXXXXXXXX........",
  "XXXXXXXXXXXXX.......", "..XXXXXXX.XX........",
  ".XXXXXX...X.........", "...XXX..X..X........",
  "....XX.....X........", ".....X...XX.........",
  "......XXX...........", ".....XXXXX..........",
  "....XXXXXXX.........", "....XXX.XXX.........",
  "....XXX.XXX.........", ".....XX.XXX.........",
  ".....X....X.........", ".....XXXXX..........",
  ".....XXXX...........", "......XXX...........",
  "......XXX...........", ".....XXXX..........."};

static const char *const SPR_WALK_C[] = {
  "...X.X..............", "X..XXXX.X...........",
  "XX.XXXXXXX..........", ".XXXXXXXXXX.........",
  "XXXXXXXXXXXX........", ".XXXXXXXXXXX........",
  "XXXXXXXXXXXXX.......", "..XXXXXXX.XX........",
  ".XXXXXX...X.........", "...XXX..X..X........",
  "....XX.....X........", ".....X...XX.........",
  "......XXX...........", ".....XXXXX..........",
  "...XXXXXXX..........", "..XXXXXXXX..........",
  ".XXX.XXXXX..........", ".XX..XXXX...........",
  ".....X....X.........", ".....XXXXX..........",
  "....XXX.XXX.........", "...XXX...XXX........",
  "..XX.......XX.......", ".XXX.......XXX......"};

static const char *const SPR_PUNCH[] = {
  "...X.X..............", "X..XXXX.X...........",
  "XX.XXXXXXX..........", ".XXXXXXXXXX.........",
  "XXXXXXXXXXXX........", ".XXXXXXXXXXX........",
  "XXXXXXXXXXXXX.......", "..XXXXXXX.XX........",
  ".XXXXXX...X.........", "...XXX..X..X........",
  "....XX.....X........", ".....X...XX.........",
  "......XXX...........", ".....XXXXX..........",
  "....XXXXXXXXXXXX....", "....XXXXXXXXXXXX....",
  "....XXXX............", ".....XXX............",
  ".....X....X.........", ".....XXXXX..........",
  "....XXX.XXX.........", "...XXX...XXX........",
  "..XX.......XX.......", ".XXX.......XXX......"};

static const char *const SPR_KICK[] = {
  "...X.X..............", "X..XXXX.X...........",
  "XX.XXXXXXX..........", ".XXXXXXXXXX.........",
  "XXXXXXXXXXXX........", ".XXXXXXXXXXX........",
  "XXXXXXXXXXXXX.......", "..XXXXXXX.XX........",
  ".XXXXXX...X.........", "...XXX..X..X........",
  "....XX.....X........", ".....X...XX.........",
  "......XXX...........", "....XXXXX...........",
  "...XXXXXXX..........", "..XXXXXXXX..........",
  "..XX.XXXXX..........", ".....XXXXXXXXXXX....",
  ".....X..XXXXXXXX....", ".....XXX............",
  ".....XX.............", ".....XX.............",
  ".....XX.............", "....XXX............."};

static const char *const SPR_KAME[] = {
  "...X.X..............", "X..XXXX.X...........",
  "XX.XXXXXXX..........", ".XXXXXXXXXX.........",
  "XXXXXXXXXXXX........", ".XXXXXXXXXXX........",
  "XXXXXXXXXXXXX.......", "..XXXXXXX.XX........",
  ".XXXXXX...X.........", "...XXX..X..X........",
  "....XX.....X........", ".....X...XX.........",
  "......XXX...........", ".....XXXXX..........",
  "...XXXXXXX..........", ".XXXXXXXXX..........",
  "XXX.XXXXXX..........", "XX...XXXX...........",
  ".....X....X.........", ".....XXXXX..........",
  "....XXX.XXX.........", "...XXX...XXX........",
  "..XX.......XX.......", ".XXX.......XXX......"};

static const char *const SPR_FIRE[] = {
  "...X.X..............", "X..XXXX.X...........",
  "XX.XXXXXXX..........", ".XXXXXXXXXX.........",
  "XXXXXXXXXXXX........", ".XXXXXXXXXXX........",
  "XXXXXXXXXXXXX.......", "..XXXXXXX.XX........",
  ".XXXXXX...X.........", "...XXX..X..X........",
  "....XX.....X........", ".....X...XX.........",
  "......XXX...........", ".....XXXXX....XX....",
  "....XXXXXXX.XXX.....", "....XXXXXXXXXX......",
  "....XXXXXXXX........", ".....XXXX...........",
  ".....X....X.........", ".....XXXXX..........",
  "....XXX.XXX.........", "...XXX...XXX........",
  "..XX.......XX.......", ".XXX.......XXX......"};
// ---- Front view, 14 wide: power-up (24 rows) and Super Saiyan (25 rows) ----
static const char *const SPR_POWER[] = {
  "...X.....X....", "...XX...XX..X.",
  ".X.XXX.XXX.XX.", ".XXXXXXXXXXXX.",
  "XXXXXXXXXXXXXX", ".XXXXXXXXXXXX.",
  "XXXXXXXXXXXXXX", "XXX.XXX.XX.XXX",
  "..X........X..", "..X.XX..XX.X..",
  "..X..X..X..X..", "...X......X...",
  "....X.XX.X....", ".....XXXX.....",
  "...XXX..XXX...", ".XXXXXXXXXXXX.",
  "XXX.XXXXXX.XXX", "XX..XXXXXX..XX",
  ".XX.XXXXXX.XX.", "..XX......XX..",
  "....XXXXXX....", "...XXX..XXX...",
  "..XXX....XXX..", ".XXX......XXX."};

static const char *const SPR_SSJ[] = {
  "..X...X...X...", "..XX..XX.XX.X.",
  "X.X.X.X.X..XX.", "XX...X......X.",
  ".X...........X", "X............X",
  ".X..........X.", "X..X...X..X..X",
  "XXX.XXX.XX.XXX", "..X........X..",
  "..X.XX..XX.X..", "..X..X..X..X..",
  "...X......X...", "....X.XX.X....",
  ".....XXXX.....", "...XXX..XXX...",
  ".XXXXXXXXXXXX.", "XXX.XXXXXX.XXX",
  "XX..XXXXXX..XX", ".XX.XXXXXX.XX.",
  "..XX......XX..", "....XXXXXX....",
  "...XXX..XXX...", "..XXX....XXX..",
  ".XXX......XXX."};

// ---- Hurt pose for Goku, and the visitors (Vegeta 26 rows, Piccolo 24) ----
static const char *const SPR_HURT[] = {
  "...X.X..............", "X..XXXX.X...........",
  "XX.XXXXXXX..........", ".XXXXXXXXXX.........",
  "XXXXXXXXXXXX........", ".XXXXXXXXXXX........",
  "XXXXXXXXXXXXX.......", "..XXXXXXX.XX........",
  ".XXXXXX...X.........", "...XXX..X..X........",
  "....XX.....X........", ".....X...XX.........",
  "......XXX...........", ".....XXXXX..........",
  "..XXXXXXX...........", ".XX.XXXXX...........",
  "X...XXXXX...........", ".....XXX............",
  ".....X....X.........", ".....XXXXX..........",
  "....XXX.XXX.........", "...XXX...XXX........",
  "..XX.......XX.......", ".XXX.......XXX......"};

static const char *const VEG_STAND[] = {
  "......X.............", ".....XX..X..........",
  "....XXX.XX..........", "...XXXXXXX..........",
  "...XXXXXXX..X.......", "..XXXXXXXXXXX.......",
  "..XXXXXXXXXX........", ".XXXXXXXXXXX........",
  ".XXXXXXXXXX.........", "..XXXXXX.X..........",
  "...XXXX...X.........", "...XXX.XX..X........",
  "....XX.....X........", ".....X...XX.........",
  "......XXX...........", ".....XXXXX..........",
  "....XXXXXXX.........", "....XXX.XXX.........",
  "....XXX.XXX.........", ".....XX.XXX.........",
  ".....X....X.........", ".....XXXXX..........",
  ".....XXXXX..........", ".....XX.XX..........",
  ".....XX.XX..........", "....XXX.XXX........."};

static const char *const PIC_STAND[] = {
  "...........X........", "....XXXX..X.........",
  "..XXXXXXXXX.........", ".XXXXXXXXXXX........",
  ".XXXXXXXXXXX........", ".X........X.........",
  ".XXXXXXXXXX.........", "..X.......X.........",
  "X.X...X....X........", ".XX........X........",
  "..X.......X.........", "...X...XX.X.........",
  "....XXXX............", "...X.XXXXX..........",
  "...XXXXXXXX.........", "...XXXX.XXX.........",
  "..XXXXX.XXX.........", "..XX.XX.XXX.........",
  "..XX.X....X.........", ".XXX.XXXXX..........",
  ".XXX.XXXXX..........", ".XXX.XX.XX..........",
  "XXXX.XX.XX..........", "....XXX.XXX........."};

static const char *const VEG_PUNCH[] = {
  "......X.............", ".....XX..X..........",
  "....XXX.XX..........", "...XXXXXXX..........",
  "...XXXXXXX..X.......", "..XXXXXXXXXXX.......",
  "..XXXXXXXXXX........", ".XXXXXXXXXXX........",
  ".XXXXXXXXXX.........", "..XXXXXX.X..........",
  "...XXXX...X.........", "...XXX.XX..X........",
  "....XX.....X........", ".....X...XX.........",
  "......XXX...........", ".....XXXXX..........",
  "....XXXXXXXXXXXX....", "....XXXXXXXXXXXX....",
  "....XXXX............", ".....XXX............",
  ".....X....X.........", ".....XXXXX..........",
  "....XXX.XXX.........", "...XXX...XXX........",
  "..XX.......XX.......", ".XXX.......XXX......"};

static const char *const PIC_PUNCH[] = {
  "...........X........", "....XXXX..X.........",
  "..XXXXXXXXX.........", ".XXXXXXXXXXX........",
  ".XXXXXXXXXXX........", ".X........X.........",
  ".XXXXXXXXXX.........", "..X.......X.........",
  "X.X...X....X........", ".XX........X........",
  "..X.......X.........", "...X...XX.X.........",
  "....XXXX............", "...X.XXXXX..........",
  "...XXXXXXXXXXXXX....", "...XXXXXXXXXXXXX....",
  "..XXXXXX............", "..XX.XXX............",
  "..XX.X....X.........", ".XXX.XXXXX..........",
  ".XXXXXX.XXX.........", ".XXXXX...XXX........",
  "XXXX.......XX.......", ".XXX.......XXX......"};

static const char *const VEG_KICK[] = {
  "......X.............", ".....XX..X..........",
  "....XXX.XX..........", "...XXXXXXX..........",
  "...XXXXXXX..X.......", "..XXXXXXXXXXX.......",
  "..XXXXXXXXXX........", ".XXXXXXXXXXX........",
  ".XXXXXXXXXX.........", "..XXXXXX.X..........",
  "...XXXX...X.........", "...XXX.XX..X........",
  "....XX.....X........", ".....X...XX.........",
  "......XXX...........", "....XXXXX...........",
  "...XXXXXXX..........", "..XXXXXXXX..........",
  "..XX.XXXXX..........", ".....XXXXXXXXXXX....",
  ".....X..XXXXXXXX....", ".....XXX............",
  ".....XX.............", ".....XX.............",
  ".....XX.............", "....XXX............."};

static const char *const PIC_KICK[] = {
  "...........X........", "....XXXX..X.........",
  "..XXXXXXXXX.........", ".XXXXXXXXXXX........",
  ".XXXXXXXXXXX........", ".X........X.........",
  ".XXXXXXXXXX.........", "..X.......X.........",
  "X.X...X....X........", ".XX........X........",
  "..X.......X.........", "...X...XX.X.........",
  "....XXXX............", "...XXXXXX...........",
  "...XXXXXXX..........", "..XXXXXXXX..........",
  "..XX.XXXXX..........", "..XX.XXXXXXXXXXX....",
  "..XX.X..XXXXXXXX....", ".XXX.XXX............",
  ".XXX.XX.............", ".XXX.XX.............",
  "XXXX.XX.............", "....XXX............."};

static const char *const VEG_HURT[] = {
  "......X.............", ".....XX..X..........",
  "....XXX.XX..........", "...XXXXXXX..........",
  "...XXXXXXX..X.......", "..XXXXXXXXXXX.......",
  "..XXXXXXXXXX........", ".XXXXXXXXXXX........",
  ".XXXXXXXXXX.........", "..XXXXXX.X..........",
  "...XXXX...X.........", "...XXX.XX..X........",
  "....XX.....X........", ".....X...XX.........",
  "......XXX...........", ".....XXXXX..........",
  "..XXXXXXX...........", ".XX.XXXXX...........",
  "X...XXXXX...........", ".....XXX............",
  ".....X....X.........", ".....XXXXX..........",
  "....XXX.XXX.........", "...XXX...XXX........",
  "..XX.......XX.......", ".XXX.......XXX......"};

static const char *const PIC_HURT[] = {
  "...........X........", "....XXXX..X.........",
  "..XXXXXXXXX.........", ".XXXXXXXXXXX........",
  ".XXXXXXXXXXX........", ".X........X.........",
  ".XXXXXXXXXX.........", "..X.......X.........",
  "X.X...X....X........", ".XX........X........",
  "..X.......X.........", "...X...XX.X.........",
  "....XXXX............", "...X.XXXXX..........",
  "..XXXXXXX...........", ".XXXXXXXX...........",
  "X.XXXXXXX...........", "..XX.XXX............",
  "..XX.X....X.........", ".XXX.XXXXX..........",
  ".XXXXXX.XXX.........", ".XXXXX...XXX........",
  "XXXX.......XX.......", ".XXX.......XXX......"};

// ---- Crouched beam-struggle pose, Genki Dama pose (front, 14x27), Shenron head ----
static const char *const SPR_STRAIN[] = {
  "....................", "....................",
  "...X.X..............", "X..XXXX.X...........",
  "XX.XXXXXXX..........", ".XXXXXXXXXX.........",
  "XXXXXXXXXXXX........", ".XXXXXXXXXXX........",
  "XXXXXXXXXXXXX.......", "..XXXXXXX.XX........",
  ".XXXXXX...X.........", "...XXX..X..X........",
  "....XX.....X........", ".....X...XX.........",
  "......XXX...........", ".....XXXXX..........",
  "....XXXXXXXXXXXX....", "....XXXXXXXXXXXX....",
  "....XXXX............", ".....XXX............",
  ".....X....X.........", "...XXXXXXX..........",
  "..XX.....XX.........", ".XXX.....XXX........"};

static const char *const SPR_GENKI[] = {
  ".XX........XX.", ".XX........XX.",
  "..X........X..", "...X.....X....",
  "...XX...XX..X.", ".X.XXX.XXX.XX.",
  ".XXXXXXXXXXXX.", "XXXXXXXXXXXXXX",
  ".XXXXXXXXXXXX.", "XXXXXXXXXXXXXX",
  "XXX.XXX.XX.XXX", ".XX........XX.",
  ".XX.XX..XX.XX.", ".XX..X..X..XX.",
  ".X.X......X.X.", ".X..X.XX.X..X.",
  ".X...XXXX...X.", "..XXXXXXXXXX..",
  "...XXXXXXXX...", "...XXXXXXXX...",
  "....XXXXXX....", "....X....X....",
  "....XXXXXX....", "...XXX..XXX...",
  "..XXX....XXX..", ".XXX......XXX.",
  ".XX........XX."};

static const char *const SHENRON_HEAD[] = {
  "..X...X...........", "...X.X............",
  "....XXXXXX........", "...XXXXXXXXXX.....",
  "..XXX.XXXXXXXXX...", "..XXXXXXXXXXXXXXX.",
  "..XXXXXX..........", "..XXXXXXX.XX.X.X..",
  "...XXXXXXXXXXXX...", "....XXXXX........."};

enum DbPhase {
  DB_IDLE, DB_BLINK, DB_RUN, DB_CHARGE, DB_FIRE, DB_RETRACT, DB_FORM,
  DB_SSJ_UP, DB_SSJ_CRUMBLE, DB_SSJ_STRIKE, DB_SSJ_DOWN
};
enum DbIdle {
  IDLE_WALK, IDLE_PAUSE, IDLE_KATA, IDLE_SPAR, IDLE_POWER,
  IDLE_VOLLEY, IDLE_BLASTS, IDLE_BEAM, IDLE_NIMBUS, IDLE_FIGHT,
  IDLE_GENKI, IDLE_FETCH, IDLE_SHENRON
};

// ---- Visitors ----
enum DbPose { POSE_STAND, POSE_PUNCH, POSE_KICK, POSE_HURT, POSE_STRAIN };
enum DbFoeKind { FOE_VEGETA, FOE_PICCOLO };
enum DbFight {
  FIGHT_ARRIVE, FIGHT_BEATS, FIGHT_VOLLEY, FIGHT_CHARGE, FIGHT_STRUGGLE, FIGHT_KO, FIGHT_WIN
};

// Ki exchange before the beams: when each fighter shoots (1 = Goku, 2 = visitor).
// Pairs at the same moment meet in the middle.
struct DbShot { float t; uint8_t who; };
static const DbShot FIGHT_VOLLEY_SHOTS[] = {
  {0.00f, 1}, {0.30f, 2}, {0.65f, 1}, {0.65f, 2},
  {1.05f, 2}, {1.35f, 1}, {1.60f, 1}, {1.60f, 2},
};

static const char *const *const FOE_POSES[2][4] = {
  {VEG_STAND, VEG_PUNCH, VEG_KICK, VEG_HURT},
  {PIC_STAND, PIC_PUNCH, PIC_KICK, PIC_HURT},
};
static const int FOE_H[2] = {26, 24};

// One exchange: what each fighter does, how far each moves toward the other
// (negative = knocked back), and the effect at the point of contact.
struct DbBeat {
  uint8_t goku, foe;
  float secs;
  int8_t gokuStep, foeStep;
  uint8_t fx;       // 0 none, 1 hit star, 2 fist clash (big flash + shake)
};

static const DbBeat FIGHT_SCRIPT[] = {
  {POSE_STAND, POSE_STAND, 0.55f, 0, 0, 0},
  {POSE_PUNCH, POSE_HURT, 0.25f, 2, -3, 1},
  {POSE_STAND, POSE_STAND, 0.15f, 0, 0, 0},
  {POSE_HURT, POSE_PUNCH, 0.25f, -3, 2, 1},
  {POSE_STAND, POSE_STAND, 0.12f, 0, 0, 0},
  {POSE_KICK, POSE_HURT, 0.30f, 1, -3, 1},
  {POSE_HURT, POSE_KICK, 0.30f, -3, 1, 1},
  {POSE_STAND, POSE_STAND, 0.15f, 0, 0, 0},
  {POSE_PUNCH, POSE_PUNCH, 0.30f, 3, 3, 2},
  {POSE_STAND, POSE_STAND, 0.20f, -4, -4, 0},
  {POSE_STAND, POSE_STAND, 0.40f, 0, 0, 0},
};

#define DB_FIGHT_GAP 17          // sprite-left to sprite-left at the start
enum DbSlot { SLOT_SOLID, SLOT_ERASING, SLOT_GONE, SLOT_FORMING };

struct DbSpark {
  bool active;
  float x, y, vx, vy, life;
};

struct DbKi {
  bool active;
  float x, y, vx, vy;
  int8_t target;    // digit slot it is aimed at, -1 = flies off screen
  uint8_t owner;    // 0 idle shot, 1 Goku in a fight, 2 visitor, 3 swatted away
};

// One beat of a kata or sparring routine.
struct DbMove {
  const char *const *pose;
  float secs;
  int8_t step;      // px moved forward during the beat
  int8_t jump;      // arc height, 0 = grounded
  bool turn;        // face the other way at the end of the beat
  bool hit;         // impact flash at the end of the limb
};

static const DbMove KATA[] = {
  {SPR_STAND, 0.30f, 0, 0, false, false},
  {SPR_PUNCH, 0.22f, 1, 0, false, true},
  {SPR_STAND, 0.14f, 0, 0, false, false},
  {SPR_PUNCH, 0.22f, 1, 0, false, true},
  {SPR_KICK, 0.35f, 0, 0, false, true},
  {SPR_STAND, 0.25f, 0, 0, true, false},
  {SPR_PUNCH, 0.22f, 1, 0, false, true},
  {SPR_KICK, 0.65f, 12, 10, false, true},
  {SPR_STAND, 0.45f, 0, 0, false, false},
};

static const DbMove SPAR[] = {
  {SPR_STAND, 0.25f, 0, 0, false, false},
  {SPR_PUNCH, 0.18f, 2, 0, false, true},
  {SPR_STAND, 0.12f, -2, 0, false, false},
  {SPR_KICK, 0.25f, 0, 0, false, true},
  {SPR_STAND, 0.12f, 0, 0, false, false},
  {SPR_PUNCH, 0.18f, 2, 0, false, true},
  {SPR_PUNCH, 0.18f, 0, 0, false, true},
  {SPR_KICK, 0.30f, -2, 0, false, true},
  {SPR_STAND, 0.30f, 0, 0, false, false},
};

#define DB_LEN(a) (int)(sizeof(a) / sizeof(a[0]))

static DbPhase db_phase = DB_IDLE;
static DbIdle db_idle = IDLE_PAUSE;
static float db_phase_t = 0.0f;
static float db_x = 40.0f;                       // sprite left edge
static int db_dir = 1;
static float db_lift = 0.0f;                     // feet above the ground
static float db_walk_to = 40.0f;
static float db_walk_dist = 0.0f;                // drives the leg cycle
static int db_move = 0;                          // index into kata/spar
static float db_move_t = 0.0f;
static float db_move_x0 = 0.0f;
static float db_flash_t = 0.0f;                  // impact star
static int db_flash_x = 0, db_flash_y = 0;
static float db_clock = 0.0f;

static float db_ghost_t = 0;                     // zanzoken afterimage
static float db_ghost_x = 0;
static int db_ghost_dir = 1;
static const char *const *db_ghost_pose = SPR_STAND;

// Beam geometry, shared by the minute-change shot and the idle forward beam.
static float db_beam = 0, db_tail = 0, db_beam_len = 0;
static float db_hx = 0, db_hy = 0, db_ux = 0, db_uy = 0;
static int db_beam_clip = -1;                    // digit slot the beam may enter
static float db_burst_t = 0;

static DbKi db_ki[DB_MAX_KI];
static int db_shots_left = 0;
static float db_shot_timer = 0;
static int db_volley_target = 0;

static float db_cloud_x = 0;                     // Flying Nimbus
static int db_nimbus_stage = 0;
static float db_nimbus_to = 0;
static int db_nimbus_legs = 0;

static DbFoeKind db_foe = FOE_VEGETA;            // fight visitor
static DbFight db_fight = FIGHT_ARRIVE;
static float db_foe_x = 0, db_foe_lift = 0;
static float db_foe_vx = 0, db_foe_vy = 0;
static DbPose db_goku_pose = POSE_STAND, db_foe_pose = POSE_STAND;
static int db_beat = 0;
static float db_beat_t = 0;
static float db_beat_g0 = 0, db_beat_f0 = 0;
static float db_clash_x = 0;
static float db_shock_t = 0;                     // clash shake
static int db_now_sec = 0;
static int db_shot = 0;                          // next entry in the ki exchange
static float db_goku_act = 0, db_foe_act = 0;   // brief punch pose after a shot or swat
static float db_flare_t = 0;                     // Goku's comeback burst

#define DB_MAX_MOTES 30
static DbSpark db_motes[DB_MAX_MOTES];           // Genki Dama energy
static float db_genki_x = 0, db_genki_y = 0, db_genki_r = 0;
static bool db_genki_thrown = false;
static float db_boom_t = 0;                      // explosion over the horizon
static int db_boom_x = 0, db_boom_y = 0;

static int db_ball_x = -1;                       // Dragon Ball on the ground
static int db_balls = 0;                         // collected so far
static float db_ball_timer = 45.0f;
static float db_pips_t = 0;                      // collected-count display
static float db_scatter_x[7], db_scatter_y[7], db_scatter_vx[7], db_scatter_vy[7];

static DbSlot slot_mode[5];
static int slot_rows_gone[5];
static uint8_t slot_value[5];
static uint8_t slot_new[5];
static float slot_form_t0[5];
static float slot_reveal[5][35];
static float slot_shake[5];

static DbSpark db_sparks[DB_MAX_SPARKS];
static DbSpark db_rubble[DB_MAX_RUBBLE];

static int db_change_idx[4];
static uint8_t db_change_val[4];
static int db_num_changes = 0;
static int db_cur = 0;

static unsigned long last_db_update = 0;
static int last_minute_db = -1;
static bool db_triggered = false;
static bool db_init_done = false;

// ========== Helpers ==========
static float dbRandf(float lo, float hi) {
  return lo + (hi - lo) * (random(0, 1001) / 1000.0f);
}

static bool dbGlyph(uint8_t v, int c, int r) {
  return (DB_GLYPHS[v][c] >> r) & 1;
}

static void dbPush(DbSpark *pool, int n, float x, float y, float vx, float vy, float life) {
  for (int i = 0; i < n; i++) {
    if (pool[i].active) continue;
    pool[i] = {true, x, y, vx, vy, life};
    return;
  }
}

static void dbSpark(float x, float y, float vx, float vy, float life) {
  dbPush(db_sparks, DB_MAX_SPARKS, x, y, vx, vy, life);
}

static int dbSideTop() { return DB_FEET_Y - DB_SIDE_H + 1; }

// Pixel column of a sprite column, honouring the facing direction.
static float dbCol(int col) {
  return db_x + (db_dir > 0 ? col : DB_SIDE_W - 1 - col);
}

static float dbClampX(float x) {
  if (x < 0) return 0;
  if (x > SCREEN_WIDTH - DB_SIDE_W) return SCREEN_WIDTH - DB_SIDE_W;
  return x;
}

static void dbLeaveGhost(const char *const *pose) {
  db_ghost_t = 0.45f;
  db_ghost_x = db_x;
  db_ghost_dir = db_dir;
  db_ghost_pose = pose;
}

static void dbIdlePause() {
  db_idle = IDLE_PAUSE;
  db_phase_t = 0;
}

static void dbStartWalk() {
  db_idle = IDLE_WALK;
  db_walk_to = dbRandf(0, SCREEN_WIDTH - DB_SIDE_W);
  db_dir = db_walk_to >= db_x ? 1 : -1;
}

static void dbStartRoutine(DbIdle kind) {
  db_idle = kind;
  db_move = 0;
  db_move_t = 0;
  db_move_x0 = db_x;
  db_phase_t = 0;
  // Keep a routine's travel on screen.
  if (db_dir > 0 && db_x > SCREEN_WIDTH - DB_SIDE_W - 30) db_dir = -1;
  if (db_dir < 0 && db_x < 30) db_dir = 1;
}

static void dbFireKi(float x, float y, float tx, float ty, int target) {
  float dx = tx - x, dy = ty - y, d = sqrtf(dx * dx + dy * dy);
  for (int i = 0; i < DB_MAX_KI; i++) {
    if (db_ki[i].active) continue;
    db_ki[i] = {true, x, y, dx / d * DB_KI_SPEED, dy / d * DB_KI_SPEED, (int8_t)target, 0};
    return;
  }
}

// Fighter edges, fist height, and where the two of them meet.
static float dbFoeDir() { return -db_dir; }
static int dbFistY() { return dbSideTop() + DB_FIST_Y; }
static float dbGokuFront() { return dbCol(DB_FIST_X); }
static float dbFoeFront() {
  return db_foe_x + (dbFoeDir() > 0 ? DB_FIST_X : DB_SIDE_W - 1 - DB_FIST_X);
}

static void dbStartFight(DbFoeKind foe) {
  // Goku turns toward the open side; the visitor takes the spot ahead of him.
  // He also needs room behind him: the visitor's beam drives him back.
  db_dir = db_x < (SCREEN_WIDTH - DB_SIDE_W) / 2 ? 1 : -1;
  if (db_dir > 0) db_x = fminf(fmaxf(db_x, 18), SCREEN_WIDTH - DB_SIDE_W - DB_FIGHT_GAP - 8);
  else db_x = fmaxf(fminf(db_x, SCREEN_WIDTH - DB_SIDE_W - 18), DB_FIGHT_GAP + 8);
  db_foe = foe;
  db_idle = IDLE_FIGHT;
  db_fight = FIGHT_ARRIVE;
  db_phase_t = 0;
  db_goku_pose = db_foe_pose = POSE_STAND;
  db_shot = 0;
  db_goku_act = db_foe_act = 0;
  float spot = db_x + db_dir * DB_FIGHT_GAP;
  db_foe_vx = spot;  // landing spot, kept here during the arrival
  if (db_foe == FOE_VEGETA) {
    db_foe_x = db_dir > 0 ? SCREEN_WIDTH + 4 : -DB_SIDE_W - 4;
    db_foe_lift = 22;
  } else {
    db_foe_x = spot;
    db_foe_lift = 0;
  }
}

static void dbStartGenki() {
  db_idle = IDLE_GENKI;
  db_phase_t = 0;
  db_genki_thrown = false;
  db_genki_r = 0;
  db_x = fminf(fmaxf(db_x, 8), SCREEN_WIDTH - DB_SIDE_W - 8);
  db_dir = db_x < (SCREEN_WIDTH - DB_SIDE_W) / 2 ? 1 : -1;  // hurl it toward the far side
  db_genki_x = db_x + DB_SIDE_W / 2;
  db_genki_y = 27;
  memset(db_motes, 0, sizeof(db_motes));
}

// The seven balls line up in the middle of the ground.
static int dbRowBallX(int i) { return SCREEN_WIDTH / 2 - 18 + i * 6; }

static void dbStartShenron() {
  db_idle = IDLE_SHENRON;
  db_phase_t = 0;
  db_ball_x = -1;
  // Step aside so the balls have the middle of the ground.
  float cx = db_x + DB_SIDE_W / 2;
  if (cx > 36 && cx < SCREEN_WIDTH - 36) {
    dbLeaveGhost(SPR_STAND);
    db_x = cx < SCREEN_WIDTH / 2 ? 4 : SCREEN_WIDTH - DB_SIDE_W - 4;
  }
  db_dir = db_x < SCREEN_WIDTH / 2 ? 1 : -1;
  for (int i = 0; i < 7; i++) {
    db_scatter_x[i] = dbRowBallX(i);
    db_scatter_y[i] = DB_GROUND_Y - 3;
    float a = -2.6f + i * 0.35f;  // fan out upward
    db_scatter_vx[i] = cosf(a) * 110;
    db_scatter_vy[i] = sinf(a) * 110;
  }
}

// Everything Goku can do between minutes, plus the two minute-change shows
// (replayed on the digits already showing) for the demo endpoint.
enum DbAct {
  ACT_WALK, ACT_KATA, ACT_SPAR, ACT_VOLLEY, ACT_BLASTS, ACT_BEAM, ACT_NIMBUS,
  ACT_BLINK, ACT_POWER, ACT_FIGHT_VEGETA, ACT_FIGHT_PICCOLO, ACT_GENKI,
  ACT_FETCH, ACT_SHENRON, ACT_SHOT, ACT_SSJ, ACT_COUNT
};

static int db_demo_step = -1;                    // next demo act, -1 = off
static int db_demo_last = -1;                    // last act to play
static bool db_demo_on = false;
static int db_demo_prev_style = -1;              // restored when the demo ends
static bool db_demo_prev_force = false;
static unsigned long db_last_draw = 0;

static void dbStartDemoChange(int count);

static void dbStartAct(int act) {
  switch (act) {
    case ACT_WALK:
      dbStartWalk();
      break;
    case ACT_KATA:
      dbStartRoutine(IDLE_KATA);
      break;
    case ACT_SPAR:
      dbStartRoutine(IDLE_SPAR);
      break;
    case ACT_VOLLEY: {
      // Volley at a random digit: face it, then fire in ragged bursts.
      static const int slots[4] = {0, 1, 3, 4};
      db_volley_target = slots[random(0, 4)];
      db_dir = DIGIT_X[db_volley_target] + 7 >= db_x + DB_SIDE_W / 2 ? 1 : -1;
      db_idle = IDLE_VOLLEY;
      db_shots_left = random(3, 7);
      db_shot_timer = 0.2f;
      break;
    }
    case ACT_BLASTS:
    case ACT_BEAM:
      if (db_dir > 0 && db_x > SCREEN_WIDTH - 50) db_dir = -1;
      if (db_dir < 0 && db_x < 30) db_dir = 1;
      if (act == ACT_BLASTS) {
        db_idle = IDLE_BLASTS;
        db_shots_left = random(3, 6);
        db_shot_timer = 0.15f;
      } else {
        db_idle = IDLE_BEAM;
        db_phase_t = 0;
        db_beam = db_tail = 0;
      }
      break;
    case ACT_NIMBUS:
      // Nimbus swoops in from the side he is facing away from.
      db_idle = IDLE_NIMBUS;
      db_nimbus_stage = 0;
      db_cloud_x = db_dir > 0 ? -26 : SCREEN_WIDTH + 2;
      db_phase_t = 0;
      break;
    case ACT_BLINK:
      dbLeaveGhost(SPR_STAND);
      db_x = dbRandf(0, SCREEN_WIDTH - DB_SIDE_W);
      db_dir = random(0, 2) ? 1 : -1;
      dbIdlePause();
      break;
    case ACT_POWER:
      db_idle = IDLE_POWER;
      db_phase_t = 0;
      break;
    case ACT_FIGHT_VEGETA:
    case ACT_FIGHT_PICCOLO:
      dbStartFight(act == ACT_FIGHT_VEGETA ? FOE_VEGETA : FOE_PICCOLO);
      break;
    case ACT_GENKI:
      dbStartGenki();
      break;
    case ACT_FETCH:
      if (db_ball_x < 0) {
        float cx = db_x + DB_SIDE_W / 2;
        db_ball_x = (int)(cx < SCREEN_WIDTH / 2 ? cx + 30 : cx - 30);
      }
      db_idle = IDLE_FETCH;
      db_walk_to = dbClampX(db_ball_x - DB_SIDE_W / 2);
      db_dir = db_walk_to >= db_x ? 1 : -1;
      db_phase_t = 0;
      break;
    case ACT_SHENRON:
      db_balls = 7;
      dbStartShenron();
      break;
    case ACT_SHOT:
      dbStartDemoChange(1);
      break;
    case ACT_SSJ:
      dbStartDemoChange(4);
      break;
  }
}

// Picks what Goku does next. Without tricks he only walks and stands.
static void dbEndDemo(bool restoreStyle);

static void dbNextIdle() {
  if (db_demo_step >= 0) {
    int act = db_demo_step++;
    if (db_demo_step > db_demo_last) db_demo_step = -1;
    dbStartAct(act);
    return;
  }
  if (db_demo_on) {
    dbEndDemo(true);
    return;
  }
  if (!settings.dragonIdleTricks) {
    if (random(0, 3)) dbStartWalk();
    else dbIdlePause();
    return;
  }
  if (db_balls >= 7 && db_now_sec < 38) {
    dbStartAct(ACT_SHENRON);
    return;
  }
  if (db_ball_x >= 0 && random(0, 100) < 70) {
    dbStartAct(ACT_FETCH);
    return;
  }
  int roll = random(0, 100);
  int act;
  if (roll < 5 && db_now_sec < 40) act = ACT_GENKI;
  else if (roll < 28) act = ACT_WALK;
  else if (roll < 42) act = ACT_KATA;
  else if (roll < 51) act = ACT_SPAR;
  else if (roll < 63) act = ACT_VOLLEY;
  else if (roll < 69) act = ACT_BLASTS;
  else if (roll < 74) act = ACT_BEAM;
  else if (roll < 80 && db_now_sec < 40) act = random(0, 2) ? ACT_FIGHT_VEGETA : ACT_FIGHT_PICCOLO;
  else if (roll < 87 && db_now_sec < 48) act = ACT_NIMBUS;
  else if (roll < 94) act = ACT_BLINK;
  else act = ACT_POWER;
  dbStartAct(act);
}

// Where Goku stands to shoot digit `idx`: left of it facing right unless
// that runs off the screen.
static void dbShotSpot(int idx, float &x, int &dir) {
  int c = DIGIT_X[idx] + 7;
  if (c - DB_AIM_DX - DB_HAND_X >= 0) {
    dir = 1;
    x = c - DB_AIM_DX - DB_HAND_X;
  } else {
    dir = -1;
    x = c + DB_AIM_DX - (DB_SIDE_W - 1 - DB_HAND_X);
  }
}

static void dbStartChange() {
  float x;
  int dir;
  dbShotSpot(db_change_idx[db_cur], x, dir);
  db_lift = 0;
  db_phase_t = 0;
  if (settings.dragonIdleTricks) {
    dbLeaveGhost(SPR_STAND);
    db_x = x;
    db_dir = dir;
    db_phase = DB_BLINK;
  } else {
    db_walk_to = x;
    db_dir = x >= db_x ? 1 : -1;
    db_phase = DB_RUN;
  }
}

// Starts the minute-change show for db_change_idx/val. False if nothing changes.
static bool dbBeginChanges() {
  db_cur = 0;
  db_beam = db_tail = 0;
  db_lift = 0;
  memset(db_ki, 0, sizeof(db_ki));
  memset(db_motes, 0, sizeof(db_motes));
  db_genki_r = 0;
  if (db_num_changes >= DB_SSJ_MIN_CHANGES) {
    db_phase = DB_SSJ_UP;
    db_phase_t = 0;
    db_x = fminf(fmaxf(db_x, 10), SCREEN_WIDTH - DB_FRONT_W - 10);
    return true;
  }
  if (db_num_changes > 0) {
    dbStartChange();
    return true;
  }
  return false;
}

// Demo: replay the show on the digits already showing, so the time stays right.
static void dbStartDemoChange(int count) {
  static const int slots[4] = {0, 1, 3, 4};
  db_num_changes = 0;
  for (int k = count == 1 ? 3 : 0; k < 4; k++) {
    db_change_idx[db_num_changes] = slots[k];
    db_change_val[db_num_changes] = getDisplayedDigitValue(slots[k]);
    db_num_changes++;
  }
  dbBeginChanges();
}

// Hands the screen back to whatever was showing before the demo started.
// restoreStyle is false when the user already picked another style meanwhile.
static void dbEndDemo(bool restoreStyle) {
  db_demo_step = -1;
  db_demo_on = false;
  int prev = db_demo_prev_style;
  db_demo_prev_style = -1;
  if (prev < 0) return;
  httpForceClock = db_demo_prev_force;
  if (restoreStyle && settings.clockStyle != prev) {
    settings.clockStyle = prev;
    resetClockAnimationState();
  }
}

void dragonBallStartDemo(int only, int prevStyle, bool prevForce) {
  if (only == -2) {
    if (db_demo_on) dbEndDemo(true);
    return;
  }
  if (!db_demo_on) {
    db_demo_prev_style = prevStyle;
    db_demo_prev_force = prevForce;
  }
  db_demo_on = true;
  db_last_draw = millis();
  if (only >= 0 && only < ACT_COUNT) {
    db_demo_step = db_demo_last = only;
  } else {
    db_demo_step = 0;
    db_demo_last = ACT_COUNT - 1;
  }
  if (db_phase == DB_IDLE) {
    db_idle = IDLE_PAUSE;
    db_phase_t = 1.0f;  // pick the first act on the next frame
  }
}

int dragonBallDemoActs() { return ACT_COUNT; }

static void dbStartForm(int idx, uint8_t value, float spread) {
  slot_mode[idx] = SLOT_FORMING;
  slot_new[idx] = value;
  slot_form_t0[idx] = db_clock;
  for (int i = 0; i < 35; i++) slot_reveal[idx][i] = dbRandf(0.0f, spread);
}

static void dbFinishSlot(int idx) {
  updateDisplayedTimeDigit(idx, slot_new[idx]);
  slot_value[idx] = slot_new[idx];
  slot_mode[idx] = SLOT_SOLID;
}

// Aims the beam from the fire-pose hands at the centre of digit `idx`.
static void dbAimAt(int idx) {
  db_hx = dbCol(DB_HAND_X);
  db_hy = dbSideTop() + DB_HAND_Y;
  float dx = DIGIT_X[idx] + 7 - db_hx, dy = DB_TARGET_Y - db_hy;
  db_beam_len = sqrtf(dx * dx + dy * dy);
  db_ux = dx / db_beam_len;
  db_uy = dy / db_beam_len;
  db_beam_clip = idx;
}

static void dbEraseRow(int idx, int r) {
  for (int c = 0; c < 5; c++) {
    if (!dbGlyph(slot_value[idx], c, r)) continue;
    float x = DIGIT_X[idx] + c * 3 + 1, y = DB_DIGIT_Y + r * 3 + 1;
    float side = c < 2 ? -1.0f : (c > 2 ? 1.0f : (random(0, 2) ? 1.0f : -1.0f));
    dbSpark(x, y, side * dbRandf(30, 70), dbRandf(-40, 10), dbRandf(0.5f, 0.9f));
  }
}

// Every font pixel of the old digit becomes a chunk of rubble.
static void dbCrumble(int idx) {
  for (int c = 0; c < 5; c++)
    for (int r = 0; r < 7; r++)
      if (dbGlyph(slot_value[idx], c, r))
        dbPush(db_rubble, DB_MAX_RUBBLE, DIGIT_X[idx] + c * 3, DB_DIGIT_Y + r * 3,
               dbRandf(-25, 25), dbRandf(-30, 5), dbRandf(1.6f, 2.4f));
  slot_mode[idx] = SLOT_GONE;
}

// ========== Reset ==========
void resetDragonBallAnimation() {
  db_phase = DB_IDLE;
  dbIdlePause();
  db_x = 40;
  db_dir = 1;
  db_lift = 0;
  db_ghost_t = 0;
  db_flash_t = 0;
  db_burst_t = 0;
  db_shock_t = 0;
  db_boom_t = 0;
  db_flare_t = 0;
  db_pips_t = 0;
  db_goku_act = db_foe_act = 0;
  db_genki_r = 0;
  db_genki_thrown = false;
  for (int i = 0; i < 5; i++) {
    slot_mode[i] = SLOT_SOLID;
    slot_rows_gone[i] = 0;
    slot_value[i] = getDisplayedDigitValue(i);
    slot_shake[i] = 0;
  }
  memset(db_sparks, 0, sizeof(db_sparks));
  memset(db_rubble, 0, sizeof(db_rubble));
  memset(db_ki, 0, sizeof(db_ki));
  memset(db_motes, 0, sizeof(db_motes));
  db_num_changes = 0;
  last_db_update = 0;
  last_minute_db = -1;
  db_triggered = false;
  db_init_done = true;
}

bool dragonBallIsAnimating() {
  return db_phase != DB_IDLE;
}

// ========== Idle ==========
static void dbUpdateRoutine(const DbMove *moves, int n, float dt) {
  const DbMove &m = moves[db_move];
  db_move_t += dt;
  float k = fminf(1.0f, db_move_t / m.secs);
  db_x = dbClampX(db_move_x0 + m.step * k * db_dir);
  if (db_move_t < m.secs) return;

  if (m.hit) {
    db_flash_t = 0.12f;
    db_flash_x = (int)dbCol(DB_FIST_X + 2);
    db_flash_y = dbSideTop() + (m.pose == SPR_KICK ? 17 : 14) - (m.jump ? 6 : 0);
  }
  if (m.turn) db_dir = -db_dir;
  db_move++;
  db_move_t = 0;
  db_move_x0 = db_x;
  if (db_move >= n) dbIdlePause();
}

static void dbUpdateNimbus(float dt) {
  float feetX = db_x + DB_SIDE_W / 2 - 12;       // cloud left edge under him
  switch (db_nimbus_stage) {
    case 0:  // cloud arrives beside him at ankle height
      db_cloud_x += (feetX > db_cloud_x ? 1 : -1) * 70.0f * dt;
      if (fabsf(db_cloud_x - feetX) < 2) {
        db_cloud_x = feetX;
        db_nimbus_stage = 1;
        db_phase_t = 0;
      }
      break;
    case 1:  // hop on
      db_lift = sinf(fminf(1.0f, db_phase_t / 0.4f) * 1.5708f) * 8;
      if (db_phase_t > 0.4f) {
        db_nimbus_stage = 2;
        db_nimbus_legs = random(3, 5);
        db_nimbus_to = dbRandf(0, SCREEN_WIDTH - DB_SIDE_W);
        db_dir = db_nimbus_to >= db_x ? 1 : -1;
        db_phase_t = 0;
      }
      break;
    case 2: {  // ride a few legs back and forth, climbing a little, bobbing
      float d = db_nimbus_to - db_x, step = 45.0f * dt;
      db_lift = 8 + fminf(6.0f, db_phase_t * 8) + sinf(db_clock * 5) + 2 * sinf(db_phase_t * 1.3f);
      if (fabsf(d) <= step) {
        db_x = db_nimbus_to;
        if (--db_nimbus_legs > 0) {
          // Next leg heads well away from here so the turns read clearly.
          float to = dbRandf(0, SCREEN_WIDTH - DB_SIDE_W);
          if (fabsf(to - db_x) < 35) to = db_x < SCREEN_WIDTH / 2 ? db_x + 45 : db_x - 45;
          db_nimbus_to = dbClampX(to);
          db_dir = db_nimbus_to >= db_x ? 1 : -1;
        } else {
          db_nimbus_stage = 3;
          db_phase_t = 0;
        }
      } else {
        db_x += step * db_dir;
      }
      db_cloud_x = db_x + DB_SIDE_W / 2 - 12;
      break;
    }
    case 3:  // hop off; the cloud flies away
      db_lift = fmaxf(0.0f, 14 * (1 - db_phase_t / 0.35f));
      if (db_phase_t > 0.35f) {
        db_lift = 0;
        db_nimbus_stage = 4;
      }
      break;
    case 4:
      db_cloud_x += db_dir * 80.0f * dt;
      if (db_cloud_x < -30 || db_cloud_x > SCREEN_WIDTH + 4) dbIdlePause();
      break;
  }
}

static void dbBeatFx(uint8_t fx, DbPose g, DbPose f) {
  if (!fx) return;
  int x = (int)((dbGokuFront() + dbFoeFront()) / 2);
  int y = dbFistY() + ((g == POSE_KICK || f == POSE_KICK) ? 3 : 0);
  db_flash_t = fx == 2 ? 0.25f : 0.12f;
  db_flash_x = x;
  db_flash_y = y;
  if (fx == 2) {
    db_shock_t = 0.3f;
    for (int i = 0; i < 8; i++)
      dbSpark(x, y, dbRandf(-80, 80), dbRandf(-70, 10), dbRandf(0.3f, 0.6f));
  }
}

static void dbUpdateFight(float dt) {
  float fdir = dbFoeDir();
  switch (db_fight) {
    case FIGHT_ARRIVE:
      if (db_foe == FOE_VEGETA) {
        // Swoops in from the edge and lands in front of Goku.
        float d = db_foe_vx - db_foe_x, step = 90.0f * dt;
        if (fabsf(d) <= step) {
          db_foe_x = db_foe_vx;
          db_foe_lift = 0;
          for (int i = 0; i < 5; i++)
            dbSpark(db_foe_x + 10, DB_GROUND_Y - 1, dbRandf(-40, 40), dbRandf(-25, -5), 0.4f);
        } else {
          db_foe_x += d > 0 ? step : -step;
          db_foe_lift = fmaxf(0.0f, 22 * fabsf(d) / 60.0f);
        }
      }
      if ((db_foe == FOE_VEGETA && db_foe_lift <= 0 && db_phase_t > 0.3f) ||
          (db_foe == FOE_PICCOLO && db_phase_t > 0.5f)) {
        db_fight = FIGHT_BEATS;
        db_beat = 0;
        db_beat_t = 0;
        db_beat_g0 = db_x;
        db_beat_f0 = db_foe_x;
      }
      break;

    case FIGHT_BEATS: {
      const DbBeat &b = FIGHT_SCRIPT[db_beat];
      db_goku_pose = (DbPose)b.goku;
      db_foe_pose = (DbPose)b.foe;
      db_beat_t += dt;
      float k = fminf(1.0f, db_beat_t / b.secs);
      db_x = dbClampX(db_beat_g0 + b.gokuStep * k * db_dir);
      db_foe_x = db_beat_f0 + b.foeStep * k * fdir;
      // Never let them overlap or drift apart.
      float gap = (db_foe_x - db_x) * db_dir;
      if (gap < 14) db_foe_x = db_x + 14 * db_dir;
      if (gap > 26) db_foe_x = db_x + 26 * db_dir;
      if (db_beat_t < b.secs) break;
      dbBeatFx(b.fx, db_goku_pose, db_foe_pose);
      db_beat++;
      db_beat_t = 0;
      db_beat_g0 = db_x;
      db_beat_f0 = db_foe_x;
      if (db_beat >= DB_LEN(FIGHT_SCRIPT)) {
        db_phase_t = 0;
        db_fight = FIGHT_VOLLEY;
        db_shot = 0;
      }
      break;
    }

    case FIGHT_VOLLEY: {
      while (db_shot < DB_LEN(FIGHT_VOLLEY_SHOTS) && db_phase_t >= FIGHT_VOLLEY_SHOTS[db_shot].t) {
        bool goku = FIGHT_VOLLEY_SHOTS[db_shot].who == 1;
        float x = goku ? dbGokuFront() : dbFoeFront();
        int dir = goku ? db_dir : -db_dir;
        for (int i = 0; i < DB_MAX_KI; i++) {
          if (db_ki[i].active) continue;
          db_ki[i] = {true, x, (float)dbFistY(), dir * 130.0f, 0, -1, (uint8_t)(goku ? 1 : 2)};
          break;
        }
        if (goku) db_goku_act = 0.15f;
        else db_foe_act = 0.15f;
        db_shot++;
      }
      db_goku_pose = db_goku_act > 0 ? POSE_PUNCH : POSE_STAND;
      db_foe_pose = db_foe_act > 0 ? POSE_PUNCH : POSE_STAND;
      bool flying = false;
      for (int i = 0; i < DB_MAX_KI; i++)
        if (db_ki[i].active && (db_ki[i].owner == 1 || db_ki[i].owner == 2)) flying = true;
      if ((db_shot >= DB_LEN(FIGHT_VOLLEY_SHOTS) && !flying && db_phase_t > 2.0f) || db_phase_t > 3.5f) {
        db_fight = FIGHT_CHARGE;
        db_phase_t = 0;
      }
      break;
    }

    case FIGHT_CHARGE:
      db_goku_pose = POSE_STAND;
      db_foe_pose = POSE_STAND;
      if (db_phase_t > 0.7f) {
        db_fight = FIGHT_STRUGGLE;
        db_phase_t = 0;
        db_clash_x = (dbGokuFront() + dbFoeFront()) / 2;
      }
      break;

    case FIGHT_STRUGGLE: {
      // Even at first; then the visitor's beam drives Goku back, crouched and
      // sliding, until he flares up and drives the clash home.
      db_foe_pose = POSE_PUNCH;
      float mid = (dbGokuFront() + dbFoeFront()) / 2;
      if (db_phase_t < 1.2f) {
        db_goku_pose = POSE_PUNCH;
        db_clash_x = mid + sinf(db_phase_t * 5.0f) * 4 * db_dir;
      } else if (db_phase_t < 2.7f) {
        db_goku_pose = POSE_STRAIN;
        db_x = dbClampX(db_x - db_dir * 7.0f * dt);
        db_clash_x -= db_dir * 9.0f * dt;
        if ((db_clash_x - dbGokuFront()) * db_dir < 5) db_clash_x = dbGokuFront() + 5 * db_dir;
        if (random(0, 2) == 0)
          dbSpark(dbCol(3), DB_GROUND_Y - 1, -db_dir * dbRandf(10, 40), dbRandf(-30, -10), 0.3f);
      } else {
        if (db_flare_t <= 0 && db_goku_pose == POSE_STRAIN) {
          db_flare_t = 0.45f;
          db_shock_t = 0.4f;
        }
        db_goku_pose = POSE_PUNCH;
        db_clash_x += db_dir * 75.0f * dt;
      }
      if (random(0, 2) == 0)
        dbSpark(db_clash_x, dbFistY(), dbRandf(-50, 50), dbRandf(-60, 20), 0.3f);
      if ((db_clash_x - dbFoeFront()) * db_dir >= 0 || db_phase_t > 5.0f) {
        db_flash_t = 0.25f;
        db_flash_x = (int)dbFoeFront();
        db_flash_y = dbFistY();
        db_shock_t = 0.3f;
        db_fight = FIGHT_KO;
        db_phase_t = 0;
        db_foe_pose = POSE_HURT;
        db_foe_vx = fdir * -120.0f;
        db_foe_vy = -70.0f;
      }
      break;
    }

    case FIGHT_KO:
      // Knocked tumbling off the screen.
      if (db_phase_t > 0.2f) db_goku_pose = POSE_STAND;
      db_foe_x += db_foe_vx * dt;
      db_foe_lift -= db_foe_vy * dt;
      db_foe_vy += 150.0f * dt;
      if (db_foe_lift < 0) {
        db_foe_lift = 0;
        db_foe_vy = -fabsf(db_foe_vy) * 0.4f;
      }
      if (db_foe_x < -DB_SIDE_W - 2 || db_foe_x > SCREEN_WIDTH + 2) {
        db_fight = FIGHT_WIN;
        db_phase_t = 0;
      }
      break;

    case FIGHT_WIN:
      if (db_phase_t > 0.8f) dbIdlePause();
      break;
  }
}

static void dbUpdateGenki(float dt) {
  float t = db_phase_t;
  if (!db_genki_thrown) {
    if (t > 0.4f && t < 4.2f) {
      db_genki_r = fminf(8.0f, 1 + 7 * (t - 0.4f) / 3.8f);
      // Motes of energy drift in from every edge of the sky.
      for (int n = 0; n < 2; n++) {
        float x, y;
        switch (random(0, 3)) {
          case 0: x = dbRandf(0, SCREEN_WIDTH); y = 0; break;
          case 1: x = 0; y = dbRandf(0, 50); break;
          default: x = SCREEN_WIDTH - 1; y = dbRandf(0, 50); break;
        }
        dbPush(db_motes, DB_MAX_MOTES, x, y, 0, 0, 3.0f);
      }
    }
    for (int i = 0; i < DB_MAX_MOTES; i++) {
      DbSpark &m = db_motes[i];
      if (!m.active) continue;
      float dx = db_genki_x - m.x, dy = db_genki_y - m.y, d = sqrtf(dx * dx + dy * dy);
      if (d < db_genki_r + 1 || (m.life -= dt) <= 0) { m.active = false; continue; }
      m.x += dx / d * 75.0f * dt;
      m.y += dy / d * 75.0f * dt;
    }
    if (t > 4.6f) {
      db_genki_thrown = true;
      memset(db_motes, 0, sizeof(db_motes));
    }
    return;
  }
  // Hurled toward the far edge, dropping a little, then a blast beyond it.
  if (db_boom_t <= 0 && db_genki_r > 0) {
    db_genki_x += db_dir * 85.0f * dt;
    db_genki_y += 10.0f * dt;
    if (db_genki_x < -db_genki_r || db_genki_x > SCREEN_WIDTH + db_genki_r) {
      db_boom_t = 0.8f;
      db_boom_x = db_dir > 0 ? SCREEN_WIDTH - 1 : 0;
      db_boom_y = (int)db_genki_y;
      db_shock_t = 0.8f;
      db_genki_r = 0;
    }
  } else if (db_boom_t <= 0) {
    dbIdlePause();
  }
}

// Shenron's body: a curve out of the ball row, sweeping up to the left and
// then winding right across the screen. u runs tail (0) to head (1).
static void dbShenronPoint(float u, float phase, float &x, float &y) {
  if (u < 0.22f) {
    float s = u / 0.22f, a = 1 - s;
    x = a * a * 64 + 2 * a * s * 24 + s * s * 14;
    y = a * a * 57 + 2 * a * s * 64 + s * s * 36;
  } else {
    float s = (u - 0.22f) / 0.78f;
    x = 14 + s * 92;
    y = 36 + 12 * sinf(s * 6.283f + phase) * fminf(1.0f, s * 4);
  }
}

static void dbUpdateShenron(float dt) {
  float t = db_phase_t;
  if (t < 1.0f && random(0, 3) == 0) {
    int i = random(0, 7);
    dbSpark(dbRowBallX(i), DB_GROUND_Y - 4, dbRandf(-10, 10), dbRandf(-70, -40), 0.4f);
  }
  if (t > 6.0f && t < 7.0f) {
    float x, y;
    dbShenronPoint((t - 6.0f), (t - 3.0f) * 2.0f, x, y);
    for (int s = 0; s < 2; s++) dbSpark(x, y, dbRandf(-40, 40), dbRandf(-40, 10), 0.5f);
  }
  if (t > 7.0f) {
    for (int i = 0; i < 7; i++) {
      db_scatter_x[i] += db_scatter_vx[i] * dt;
      db_scatter_y[i] += db_scatter_vy[i] * dt;
    }
  }
  if (t > 7.9f) {
    db_balls = 0;
    db_ball_timer = dbRandf(60, 150);
    dbIdlePause();
  }
}

static void dbUpdateIdle(float dt) {
  switch (db_idle) {
    case IDLE_WALK: {
      float d = db_walk_to - db_x, step = DB_WALK_SPEED * dt;
      if (fabsf(d) <= step) {
        db_x = db_walk_to;
        dbIdlePause();
      } else {
        db_x += step * db_dir;
        db_walk_dist += step;
      }
      break;
    }
    case IDLE_PAUSE:
      if (db_phase_t > 0.9f) dbNextIdle();
      break;
    case IDLE_KATA:
      dbUpdateRoutine(KATA, DB_LEN(KATA), dt);
      break;
    case IDLE_SPAR:
      dbUpdateRoutine(SPAR, DB_LEN(SPAR), dt);
      break;
    case IDLE_POWER:
      if (random(0, 3) == 0)
        dbSpark(db_x + dbRandf(-4, 22), DB_GROUND_Y - 1, dbRandf(-6, 6), dbRandf(-45, -20), 0.6f);
      if (db_phase_t > 1.6f) dbIdlePause();
      break;
    case IDLE_VOLLEY:
    case IDLE_BLASTS:
      db_shot_timer -= dt;
      if (db_shot_timer <= 0 && db_shots_left > 0) {
        db_shots_left--;
        db_shot_timer = dbRandf(0.07f, 0.32f);  // ragged rhythm
        if (db_idle == IDLE_VOLLEY) {
          float hx = dbCol(DB_HAND_X), hy = dbSideTop() + DB_HAND_Y;
          dbFireKi(hx, hy, DIGIT_X[db_volley_target] + 7 + dbRandf(-5, 5), DB_DIGIT_BOTTOM - 2,
                   db_volley_target);
        } else {
          float fx = dbCol(DB_FIST_X), fy = dbSideTop() + DB_FIST_Y;
          dbFireKi(fx, fy, fx + db_dir * 100, fy + dbRandf(-3, 3), -1);
        }
      }
      if (db_shots_left == 0 && db_shot_timer < -0.5f) dbIdlePause();
      break;
    case IDLE_BEAM:
      // Short charge, then a level Kamehameha off the edge of the screen.
      if (db_phase_t < 0.7f) {
        if (random(0, 2) == 0) {
          float cx = dbCol(DB_KAME_X), cy = dbSideTop() + DB_KAME_Y;
          float a = dbRandf(0, 6.283f), r = dbRandf(8, 11);
          dbSpark(cx + cosf(a) * r, cy + sinf(a) * r, -cosf(a) * r * 3.5f, -sinf(a) * r * 3.5f, 0.25f);
        }
        db_hx = dbCol(DB_FIST_X);
        db_hy = dbSideTop() + DB_FIST_Y;
        db_ux = db_dir;
        db_uy = 0;
        db_beam_len = db_dir > 0 ? SCREEN_WIDTH - db_hx : db_hx;
        db_beam_clip = -1;
      } else if (db_phase_t < 1.6f) {
        db_beam = fminf(db_beam_len, db_beam + DB_BEAM_SPEED * dt);
      } else {
        db_tail += DB_BEAM_SPEED * dt;
        if (db_tail >= db_beam_len) dbIdlePause();
      }
      break;
    case IDLE_NIMBUS:
      dbUpdateNimbus(dt);
      break;
    case IDLE_FIGHT:
      dbUpdateFight(dt);
      break;
    case IDLE_GENKI:
      dbUpdateGenki(dt);
      break;
    case IDLE_FETCH: {
      float d = db_walk_to - db_x, step = DB_WALK_SPEED * 1.6f * dt;
      if (db_ball_x < 0) { dbIdlePause(); break; }
      if (fabsf(d) > step && db_phase_t < 8) {
        db_x += step * (d > 0 ? 1 : -1);
        db_walk_dist += step;
        db_phase_t = 0;  // the crouch timer starts on arrival
        break;
      }
      db_x = db_walk_to;
      if (db_phase_t > 0.45f) {
        for (int s = 0; s < 6; s++)
          dbSpark(db_ball_x, DB_GROUND_Y - 3, dbRandf(-30, 30), dbRandf(-50, -15), 0.4f);
        db_ball_x = -1;
        db_balls++;
        db_pips_t = 2.2f;
        db_ball_timer = dbRandf(60, 150);
        dbIdlePause();
      }
      break;
    }
    case IDLE_SHENRON:
      dbUpdateShenron(dt);
      break;
  }
}

// ========== Update ==========
static void dbUpdateKi(float dt) {
  for (int i = 0; i < DB_MAX_KI; i++) {
    DbKi &k = db_ki[i];
    if (!k.active) continue;
    k.x += k.vx * dt;
    k.y += k.vy * dt;
    if (k.owner == 1 || k.owner == 2) {
      // Head-on meetings pop; a blast that gets through is swatted skyward.
      for (int j = 0; j < DB_MAX_KI; j++) {
        DbKi &o = db_ki[j];
        if (j == i || !o.active || o.owner == k.owner || (o.owner != 1 && o.owner != 2)) continue;
        if (fabsf(o.x - k.x) < 4 && fabsf(o.y - k.y) < 3) {
          for (int s = 0; s < 6; s++)
            dbSpark(k.x, k.y, dbRandf(-60, 60), dbRandf(-60, 20), 0.35f);
          db_flash_t = 0.15f;
          db_flash_x = (int)k.x;
          db_flash_y = (int)k.y;
          k.active = o.active = false;
        }
      }
      if (!k.active) continue;
      bool gokuShot = k.owner == 1;
      float front = gokuShot ? dbFoeFront() : dbGokuFront();
      float dirTo = gokuShot ? db_dir : -db_dir;
      if ((k.x - front) * dirTo >= 0) {
        k.owner = 3;
        k.vx = -k.vx * 0.25f;
        k.vy = -120.0f;
        if (gokuShot) db_foe_act = 0.15f;
        else db_goku_act = 0.15f;
      }
      continue;
    }
    if (k.owner == 3) {
      if (k.y < 0) k.active = false;
      continue;
    }
    if (k.target >= 0 && k.y <= DB_DIGIT_BOTTOM) {
      // Pop against the underside of the digit.
      for (int s = 0; s < 3; s++)
        dbSpark(k.x, DB_DIGIT_BOTTOM + 1, dbRandf(-40, 40), dbRandf(0, 30), 0.3f);
      db_flash_t = 0.08f;
      db_flash_x = (int)k.x;
      db_flash_y = DB_DIGIT_BOTTOM + 2;
      slot_shake[k.target] = 0.12f;
      k.active = false;
    } else if (k.x < -3 || k.x > SCREEN_WIDTH + 3) {
      k.active = false;
    }
  }
}

static void updateDragonBallAnimation(struct tm *timeinfo) {
  unsigned long now = millis();
  float dt = (now - last_db_update) / 1000.0f;
  if (dt > 0.1f || last_db_update == 0) dt = 0.025f;
  last_db_update = now;
  db_clock += dt;
  db_phase_t += dt;
  if (db_ghost_t > 0) db_ghost_t -= dt;
  if (db_flash_t > 0) db_flash_t -= dt;
  if (db_burst_t > 0) db_burst_t -= dt;
  for (int i = 0; i < 5; i++)
    if (slot_shake[i] > 0) slot_shake[i] -= dt;
  if (db_shock_t > 0) db_shock_t -= dt;
  if (db_goku_act > 0) db_goku_act -= dt;
  if (db_foe_act > 0) db_foe_act -= dt;
  if (db_flare_t > 0) db_flare_t -= dt;
  if (db_boom_t > 0) db_boom_t -= dt;
  if (db_pips_t > 0) db_pips_t -= dt;
  db_now_sec = timeinfo->tm_sec;

  // A Dragon Ball turns up on the ground now and then.
  if (settings.dragonIdleTricks && db_phase == DB_IDLE && db_ball_x < 0 && db_balls < 7 &&
      db_idle != IDLE_SHENRON) {
    db_ball_timer -= dt;
    if (db_ball_timer <= 0) {
      for (int k = 0; k < 6; k++) {
        int x = random(6, SCREEN_WIDTH - 6);
        if (fabsf(x - (db_x + DB_SIDE_W / 2)) > 16) { db_ball_x = x; break; }
      }
      db_ball_timer = 10;  // retry soon if he was in the way every time
    }
  }

  for (int i = 0; i < 5; i++)
    if (slot_mode[i] == SLOT_SOLID) slot_value[i] = getDisplayedDigitValue(i);

  if (timeinfo->tm_min != last_minute_db) {
    last_minute_db = timeinfo->tm_min;
    db_triggered = false;
  }
  if (timeinfo->tm_sec >= DB_TRIGGER_SECOND && !db_triggered && db_phase == DB_IDLE) {
    db_triggered = true;
    time_overridden = true;
    time_override_start = millis();
    calculateTargetDigits(displayed_hour, displayed_min, displayed_is_pm);
    db_num_changes = 0;
    for (int i = 0; i < num_targets; i++) {
      if (target_digit_index[i] == 2) continue;
      db_change_idx[db_num_changes] = target_digit_index[i];
      db_change_val[db_num_changes] = target_digit_values[i];
      db_num_changes++;
    }
    if (!dbBeginChanges()) time_overridden = false;
  }

  int idx = db_num_changes ? db_change_idx[db_cur] : 0;
  bool first = db_cur == 0;

  switch (db_phase) {
    case DB_IDLE:
      dbUpdateIdle(dt);
      break;

    case DB_BLINK:
      if (db_phase_t >= (first ? 0.35f : 0.2f)) {
        db_phase = DB_CHARGE;
        db_phase_t = 0;
      }
      break;

    case DB_RUN: {
      float d = db_walk_to - db_x, step = DB_RUN_SPEED * dt;
      if (fabsf(d) <= step || db_phase_t > DB_PHASE_TIMEOUT) {
        float x;
        int dir;
        dbShotSpot(idx, x, dir);
        db_x = x;
        db_dir = dir;
        db_phase = DB_CHARGE;
        db_phase_t = 0;
      } else {
        db_x += step * db_dir;
        db_walk_dist += step;
      }
      break;
    }

    case DB_CHARGE: {
      float cx = dbCol(DB_KAME_X), cy = dbSideTop() + DB_KAME_Y;
      if (random(0, 2) == 0) {  // sparks spiral into the ki ball
        float a = dbRandf(0, 6.283f), r = dbRandf(9, 13);
        dbSpark(cx + cosf(a) * r, cy + sinf(a) * r, -cosf(a) * r * 3.5f, -sinf(a) * r * 3.5f, 0.25f);
      }
      if (db_phase_t >= (first ? 1.0f : 0.55f)) {
        dbAimAt(idx);
        db_phase = DB_FIRE;
        db_phase_t = 0;
        db_beam = db_tail = 0;
        slot_mode[idx] = SLOT_ERASING;
        slot_rows_gone[idx] = 0;
      }
      break;
    }

    case DB_FIRE: {
      bool arrived = db_beam >= db_beam_len;
      db_beam = fminf(db_beam_len, db_beam + DB_BEAM_SPEED * dt);
      float fy = db_hy + db_uy * db_beam;
      // The front eats the digit from its bottom row up...
      while (slot_rows_gone[idx] < 7 &&
             fy <= DB_DIGIT_Y + (6 - slot_rows_gone[idx]) * 3 + 1) {
        dbEraseRow(idx, 6 - slot_rows_gone[idx]);
        slot_rows_gone[idx]++;
      }
      // ...and bursts at its centre, taking the rows above with it.
      if (db_beam >= db_beam_len && !arrived) {
        while (slot_rows_gone[idx] < 7) {
          dbEraseRow(idx, 6 - slot_rows_gone[idx]);
          slot_rows_gone[idx]++;
        }
        db_burst_t = 0.35f;
      }
      if ((arrived && db_phase_t > (first ? 0.8f : 0.5f)) || db_phase_t > DB_PHASE_TIMEOUT) {
        slot_mode[idx] = SLOT_GONE;
        db_phase = DB_RETRACT;
        db_phase_t = 0;
      }
      break;
    }

    case DB_RETRACT:
      db_tail += DB_BEAM_SPEED * dt;
      if (db_tail >= db_beam_len || db_phase_t > DB_PHASE_TIMEOUT) {
        dbStartForm(idx, db_change_val[db_cur], 0.4f);
        db_phase = DB_FORM;
        db_phase_t = 0;
      }
      break;

    case DB_FORM:
      if (db_phase_t >= 0.5f) {
        dbFinishSlot(idx);
        db_cur++;
        if (db_cur < db_num_changes) {
          dbStartChange();
        } else {
          db_phase = DB_IDLE;
          dbIdlePause();
        }
      }
      break;

    case DB_SSJ_UP:
      if (random(0, 2) == 0)
        dbSpark(db_x + dbRandf(-6, 20), DB_GROUND_Y - 1, dbRandf(-8, 8), dbRandf(-60, -25), 0.7f);
      if (db_phase_t > 2.2f) {
        for (int k = 0; k < db_num_changes; k++) dbCrumble(db_change_idx[k]);
        db_phase = DB_SSJ_CRUMBLE;
        db_phase_t = 0;
      }
      break;

    case DB_SSJ_CRUMBLE:
      if (db_phase_t > 1.1f) {
        db_phase = DB_SSJ_STRIKE;
        db_phase_t = 0;
        dbStartForm(db_change_idx[0], db_change_val[0], 0.2f);
      }
      break;

    case DB_SSJ_STRIKE:
      // One bolt per digit, a quarter second apart.
      if (db_phase_t > 0.3f) {
        dbFinishSlot(db_change_idx[db_cur]);
        db_cur++;
        db_phase_t = 0;
        if (db_cur < db_num_changes) {
          dbStartForm(db_change_idx[db_cur], db_change_val[db_cur], 0.2f);
        } else {
          db_phase = DB_SSJ_DOWN;
        }
      }
      break;

    case DB_SSJ_DOWN:
      if (db_phase_t > 0.9f) {
        db_phase = DB_IDLE;
        dbIdlePause();
      }
      break;
  }

  dbUpdateKi(dt);

  for (int i = 0; i < DB_MAX_SPARKS; i++) {
    DbSpark &s = db_sparks[i];
    if (!s.active) continue;
    s.x += s.vx * dt;
    s.y += s.vy * dt;
    s.vy += 90.0f * dt;
    s.life -= dt;
    if (s.life <= 0 || s.y >= DB_GROUND_Y || s.y < 0 || s.x < 0 || s.x >= SCREEN_WIDTH)
      s.active = false;
  }
  for (int i = 0; i < DB_MAX_RUBBLE; i++) {
    DbSpark &s = db_rubble[i];
    if (!s.active) continue;
    s.x += s.vx * dt;
    s.y += s.vy * dt;
    s.vy += 160.0f * dt;
    if (s.y >= DB_GROUND_Y - 2) {  // land and skid
      s.y = DB_GROUND_Y - 2;
      s.vy *= -0.3f;
      s.vx *= 0.6f;
    }
    s.life -= dt;
    if (s.life <= 0) s.active = false;
  }
}

// ========== Drawing ==========
static void dbPixel(int x, int y, uint16_t color = DISPLAY_WHITE) {
  if (x >= 0 && x < SCREEN_WIDTH && y >= 0 && y < SCREEN_HEIGHT) display.drawPixel(x, y, color);
}

static void dbDisc(int cx, int cy, int r) {
  for (int y = -r; y <= r; y++)
    for (int x = -r; x <= r; x++)
      if (x * x + y * y <= r * r + r) dbPixel(cx + x, cy + y);
}

static void dbRing(int cx, int cy, int r) {
  for (int a = 0; a < 48; a++)
    dbPixel(cx + (int)roundf(cosf(a * 0.1309f) * r), cy + (int)roundf(sinf(a * 0.1309f) * r));
}

static void dbDrawSprite(const char *const *rows, int w, int h, int x, int y, int dir,
                         bool dotted = false) {
  for (int r = 0; r < h; r++)
    for (int c = 0; c < w; c++)
      if (rows[r][c] == 'X' && (!dotted || ((c + r) & 1)))
        dbPixel(dir > 0 ? x + c : x + w - 1 - c, y + r);
}

static void dbDrawChar(const char *const *rows, int h, int x, int lift, int dir, bool flipY) {
  int top = DB_FEET_Y - h + 1 - lift;
  for (int r = 0; r < h; r++) {
    int rr = flipY ? h - 1 - r : r;
    for (int c = 0; c < DB_SIDE_W; c++)
      if (rows[rr][c] == 'X') dbPixel(dir > 0 ? x + c : x + DB_SIDE_W - 1 - c, top + r);
  }
}

// Level beam between two x positions. The spiral variant is Piccolo's
// Special Beam Cannon: a thin core with a ribbon winding round it.
static void dbDrawHBeam(int xa, int xb, int y, bool spiral, int thick = 0) {
  if (xa > xb) { int s = xa; xa = xb; xb = s; }
  for (int x = xa; x <= xb; x++) {
    if (spiral) {
      dbPixel(x, y);
      dbPixel(x, y + (int)roundf(sinf(x * 0.7f - db_clock * 25.0f) * 3));
      dbPixel(x, y - (int)roundf(sinf(x * 0.7f - db_clock * 25.0f + 1.6f) * 3));
    } else {
      int half = 4 + thick + (int)roundf(sinf(x * 0.6f - db_clock * 18.0f));
      int core = 2 + thick / 2;
      dbPixel(x, y - half);
      dbPixel(x, y + half);
      bool band = ((x + (int)(db_clock * 60.0f)) % 6 + 6) % 6 == 0;
      for (int w = -core; w <= core; w++)
        dbPixel(x, y + w, band && abs(w) <= core - 1 ? DISPLAY_BLACK : DISPLAY_WHITE);
    }
  }
}

static const char *const *dbGokuPoseRows(DbPose p) {
  switch (p) {
    case POSE_PUNCH: return SPR_PUNCH;
    case POSE_KICK: return SPR_KICK;
    case POSE_HURT: return SPR_HURT;
    case POSE_STRAIN: return SPR_STRAIN;
    default: return SPR_STAND;
  }
}

static void dbDrawSide(const char *const *pose, int x, int lift, int dir, bool dotted = false) {
  dbDrawSprite(pose, DB_SIDE_W, DB_SIDE_H, x, dbSideTop() - lift, dir, dotted);
}

static void dbDrawFront(bool ssj) {
  int x = (int)db_x + (DB_SIDE_W - DB_FRONT_W) / 2;
  if (ssj) dbDrawSprite(SPR_SSJ, DB_FRONT_W, DB_FRONT_H + 1, x, DB_FEET_Y - DB_FRONT_H, 1);
  else dbDrawSprite(SPR_POWER, DB_FRONT_W, DB_FRONT_H, x, DB_FEET_Y - DB_FRONT_H + 1, 1);
}

static void dbDrawAura(float scale) {
  int cx = (int)db_x + DB_SIDE_W / 2, cy = DB_FEET_Y - 12;
  int n = (int)(26 * scale);
  for (int i = 0; i < n; i++) {
    float a = i * 6.283f / n + db_clock * 3.0f;
    float rx = (11 + 2 * sinf(db_clock * 17 + i)) * scale;
    float ry = (15 + 2 * cosf(db_clock * 13 + i * 2)) * scale;
    int px = cx + (int)(cosf(a) * rx), py = cy + (int)(sinf(a) * ry);
    if (py < DB_GROUND_Y) dbPixel(px, py);
  }
}

static void dbDrawStar(int x, int y) {
  dbPixel(x, y);
  for (int k = 1; k <= 2; k++) {
    dbPixel(x - k, y); dbPixel(x + k, y);
    dbPixel(x, y - k); dbPixel(x, y + k);
  }
  dbPixel(x - 2, y - 2); dbPixel(x + 2, y - 2);
  dbPixel(x - 2, y + 2); dbPixel(x + 2, y + 2);
}

// Jagged lightning between two points, re-randomised every frame.
static void dbBolt(int x0, int y0, int x1, int y1) {
  int segs = 5 + abs(y1 - y0) / 8;
  int px = x0, py = y0;
  for (int s = 1; s <= segs; s++) {
    int nx = x0 + (x1 - x0) * s / segs + (s < segs ? (int)random(-4, 5) : 0);
    nx = constrain(nx, 0, SCREEN_WIDTH - 1);
    int ny = y0 + (y1 - y0) * s / segs;
    display.drawLine(px, py, nx, ny, DISPLAY_WHITE);
    px = nx;
    py = ny;
  }
}

static void dbDrawNimbus(int x, int y) {
  // Puffs with a dark belly line, about 24x7.
  dbDisc(x + 5, y + 4, 3);
  dbDisc(x + 11, y + 3, 4);
  dbDisc(x + 18, y + 4, 3);
  for (int i = 3; i < 21; i++) dbPixel(x + i, y + 5, DISPLAY_BLACK);
  int tail = db_dir > 0 ? x - 2 : x + 24;
  dbPixel(tail, y + 4);
  dbPixel(tail - db_dir * 2, y + 5);
}

// Wavy outline and a solid core with energy bands, drawn along the aim line.
// Inside the digit row only the target column is painted, so the beam never
// smudges the neighbours.
static void dbDrawBeam(float from, float to) {
  float nx = -db_uy, ny = db_ux;
  int clip = db_beam_clip;
  int x0 = clip >= 0 ? DIGIT_X[clip] - 1 : 0, x1 = clip >= 0 ? DIGIT_X[clip] + 15 : 0;
  for (float s = from; s <= to; s += 0.5f) {
    float cx = db_hx + db_ux * s, cy = db_hy + db_uy * s;
    int half = 5 + (int)roundf(sinf(s * 0.6f - db_clock * 18.0f));
    bool band = ((int)(s - db_clock * 60.0f) % 6 + 6) % 6 == 0;
    for (int w = -half; w <= half; w++) {
      int px = (int)roundf(cx + nx * w), py = (int)roundf(cy + ny * w);
      if (clip >= 0 && py <= DB_DIGIT_BOTTOM && (px < x0 || px > x1)) continue;
      if (abs(w) == half || (abs(w) <= 2 && !(band && abs(w) <= 1))) dbPixel(px, py);
      else if (abs(w) <= 2) dbPixel(px, py, DISPLAY_BLACK);
    }
  }
}

static void dbDrawDigit(int i, int ox) {
  if (slot_shake[i] > 0) ox += ((int)(db_clock * 60) & 1) ? 1 : -1;
  int x0 = DIGIT_X[i] + ox;
  switch (slot_mode[i]) {
    case SLOT_GONE:
      return;
    case SLOT_FORMING: {
      float age = db_clock - slot_form_t0[i];
      int k = 0;
      for (int c = 0; c < 5; c++)
        for (int r = 0; r < 7; r++, k++) {
          if (!dbGlyph(slot_new[i], c, r)) continue;
          float t = age - slot_reveal[i][k];
          if (t < 0) continue;
          int x = x0 + c * 3, y = DB_DIGIT_Y + r * 3;
          if (t < 0.12f) dbPixel(x + 1, y + 1);  // spark first
          else display.fillRect(x, y, 3, 3, DISPLAY_WHITE);
        }
      return;
    }
    default: {
      int last = slot_mode[i] == SLOT_ERASING ? 6 - slot_rows_gone[i] : 6;
      for (int r = 0; r <= last; r++)
        for (int c = 0; c < 5; c++)
          if (dbGlyph(slot_value[i], c, r))
            display.fillRect(x0 + c * 3, DB_DIGIT_Y + r * 3, 3, 3, DISPLAY_WHITE);
    }
  }
}

static const char *const *dbWalkFrame() {
  static const char *const *const cycle[4] = {SPR_WALK_A, SPR_WALK_B, SPR_WALK_C, SPR_WALK_B};
  return cycle[((int)(db_walk_dist / 4.0f)) & 3];
}

static void dbDrawFight(int gx) {
  int fdir = (int)dbFoeDir();
  int fx = (int)db_foe_x;
  const char *const *foeRows = FOE_POSES[db_foe][db_foe_pose];
  int fh = FOE_H[db_foe];

  if (db_fight == FIGHT_ARRIVE && db_foe == FOE_PICCOLO) {
    // Flickers in: dotted at first, then solid.
    if (db_phase_t < 0.3f) dbDrawSprite(foeRows, DB_SIDE_W, fh, fx, DB_FEET_Y - fh + 1, fdir, true);
    else if ((int)(db_phase_t * 30) & 1) dbDrawChar(foeRows, fh, fx, 0, fdir, false);
  } else if (db_fight != FIGHT_WIN) {
    bool tumble = db_fight == FIGHT_KO && ((int)(db_phase_t * 12) & 1);
    dbDrawChar(foeRows, fh, fx, (int)db_foe_lift, fdir, tumble);
  }

  if (db_fight == FIGHT_CHARGE) {
    // Both gather a ball of ki in front of them.
    int r = 1 + (int)(db_phase_t * 4);
    dbDisc((int)dbGokuFront(), dbFistY(), r);
    dbDisc((int)dbFoeFront(), dbFistY(), r);
  }
  if (db_fight == FIGHT_STRUGGLE) {
    int y = dbFistY();
    bool pushed = db_goku_pose == POSE_STRAIN, comeback = db_phase_t >= 2.7f;
    dbDrawHBeam((int)dbGokuFront(), (int)db_clash_x, y, false, comeback ? 2 : 0);
    dbDrawHBeam((int)db_clash_x, (int)dbFoeFront(), y, db_foe == FOE_PICCOLO, pushed ? 2 : 0);
    dbDisc((int)db_clash_x, y, (comeback ? 5 : 4) + ((int)(db_clock * 20) & 1));
  }
  if (db_flare_t > 0) {
    // Comeback burst: a ring blowing outward from Goku.
    int r = 4 + (int)((0.45f - db_flare_t) * 50);
    dbRing((int)(db_x + DB_SIDE_W / 2), DB_FEET_Y - 12, r);
  }

  dbDrawChar(dbGokuPoseRows(db_goku_pose), DB_SIDE_H, gx, 0, db_dir, false);
}

static void dbDrawGenkiBall(int cx, int cy, int r) {
  // Black body so it reads in front of the digits, shimmering checker inside.
  bool odd = (int)(db_clock * 10) & 1;
  for (int y = -r; y <= r; y++)
    for (int x = -r; x <= r; x++) {
      int d = x * x + y * y;
      if (d > r * r) continue;
      bool rim = d >= (r - 1) * (r - 1);
      dbPixel(cx + x, cy + y, rim || (((x + y) & 1) == odd) ? DISPLAY_WHITE : DISPLAY_BLACK);
    }
}

static void dbDrawDragonBall(int x, int y, int r) {
  dbDisc(x, y, r);
  dbPixel(x, y, DISPLAY_BLACK);                 // the star
  if ((int)(db_clock * 3) % 4 == 0) dbPixel(x - 1, y - 1, DISPLAY_BLACK);  // glint
}

static void dbDrawShenron() {
  float t = db_phase_t;
  if (t < 1.0f) return;
  float grow = fminf(1.0f, (t - 1.0f) / 2.0f);
  float tail = t > 6.0f ? fminf(1.0f, t - 6.0f) : 0.0f;
  if (tail >= 1.0f) return;
  float phase = t > 3.0f ? (t - 3.0f) * 2.0f : 0.0f;
  int n = 0;
  float px = 0, py = 0;
  dbShenronPoint(tail, phase, px, py);
  for (float u = tail + 0.0025f; u <= grow; u += 0.0025f, n++) {
    float x, y;
    dbShenronPoint(u, phase, x, y);
    float dx = x - px, dy = y - py, len = sqrtf(dx * dx + dy * dy);
    if (len < 0.01f) continue;
    float nx = -dy / len, ny = dx / len;
    if (ny > 0) { nx = -nx; ny = -ny; }        // normal points up
    int half = u < 0.1f ? 2 : 3;
    for (int w = -half + 1; w < half; w++) dbPixel((int)roundf(x + nx * w), (int)roundf(y + ny * w), DISPLAY_BLACK);
    dbPixel((int)roundf(x + nx * half), (int)roundf(y + ny * half));
    dbPixel((int)roundf(x - nx * half), (int)roundf(y - ny * half));
    if (n % 6 == 0) dbPixel((int)roundf(x), (int)roundf(y));                       // scales
    if (n % 9 == 0) dbPixel((int)roundf(x + nx * (half + 1)), (int)roundf(y + ny * (half + 1)));  // spines
    px = x;
    py = y;
  }
  // Head at the leading end, facing the way the body travels.
  float hx, hy, bx, by;
  dbShenronPoint(grow, phase, hx, hy);
  dbShenronPoint(fmaxf(0, grow - 0.01f), phase, bx, by);
  int dir = hx >= bx ? 1 : -1;
  int x0 = dir > 0 ? (int)hx - 3 : (int)hx - 14;
  for (int r = 0; r < 10; r++)
    for (int c = 0; c < 18; c++)
      if (SHENRON_HEAD[r][c] == 'X') dbPixel(dir > 0 ? x0 + c : x0 + 17 - c, (int)hy - 5 + r);
  // Whiskers trailing back from the snout.
  int sx = dir > 0 ? x0 + 15 : x0 + 2, sy = (int)hy;
  for (int k = 0; k < 10; k++) {
    int wx = sx - dir * k;
    dbPixel(wx, sy - 3 - (int)roundf(sinf(k * 0.5f + db_clock * 6) * 2) - k / 3);
    dbPixel(wx, sy + 4 + (int)roundf(sinf(k * 0.5f + db_clock * 6 + 1) * 2) + k / 3);
  }
}

static void dbDrawGokuIdle(int gx) {
  int lift = (int)db_lift;
  switch (db_idle) {
    case IDLE_WALK:
      dbDrawSide(dbWalkFrame(), gx, 0, db_dir);
      break;
    case IDLE_KATA:
    case IDLE_SPAR: {
      const DbMove *moves = db_idle == IDLE_KATA ? KATA : SPAR;
      const DbMove &m = moves[db_move];
      int jump = m.jump ? (int)(sinf(3.1416f * fminf(1.0f, db_move_t / m.secs)) * m.jump) : 0;
      dbDrawSide(m.pose, gx, jump, db_dir);
      if (db_idle == IDLE_SPAR) {
        // The sparring partner is his own afterimage, facing him.
        const char *const *answer = m.pose == SPR_STAND ? SPR_PUNCH : SPR_STAND;
        dbDrawSide(answer, gx + db_dir * 19, 0, -db_dir, true);
      }
      break;
    }
    case IDLE_POWER:
      dbDrawFront(false);
      dbDrawAura(1.0f);
      break;
    case IDLE_VOLLEY:
      dbDrawSide(db_shots_left > 0 || db_shot_timer > -0.2f ? SPR_FIRE : SPR_STAND, gx, 0, db_dir);
      break;
    case IDLE_BLASTS:
      dbDrawSide(((int)(db_clock * 12) & 1) && db_shots_left > 0 ? SPR_PUNCH : SPR_STAND, gx, 0, db_dir);
      break;
    case IDLE_BEAM:
      if (db_phase_t < 0.7f) {
        dbDrawSide(SPR_KAME, gx, 0, db_dir);
        int r = 1 + (int)(db_phase_t * 4);
        dbDisc((int)dbCol(DB_KAME_X), dbSideTop() + DB_KAME_Y, r);
      } else {
        dbDrawBeam(db_tail, db_beam);
        dbDrawSide(db_phase_t < 1.6f ? SPR_PUNCH : SPR_STAND, gx, 0, db_dir);
        if (db_phase_t < 1.6f) dbDisc((int)db_hx, (int)db_hy, 3);
      }
      break;
    case IDLE_FIGHT:
      dbDrawFight(gx);
      break;
    case IDLE_GENKI:
      if (!db_genki_thrown) {
        dbDrawSprite(SPR_GENKI, DB_FRONT_W, 27, gx + (DB_SIDE_W - DB_FRONT_W) / 2, DB_FEET_Y - 26, 1);
        for (int i = 0; i < DB_MAX_MOTES; i++)
          if (db_motes[i].active) dbPixel((int)db_motes[i].x, (int)db_motes[i].y);
      } else {
        dbDrawSide(db_boom_t > 0 || db_genki_r <= 0 ? SPR_STAND : SPR_PUNCH, gx, 0, db_dir);
      }
      if (db_genki_r >= 1) dbDrawGenkiBall((int)db_genki_x, (int)db_genki_y, (int)db_genki_r);
      break;
    case IDLE_FETCH:
      if (fabsf(db_walk_to - db_x) > 0.5f) dbDrawSide(dbWalkFrame(), gx, 0, db_dir);
      else dbDrawSide(SPR_STRAIN, gx, 0, db_dir);  // bends down for it
      break;
    case IDLE_SHENRON: {
      dbDrawSide(SPR_STAND, gx, 0, db_dir);
      float t = db_phase_t;
      if (t < 7.0f) {
        int r = t < 1.0f ? 2 + ((int)(db_clock * 12) & 1) : 2;
        for (int i = 0; i < 7; i++) dbDrawDragonBall(dbRowBallX(i), DB_GROUND_Y - 3, r);
        if (t < 1.0f && random(0, 3) == 0) {
          int bx = dbRowBallX(random(0, 7));
          dbBolt(bx, DB_GROUND_Y - 5, bx + (int)random(-6, 7), 24);
        }
      } else {
        for (int i = 0; i < 7; i++) dbDrawDragonBall((int)db_scatter_x[i], (int)db_scatter_y[i], 2);
      }
      dbDrawShenron();
      break;
    }
    case IDLE_NIMBUS:
      dbDrawSide(SPR_STAND, gx, lift, db_dir);
      dbDrawNimbus((int)db_cloud_x, DB_FEET_Y - (db_nimbus_stage >= 1 && db_nimbus_stage <= 2 ? lift : 8) + 1);
      break;
    default:
      dbDrawSide(SPR_STAND, gx, 0, db_dir);
  }
}

void displayClockWithDragonBall() {
  if (!db_init_done) resetDragonBallAnimation();

  // Not drawn for a while means another style took over; drop the demo there.
  unsigned long drawNow = millis();
  if (db_demo_on && drawNow - db_last_draw > 2000) dbEndDemo(false);
  db_last_draw = drawNow;

  struct tm timeinfo;
  if (!getTimeWithTimeout(&timeinfo)) {
    display.setTextSize(1);
    display.setCursor(20, 28);
    display.print(ntpSynced ? "Time Error" : "Syncing time...");
    return;
  }

  updateDragonBallAnimation(&timeinfo);

  if (!time_overridden) syncDisplayedTime(&timeinfo);
  maintainTimeOverride(&timeinfo, db_phase == DB_IDLE);

  bool ssj = db_phase >= DB_SSJ_UP;
  bool powering = db_phase == DB_IDLE && db_idle == IDLE_POWER;
  int amp = (db_phase == DB_SSJ_UP || db_phase == DB_SSJ_CRUMBLE) ? 2
          : (powering || db_burst_t > 0 || db_shock_t > 0) ? 1 : 0;
  int shake = amp ? (int)(db_clock * 40) % (2 * amp + 1) - amp : 0;
  int shakeY = amp == 2 ? -((int)(db_clock * 33) & 1) : 0;

  for (int i = 0; i < 5; i++)
    if (i != 2) dbDrawDigit(i, shake);
  display.setTextSize(3);
  display.setCursor(DIGIT_X[2] + shake, DB_DIGIT_Y);
  display.print(shouldShowColon() ? ':' : ' ');

  display.drawFastHLine(0, DB_GROUND_Y + shakeY, SCREEN_WIDTH, DISPLAY_WHITE);

  int gx = (int)db_x;
  if (db_ghost_t > 0) dbDrawSide(db_ghost_pose, (int)db_ghost_x, 0, db_ghost_dir, true);

  switch (db_phase) {
    case DB_IDLE:
      dbDrawGokuIdle(gx);
      break;

    case DB_BLINK:
      if (db_phase_t > 0.15f || ((int)(db_phase_t * 40) & 1)) dbDrawSide(SPR_STAND, gx, 0, db_dir);
      break;

    case DB_RUN:
      dbDrawSide(dbWalkFrame(), gx, 0, db_dir);
      break;

    case DB_CHARGE: {
      dbDrawSide(SPR_KAME, gx, 0, db_dir);
      float chargeTime = db_cur == 0 ? 1.0f : 0.55f;
      int r = 1 + (int)(3.0f * fminf(1.0f, db_phase_t / chargeTime));
      dbDisc((int)dbCol(DB_KAME_X), dbSideTop() + DB_KAME_Y,
                         r + ((int)(db_clock * 20) & 1));
      break;
    }

    case DB_FIRE:
    case DB_RETRACT: {
      int idx = db_change_idx[db_cur];
      dbDrawBeam(db_phase == DB_FIRE ? 0 : db_tail, db_beam);
      dbDrawSide(db_phase == DB_FIRE ? SPR_FIRE : SPR_STAND, gx, 0, db_dir);
      if (db_phase == DB_FIRE)
        dbDisc((int)db_hx, (int)db_hy, 3 + ((int)(db_clock * 20) & 1));
      if (db_burst_t > 0) {
        int r = (int)((0.35f - db_burst_t) * 20) + 3;
        dbRing(DIGIT_X[idx] + 7, DB_TARGET_Y, r);
      }
      break;
    }

    case DB_FORM:
      dbDrawSide(SPR_STAND, gx, 0, db_dir);
      break;

    case DB_SSJ_UP: {
      // Hair flickers between normal and Super Saiyan, faster as it builds.
      float k = db_phase_t / 2.2f;
      bool lit = k > 0.8f || ((int)(db_clock * (6 + 20 * k)) & 1);
      dbDrawFront(lit);
      dbDrawAura(0.6f + 0.6f * k);
      if (random(0, 3) == 0) {
        int bx = random(0, SCREEN_WIDTH);
        int ex = bx + (int)random(-12, 13);
        ex = constrain(ex, 0, SCREEN_WIDTH - 1);
        dbBolt(bx, 0, ex, random(30, 62));
      }
      break;
    }

    case DB_SSJ_CRUMBLE:
      dbDrawFront(true);
      dbDrawAura(1.2f);
      if (random(0, 2) == 0) {
        int bx = random(0, SCREEN_WIDTH);
        int ex = bx + (int)random(-12, 13);
        ex = constrain(ex, 0, SCREEN_WIDTH - 1);
        dbBolt(bx, 0, ex, random(30, 62));
      }
      break;

    case DB_SSJ_STRIKE: {
      dbDrawFront(true);
      dbDrawAura(1.2f);
      int idx = db_change_idx[db_cur < db_num_changes ? db_cur : db_num_changes - 1];
      if (db_phase_t < 0.15f)
        dbBolt((int)db_x + DB_SIDE_W / 2, DB_FEET_Y - DB_FRONT_H - 1, DIGIT_X[idx] + 7, DB_DIGIT_BOTTOM);
      break;
    }

    case DB_SSJ_DOWN:
      dbDrawFront(db_phase_t < 0.5f && ((int)(db_clock * 14) & 1));
      dbDrawAura(1.2f * (1.0f - db_phase_t / 0.9f));
      break;
  }

  if (db_ball_x >= 0) dbDrawDragonBall(db_ball_x, DB_GROUND_Y - 3, 2);
  if (db_pips_t > 0) {
    for (int i = 0; i < 7; i++) {
      int x = SCREEN_WIDTH / 2 - 18 + i * 6, y = 29;
      if (i < db_balls) dbDisc(x, y, 1);
      else { dbPixel(x - 1, y - 1); dbPixel(x + 1, y - 1); dbPixel(x - 1, y + 1); dbPixel(x + 1, y + 1); }
    }
  }
  if (db_boom_t > 0) {
    int r = (int)((0.8f - db_boom_t) * 40) + 2;
    dbRing(db_boom_x, db_boom_y, r);
    dbRing(db_boom_x, db_boom_y, r / 2);
  }
  if (db_flash_t > 0) dbDrawStar(db_flash_x, db_flash_y);

  for (int i = 0; i < DB_MAX_KI; i++)
    if (db_ki[i].active) dbDisc((int)db_ki[i].x, (int)db_ki[i].y, 1);

  for (int i = 0; i < DB_MAX_RUBBLE; i++)
    if (db_rubble[i].active && (db_rubble[i].life > 0.5f || ((int)(db_clock * 20) & 1)))
      {
        int rx = (int)db_rubble[i].x, ry = (int)db_rubble[i].y;
        dbPixel(rx, ry); dbPixel(rx + 1, ry); dbPixel(rx, ry + 1); dbPixel(rx + 1, ry + 1);
      }

  for (int i = 0; i < DB_MAX_SPARKS; i++)
    if (db_sparks[i].active) dbPixel((int)db_sparks[i].x, (int)db_sparks[i].y);

  drawMeridiemIndicator(112, 26, displayed_is_pm);
  if (!wifiConnected) drawNoWiFiIcon(0, 26);
}
