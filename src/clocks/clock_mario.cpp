/*
 * SmallOLED-PCMonitor - Mario Clock Implementation
 *
 * Mario clock style with animated Mario character that jumps to change digits.
 */

#include "../config/config.h"
#include "../display/display.h"
#include "clocks.h"
#include "clock_constants.h"
#include "clock_globals.h"

// Forward declarations for helper functions used by Mario clock
void drawTimeWithBounce();
void advanceDisplayedTime();
void updateSpecificDigit(int digitIndex, int newValue);

// Coin animation struct (needed before displayClockWithMario)
struct MarioCoin {
  float x, y, vy;
  bool active;
  uint8_t frame;
};
#define MAX_COINS 4
static MarioCoin coins[MAX_COINS];

// Idle encounter forward declarations
void startIdleEncounter();
void updateIdleEncounter();
void abortEncounter();
void drawEnemy(MarioEnemy& e);
void drawGoomba(int x, int y, int frame, bool squashing);
void drawSpiny(int x, int y, int frame, bool hit);
void drawMarioFireball(MarioFireball& fb);
void updateMarioFireball();
static void drawCoin(MarioCoin& c);
void drawKoopa(int x, int y, int frame, bool shellOnly);
static void drawStarSprite(int x, int y, uint8_t frame);
static void drawMushroomSprite(int x, int y, uint8_t frame);
static void drawBigMario(int x, int y, bool facingRight, int frame);

// Multi-enemy support (forward declaration)
static MarioEnemy secondEnemy;
static bool secondEnemyActive;

// Coin counter (SMB1 style, resets on reboot)
static uint16_t marioCoins = 0;

// Encounter variation (forward declaration for abort check)
enum EncounterVariation {
  ENCOUNTER_MARIO_VS_ENEMY,
  ENCOUNTER_ENEMY_PASS_BY,
  ENCOUNTER_COIN_BLOCKS,
  ENCOUNTER_MULTI_ENEMY,
  ENCOUNTER_STAR,
  ENCOUNTER_MUSHROOM
};
static EncounterVariation encounterVariation;

// Star power-up state
struct MarioStar {
  float x, y, vy, vx;
  bool active;
  uint8_t frame;
  int bounceCount;
};
static MarioStar marioStar = {0, 0, 0, 0, false, 0, 0};
static bool marioStarPowered = false;
static uint8_t marioStarTimer = 0;

// Mushroom power-up state
struct MarioMushroom {
  float x, vx;
  bool active;
  uint8_t frame;
};
static MarioMushroom marioMushroom = {0, 0, false, 0};
static uint8_t marioGrowthTimer = 0;

// Shell sliding state
static float shellSlideSpeed = 0;

// ========== Draw Time With Bounce Effect ==========
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

// ========== Advance Displayed Time ==========
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
  time_override_start = millis();
}

// ========== Update Specific Digit ==========
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
  time_override_start = millis();
}

// ========== Display Clock With Mario ==========
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

  // Check if time override should be cleared
  if (time_overridden) {
    bool ntp_matches = (timeinfo.tm_hour == displayed_hour && timeinfo.tm_min == displayed_min);
    bool timeout_expired = (millis() - time_override_start > TIME_OVERRIDE_MAX_MS);

    if (ntp_matches || timeout_expired) {
      time_overridden = false;
      // If timeout expired but NTP doesn't match, force sync to real time
      if (timeout_expired && !ntp_matches) {
        displayed_hour = timeinfo.tm_hour;
        displayed_min = timeinfo.tm_min;
      }
    }
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

  int date_x = (SCREEN_WIDTH - DATE_DISPLAY_WIDTH) / 2;
  display.setCursor(date_x, 4);
  display.print(dateStr);

  // SMB1-style coin counter (top-left, only when encounters enabled)
  if (settings.marioIdleEncounters) {
    // Mini coin icon (5x6 oval)
    display.fillRect(1, 5, 4, 4, DISPLAY_WHITE);
    display.drawPixel(2, 4, DISPLAY_WHITE);
    display.drawPixel(3, 4, DISPLAY_WHITE);
    display.drawPixel(2, 9, DISPLAY_WHITE);
    display.drawPixel(3, 9, DISPLAY_WHITE);
    display.drawPixel(2, 6, DISPLAY_BLACK);  // Center hole
    display.drawPixel(3, 6, DISPLAY_BLACK);
    display.drawPixel(2, 7, DISPLAY_BLACK);
    display.drawPixel(3, 7, DISPLAY_BLACK);
    // Counter: x00 format (2 digits, max 99)
    char coinStr[5];
    sprintf(coinStr, "x%02d", (int)(marioCoins % 100));
    display.setCursor(6, 4);
    display.print(coinStr);
  }

  updateDigitBounce();
  drawTimeWithBounce();

  updateMarioAnimation(&timeinfo);

  int mario_draw_y = mario_base_y + (int)mario_jump_y;
  bool isJumping = (mario_state == MARIO_JUMPING || mario_state == MARIO_ENCOUNTER_JUMPING);

  // Star-powered Mario: flicker effect (skip drawing every other frame)
  if (marioStarPowered && (marioStarTimer % 3 != 0)) {
    // Draw inverted/bigger Mario during star power
    drawBigMario((int)mario_x, mario_draw_y, mario_facing_right, mario_walk_frame);
  } else if (marioGrowthTimer > 0) {
    drawBigMario((int)mario_x, mario_draw_y, mario_facing_right, mario_walk_frame);
  } else {
    drawMario((int)mario_x, mario_draw_y, mario_facing_right, mario_walk_frame, isJumping);
  }

  // Draw idle encounter objects
  if (currentEnemy.type != ENEMY_NONE) {
    drawEnemy(currentEnemy);
  }
  if (secondEnemyActive && secondEnemy.type != ENEMY_NONE) {
    drawEnemy(secondEnemy);
  }
  if (marioFireball.active) {
    drawMarioFireball(marioFireball);
  }
  // Draw star and mushroom power-ups
  if (marioStar.active) {
    drawStarSprite((int)marioStar.x, (int)marioStar.y, marioStar.frame);
  }
  if (marioMushroom.active) {
    drawMushroomSprite((int)marioMushroom.x, mario_base_y, marioMushroom.frame);
  }
  // Draw coins
  for (int i = 0; i < MAX_COINS; i++) {
    drawCoin(coins[i]);
  }

  // Draw no-WiFi icon if disconnected
  if (!wifiConnected) {
    drawNoWiFiIcon(0, 0);
  }
}

// ========== Update Mario Animation ==========
void updateMarioAnimation(struct tm* timeinfo) {
  unsigned long currentMillis = millis();

  // Encounters run at higher framerate for smoother animation
  bool inEncounter = (mario_state >= MARIO_ENCOUNTER_WALKING && mario_state <= MARIO_ENCOUNTER_RETURNING);
  unsigned long animSpeed = inEncounter ? ENCOUNTER_ANIM_SPEED : MARIO_ANIM_SPEED;

  if (currentMillis - last_mario_update < animSpeed) {
    return;
  }
  last_mario_update = currentMillis;

  int seconds = timeinfo->tm_sec;
  int current_minute = timeinfo->tm_min;

  if (current_minute != last_minute) {
    last_minute = current_minute;
    animation_triggered = false;
  }

  // Abort any running encounter when minute-change time approaches
  // Minute-change animation always has priority over idle encounters
  if (seconds >= MARIO_ANIMATION_TRIGGER_SECOND &&
      mario_state >= MARIO_ENCOUNTER_WALKING && mario_state <= MARIO_ENCOUNTER_RETURNING) {
    abortEncounter();
  }

  if (seconds >= MARIO_ANIMATION_TRIGGER_SECOND && !animation_triggered && mario_state == MARIO_IDLE) {
    animation_triggered = true;
    calculateTargetDigits(displayed_hour, displayed_min);
    if (num_targets > 0) {
      current_target_index = 0;
      mario_x = MARIO_START_X;
      mario_state = MARIO_WALKING;
      mario_facing_right = true;
      digit_bounce_triggered = false;
    }
  }

  switch (mario_state) {
    case MARIO_IDLE:
      mario_walk_frame = 0;
      mario_x = MARIO_START_X;
      // Idle encounter trigger
      if (settings.marioIdleEncounters && seconds < 50 && !animation_triggered) {
        if (currentMillis - lastEncounterEnd >= nextEncounterDelay) {
          startIdleEncounter();
        }
      }
      break;

    // Idle encounter states
    case MARIO_ENCOUNTER_WALKING:
    case MARIO_ENCOUNTER_JUMPING:
    case MARIO_ENCOUNTER_SHOOTING:
    case MARIO_ENCOUNTER_SQUASH:
    case MARIO_ENCOUNTER_RETURNING:
      updateIdleEncounter();
      break;

    case MARIO_WALKING:
      if (current_target_index < num_targets) {
        int target = target_x_positions[current_target_index];

        if (abs(mario_x - target) > MARIO_TARGET_PROXIMITY) {
          float walkSpeed = settings.marioWalkSpeed / 10.0f;
          if (mario_x < target) {
            mario_x += walkSpeed;
            mario_facing_right = true;
          } else {
            mario_x -= walkSpeed;
            mario_facing_right = false;
          }
          int frameCount = settings.marioSmoothAnimation ? 4 : 2;
          mario_walk_frame = (mario_walk_frame + 1) % frameCount;
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

          jump_velocity = MARIO_BOUNCE_VELOCITY;
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
      mario_x += settings.marioWalkSpeed / 10.0f;
      {
        int frameCount = settings.marioSmoothAnimation ? 4 : 2;
        mario_walk_frame = (mario_walk_frame + 1) % frameCount;
      }

      if (mario_x > SCREEN_WIDTH + 15) {
        mario_state = MARIO_IDLE;
        mario_x = MARIO_START_X;
      }
      break;
  }
}

// ========== Draw Mario Sprite ==========
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

    if (settings.marioSmoothAnimation) {
      // 4-frame mode: both arms animate in opposite phase
      if (facingRight) {
        display.drawPixel(sx + 1, sy + 4 - (frame % 2), DISPLAY_WHITE);  // Back arm (opposite phase)
        display.drawPixel(sx + 6, sy + 3 + (frame % 2), DISPLAY_WHITE);  // Front arm
      } else {
        display.drawPixel(sx + 6, sy + 4 - (frame % 2), DISPLAY_WHITE);  // Back arm (opposite phase)
        display.drawPixel(sx + 1, sy + 3 + (frame % 2), DISPLAY_WHITE);  // Front arm
      }

      // 4-frame walk cycle for smoother animation
      switch (frame % 4) {
        case 0:  // Legs together (neutral)
          display.fillRect(sx + 2, sy + 6, 2, 3, DISPLAY_WHITE);
          display.fillRect(sx + 4, sy + 6, 2, 3, DISPLAY_WHITE);
          break;
        case 1:  // Left leg forward
          display.fillRect(sx + 1, sy + 6, 2, 3, DISPLAY_WHITE);
          display.fillRect(sx + 4, sy + 6, 2, 3, DISPLAY_WHITE);
          break;
        case 2:  // Legs apart (full stride)
          display.fillRect(sx + 1, sy + 6, 2, 3, DISPLAY_WHITE);
          display.fillRect(sx + 5, sy + 6, 2, 3, DISPLAY_WHITE);
          break;
        case 3:  // Right leg forward
          display.fillRect(sx + 2, sy + 6, 2, 3, DISPLAY_WHITE);
          display.fillRect(sx + 5, sy + 6, 2, 3, DISPLAY_WHITE);
          break;
      }
    } else {
      // 2-frame mode (original): back arm static, front arm moves
      if (facingRight) {
        display.drawPixel(sx + 1, sy + 4, DISPLAY_WHITE);
        display.drawPixel(sx + 6, sy + 3 + (frame % 2), DISPLAY_WHITE);
      } else {
        display.drawPixel(sx + 6, sy + 4, DISPLAY_WHITE);
        display.drawPixel(sx + 1, sy + 3 + (frame % 2), DISPLAY_WHITE);
      }

      // 2-frame walk cycle (original)
      if (frame == 0) {
        display.fillRect(sx + 2, sy + 6, 2, 3, DISPLAY_WHITE);
        display.fillRect(sx + 4, sy + 6, 2, 3, DISPLAY_WHITE);
      } else {
        display.fillRect(sx + 1, sy + 6, 2, 3, DISPLAY_WHITE);
        display.fillRect(sx + 5, sy + 6, 2, 3, DISPLAY_WHITE);
      }
    }
  }
}

// ========== Idle Encounter Functions ==========

static unsigned long rollEncounterDelay() {
  switch (settings.marioEncounterFreq) {
    case 0: return random(25000, 35000); // Rare
    case 2: return random(8000, 15000);  // Frequent
    case 3: return random(2000, 5000);   // Chaotic
    default: return random(15000, 25000); // Normal
  }
}

// Coin block encounter targets
static int coinDigitIndices[3];
static int numCoinTargets;
static int currentCoinTargetIdx;
static bool coinDigitBounceTriggered;

// Enemy walk speed varies by type (used after meeting point, during interaction)
static float getEnemyWalkSpeed(EnemyType type) {
  if (type == ENEMY_GOOMBA) return 0.7f;
  if (type == ENEMY_KOOPA) return 0.8f;
  return 1.3f;  // Spiny
}

// Calculated approach speed so Mario and enemy meet at desired point
static float encounterEnemyApproachSpeed = 1.0f;
static float encounterMeetX = 64.0f;

// Encounter speed multiplier based on setting
static float getEncounterSpeedMult() {
  const float mult[] = {0.65f, 0.85f, 1.1f};  // Slow, Normal, Fast
  uint8_t idx = min((uint8_t)2, settings.marioEncounterSpeed);
  return mult[idx];
}

// Spawn a coin above a digit
static void spawnCoin(int digitIndex) {
  for (int i = 0; i < MAX_COINS; i++) {
    if (!coins[i].active) {
      coins[i].x = DIGIT_X[digitIndex] + 7;  // Center of digit
      coins[i].y = TIME_Y - 14;  // Well above digits for visibility
      coins[i].vy = -2.5f * ENCOUNTER_TIME_SCALE;  // Pop upward
      coins[i].active = true;
      coins[i].frame = 0;
      marioCoins++;
      if (marioCoins > 99) marioCoins = 0;
      break;
    }
  }
}

// Update all active coins
static void updateCoins() {
  float ts = ENCOUNTER_TIME_SCALE;
  for (int i = 0; i < MAX_COINS; i++) {
    if (!coins[i].active) continue;
    coins[i].vy += 0.3f * ts;  // Gravity
    coins[i].y += coins[i].vy;
    coins[i].frame++;
    // Remove when fallen back near digit top
    if (coins[i].y > TIME_Y + 3) {
      coins[i].active = false;
    }
  }
}

// Draw a spinning coin sprite
static void drawCoin(MarioCoin& c) {
  if (!c.active) return;
  int cx = (int)c.x;
  int cy = (int)c.y;
  // Spinning coin: alternates between wide and narrow
  if ((c.frame / 3) % 2 == 0) {
    // Wide phase (4x6)
    display.fillRect(cx, cy + 1, 4, 4, DISPLAY_WHITE);
    display.fillRect(cx + 1, cy, 2, 6, DISPLAY_WHITE);
  } else {
    // Narrow phase (2x6)
    display.fillRect(cx + 1, cy, 2, 6, DISPLAY_WHITE);
  }
}

// Setup random digit targets for coin block encounter
static void setupCoinBlockTargets() {
  int validDigits[] = {0, 1, 3, 4};
  numCoinTargets = random(1, 4);  // 1 to 3 targets

  // Shuffle valid digits
  for (int i = 3; i > 0; i--) {
    int j = random(i + 1);
    int tmp = validDigits[i];
    validDigits[i] = validDigits[j];
    validDigits[j] = tmp;
  }

  // Take first numCoinTargets
  for (int i = 0; i < numCoinTargets; i++) {
    coinDigitIndices[i] = validDigits[i];
  }

  // Sort left-to-right
  for (int i = 0; i < numCoinTargets - 1; i++) {
    for (int j = i + 1; j < numCoinTargets; j++) {
      if (coinDigitIndices[j] < coinDigitIndices[i]) {
        int tmp = coinDigitIndices[i];
        coinDigitIndices[i] = coinDigitIndices[j];
        coinDigitIndices[j] = tmp;
      }
    }
  }

  currentCoinTargetIdx = 0;
  coinDigitBounceTriggered = false;
}

// Calculate enemy approach speed so Mario and enemy meet at desired meetX
static void calcApproachSpeed(float meetX, float enemyStartX) {
  float ts = ENCOUNTER_TIME_SCALE;
  float sm = getEncounterSpeedMult();
  float walkSpeed = (settings.marioWalkSpeed / 10.0f) * 1.3f * sm * ts;
  float marioTravel = meetX - MARIO_START_X;
  float enemyTravel = enemyStartX - meetX;
  if (marioTravel < 1.0f) marioTravel = 1.0f;
  encounterEnemyApproachSpeed = (enemyTravel * walkSpeed) / marioTravel;
  // Cap enemy speed so encounters don't look too rushed
  encounterEnemyApproachSpeed = min(encounterEnemyApproachSpeed, walkSpeed * 0.75f);
  encounterMeetX = meetX;
}

// Pick a random enemy type (Goomba, Spiny, or Koopa)
static EnemyType randomEnemyType() {
  int r = random(100);
  if (r < 35) return ENEMY_GOOMBA;
  if (r < 65) return ENEMY_SPINY;
  return ENEMY_KOOPA;
}

void startIdleEncounter() {
  marioFireball.active = false;
  secondEnemyActive = false;
  marioStar.active = false;
  marioStarPowered = false;
  marioStarTimer = 0;
  marioMushroom.active = false;
  marioGrowthTimer = 0;
  shellSlideSpeed = 0;

  // Pick encounter variation
  int roll = random(100);
  if (roll < 15) {
    // 15% — enemy passes by, no Mario
    encounterVariation = ENCOUNTER_ENEMY_PASS_BY;
    currentEnemy.type = randomEnemyType();
    currentEnemy.state = ENEMY_WALKING;
    currentEnemy.x = SCREEN_WIDTH + random(5, 30);
    currentEnemy.walkFrame = 0;
    currentEnemy.animTimer = 0;
    currentEnemy.fromRight = true;
    encounterEnemyApproachSpeed = getEnemyWalkSpeed(currentEnemy.type) * getEncounterSpeedMult() * ENCOUNTER_TIME_SCALE;
    mario_x = MARIO_START_X;  // Keep Mario off-screen
    mario_state = MARIO_ENCOUNTER_WALKING;
    return;
  } else if (roll < 40) {
    // 25% — coin blocks (Mario hits digits for coins)
    encounterVariation = ENCOUNTER_COIN_BLOCKS;
    currentEnemy.type = ENEMY_NONE;
    setupCoinBlockTargets();
    mario_x = MARIO_START_X;
    mario_facing_right = true;
    mario_jump_y = 0;
    jump_velocity = 0;
    mario_state = MARIO_ENCOUNTER_WALKING;
    return;
  } else if (roll < 48) {
    // 8% — multi-enemy (two Goombas)
    encounterVariation = ENCOUNTER_MULTI_ENEMY;

    float meetX = random(20, 85);
    float enemyStartX = SCREEN_WIDTH + random(5, 15);
    calcApproachSpeed(meetX, enemyStartX);

    currentEnemy.type = ENEMY_GOOMBA;
    currentEnemy.state = ENEMY_WALKING;
    currentEnemy.x = enemyStartX;
    currentEnemy.walkFrame = 0;
    currentEnemy.animTimer = 0;
    currentEnemy.fromRight = true;

    secondEnemy.type = ENEMY_GOOMBA;
    secondEnemy.state = ENEMY_WALKING;
    secondEnemy.x = enemyStartX + 18;
    secondEnemy.walkFrame = 4;
    secondEnemy.animTimer = 0;
    secondEnemy.fromRight = true;
    secondEnemyActive = true;
  } else if (roll < 53) {
    // 5% — star power-up (rare)
    encounterVariation = ENCOUNTER_STAR;
    currentEnemy.type = ENEMY_NONE;
    // Pick a random digit to hit (like coin blocks, but just 1)
    int validDigits[] = {0, 1, 3, 4};
    numCoinTargets = 1;
    coinDigitIndices[0] = validDigits[random(4)];
    currentCoinTargetIdx = 0;
    coinDigitBounceTriggered = false;
    mario_x = MARIO_START_X;
    mario_facing_right = true;
    mario_jump_y = 0;
    jump_velocity = 0;
    mario_state = MARIO_ENCOUNTER_WALKING;
    return;
  } else if (roll < 58) {
    // 5% — mushroom power-up (rare)
    encounterVariation = ENCOUNTER_MUSHROOM;
    currentEnemy.type = ENEMY_NONE;
    int validDigits[] = {0, 1};  // Hour digits only — mushroom needs room to run right
    numCoinTargets = 1;
    coinDigitIndices[0] = validDigits[random(2)];
    currentCoinTargetIdx = 0;
    coinDigitBounceTriggered = false;
    mario_x = MARIO_START_X;
    mario_facing_right = true;
    mario_jump_y = 0;
    jump_velocity = 0;
    mario_state = MARIO_ENCOUNTER_WALKING;
    return;
  } else {
    // 42% — normal single enemy encounter (includes Koopa)
    encounterVariation = ENCOUNTER_MARIO_VS_ENEMY;

    float meetX = random(20, 100);
    float enemyStartX = SCREEN_WIDTH + random(5, 15);
    calcApproachSpeed(meetX, enemyStartX);

    currentEnemy.type = randomEnemyType();
    currentEnemy.state = ENEMY_WALKING;
    currentEnemy.x = enemyStartX;
    currentEnemy.walkFrame = 0;
    currentEnemy.animTimer = 0;
    currentEnemy.fromRight = true;
  }

  mario_x = MARIO_START_X;
  mario_facing_right = true;
  mario_jump_y = 0;
  jump_velocity = 0;
  mario_state = MARIO_ENCOUNTER_WALKING;
}

void abortEncounter() {
  currentEnemy.type = ENEMY_NONE;
  currentEnemy.state = ENEMY_DEAD;
  secondEnemyActive = false;
  secondEnemy.type = ENEMY_NONE;
  marioFireball.active = false;
  marioStar.active = false;
  marioStarPowered = false;
  marioStarTimer = 0;
  marioMushroom.active = false;
  marioGrowthTimer = 0;
  shellSlideSpeed = 0;
  for (int i = 0; i < MAX_COINS; i++) coins[i].active = false;
  mario_state = MARIO_IDLE;
  mario_x = MARIO_START_X;
  mario_jump_y = 0;
  jump_velocity = 0;
  lastEncounterEnd = millis();
  nextEncounterDelay = rollEncounterDelay();
}

#define ENCOUNTER_GOOMBA_DIST 14  // Mario stops this far from Goomba before jumping
#define ENCOUNTER_SPINY_DIST 28   // Mario stops this far from Spiny before shooting
#define FIREBALL_SPEED 3.5f
#define FIREBALL_GRAVITY 0.5f
#define FIREBALL_BOUNCE -2.8f
#define SQUASH_FRAMES 8
#define HIT_FRAMES 12

// Track enemy Y for fall-off-screen animation
static float enemyFallY = 0;
static float enemyFallVY = 0;

void updateIdleEncounter() {
  // Scale all movement by time factor and speed setting
  float ts = ENCOUNTER_TIME_SCALE;
  float sm = getEncounterSpeedMult();
  float walkSpeed = (settings.marioWalkSpeed / 10.0f) * 1.3f * sm * ts;
  int frameCount = settings.marioSmoothAnimation ? 4 : 2;
  float enemySpeed = getEnemyWalkSpeed(currentEnemy.type) * sm * ts;

  // Always update enemy walk animation
  currentEnemy.walkFrame++;
  if (secondEnemyActive) secondEnemy.walkFrame++;

  // Always update coins
  updateCoins();

  switch (mario_state) {
    case MARIO_ENCOUNTER_WALKING: {
      // Move enemies using calculated approach speed
      if (currentEnemy.state == ENEMY_WALKING) {
        currentEnemy.x -= encounterEnemyApproachSpeed;
      }
      if (secondEnemyActive && secondEnemy.state == ENEMY_WALKING) {
        secondEnemy.x -= encounterEnemyApproachSpeed;
      }

      // Pass-through: enemy just walks across, no Mario
      if (encounterVariation == ENCOUNTER_ENEMY_PASS_BY) {
        float lastEnemyX = secondEnemyActive ? secondEnemy.x : currentEnemy.x;
        if (lastEnemyX < -15) {
          currentEnemy.type = ENEMY_NONE;
          currentEnemy.state = ENEMY_DEAD;
          secondEnemyActive = false;
          mario_state = MARIO_IDLE;
          lastEncounterEnd = millis();
          nextEncounterDelay = rollEncounterDelay();
        }
        break;
      }

      // Coin blocks / star / mushroom: walk toward digit target or power-up
      if (encounterVariation == ENCOUNTER_COIN_BLOCKS ||
          encounterVariation == ENCOUNTER_STAR ||
          encounterVariation == ENCOUNTER_MUSHROOM) {

        // Star: always update physics when active, chase after bouncing settles
        if (encounterVariation == ENCOUNTER_STAR && marioStar.active) {
          // Always update star bounce physics
          marioStar.vy += 0.4f * ts;
          marioStar.y += marioStar.vy;
          marioStar.x += marioStar.vx;
          marioStar.frame++;
          if (marioStar.y >= mario_base_y - 8) {
            marioStar.y = mario_base_y - 8;
            marioStar.vy = -2.0f * ts;
            marioStar.bounceCount++;
            if (marioStar.bounceCount >= 3) {
              marioStar.vx = 0;  // Stop drifting when settled
            }
          }

          if (marioStar.bounceCount >= 3) {
            // Chase the star
            if (abs(mario_x - marioStar.x) > 5) {
              mario_x += (marioStar.x > mario_x) ? walkSpeed : -walkSpeed;
              mario_facing_right = (marioStar.x > mario_x);
              mario_walk_frame = (mario_walk_frame + 1) % frameCount;
            } else {
              // Caught the star!
              marioStar.active = false;
              marioStarPowered = true;
              marioStarTimer = 50;  // ~0.8s of star power at 60fps
              marioCoins += 5;
              mario_state = MARIO_ENCOUNTER_RETURNING;
              mario_facing_right = true;
            }
          }
          // While star is bouncing, Mario just waits (stands still)
          break;
        }

        // Mushroom: after spawning, chase the mushroom sliding along ground
        if (encounterVariation == ENCOUNTER_MUSHROOM && marioMushroom.active) {
          marioMushroom.x += marioMushroom.vx;
          marioMushroom.frame++;
          if (abs(mario_x - marioMushroom.x) > 5) {
            mario_x += walkSpeed;
            mario_facing_right = true;
            mario_walk_frame = (mario_walk_frame + 1) % frameCount;
          } else {
            // Caught the mushroom!
            marioMushroom.active = false;
            marioGrowthTimer = 255;  // Stay big until off-screen
            marioCoins += 3;
            mario_state = MARIO_ENCOUNTER_RETURNING;
            mario_facing_right = true;
          }
          // Stop mushroom at screen edge
          if (marioMushroom.x > SCREEN_WIDTH + 10) {
            marioMushroom.active = false;
            mario_state = MARIO_ENCOUNTER_RETURNING;
            mario_facing_right = true;
          }
          break;
        }

        int targetX = DIGIT_X[coinDigitIndices[currentCoinTargetIdx]] + 9;  // Center of digit (size 3 = 18px wide)
        if (abs(mario_x - targetX) > 3) {
          if (mario_x < targetX) {
            mario_x += walkSpeed;
            mario_facing_right = true;
          } else {
            mario_x -= walkSpeed;
            mario_facing_right = false;
          }
          mario_walk_frame = (mario_walk_frame + 1) % frameCount;
        } else {
          mario_x = targetX;
          mario_state = MARIO_ENCOUNTER_JUMPING;
          jump_velocity = JUMP_POWER * ts;
          mario_jump_y = 0;
          coinDigitBounceTriggered = false;
        }
        break;
      }

      // Normal/multi enemy: Mario walks right toward enemy
      float stopDist = (currentEnemy.type == ENEMY_SPINY) ? ENCOUNTER_SPINY_DIST : ENCOUNTER_GOOMBA_DIST;
      float distToEnemy = currentEnemy.x - mario_x;

      if (distToEnemy > stopDist) {
        mario_x += walkSpeed;
        mario_facing_right = true;
        mario_walk_frame = (mario_walk_frame + 1) % frameCount;
      } else {
        // Close enough — interact based on enemy type
        if (currentEnemy.type == ENEMY_SPINY) {
          // Spiny: shoot fireball (can't stomp)
          mario_state = MARIO_ENCOUNTER_SHOOTING;
          marioFireball.x = mario_x + 6;
          marioFireball.y = mario_base_y - 6;
          marioFireball.vy = -1.5f * ts;
          marioFireball.active = true;
        } else {
          // Goomba & Koopa: jump on them
          mario_state = MARIO_ENCOUNTER_JUMPING;
          jump_velocity = JUMP_POWER * ts;
          mario_jump_y = 0;
        }
      }
      break;
    }

    case MARIO_ENCOUNTER_JUMPING: {
      // Coin blocks / star / mushroom: jump to hit digit from below
      if (encounterVariation == ENCOUNTER_COIN_BLOCKS ||
          encounterVariation == ENCOUNTER_STAR ||
          encounterVariation == ENCOUNTER_MUSHROOM) {
        jump_velocity += GRAVITY * ts;
        mario_jump_y += jump_velocity;

        int mario_head_y = mario_base_y + (int)mario_jump_y - MARIO_HEAD_OFFSET;
        if (!coinDigitBounceTriggered && jump_velocity < 0 && mario_head_y <= DIGIT_BOTTOM) {
          coinDigitBounceTriggered = true;
          int digitIdx = coinDigitIndices[currentCoinTargetIdx];
          triggerDigitBounce(digitIdx);

          if (encounterVariation == ENCOUNTER_STAR) {
            // Spawn star from digit
            marioStar.x = DIGIT_X[digitIdx] + 7;
            marioStar.y = TIME_Y - 14;
            marioStar.vy = -3.0f * ts;
            marioStar.vx = 1.2f * ts;  // Drift right
            marioStar.active = true;
            marioStar.frame = 0;
            marioStar.bounceCount = 0;
          } else if (encounterVariation == ENCOUNTER_MUSHROOM) {
            // Spawn mushroom ahead of Mario so there's a visible chase
            marioMushroom.x = DIGIT_X[digitIdx] + 25;
            marioMushroom.vx = walkSpeed * 0.7f;
            marioMushroom.active = true;
            marioMushroom.frame = 0;
          } else {
            spawnCoin(digitIdx);
          }
          jump_velocity = MARIO_BOUNCE_VELOCITY * ts;
        }

        if (mario_jump_y >= 0) {
          mario_jump_y = 0;
          jump_velocity = 0;
          if (encounterVariation == ENCOUNTER_COIN_BLOCKS) {
            currentCoinTargetIdx++;
            if (currentCoinTargetIdx < numCoinTargets) {
              mario_state = MARIO_ENCOUNTER_WALKING;
            } else {
              mario_state = MARIO_ENCOUNTER_RETURNING;
              mario_facing_right = true;
            }
          } else {
            // Star/mushroom: go back to walking to chase the power-up
            mario_state = MARIO_ENCOUNTER_WALKING;
          }
        }
        break;
      }

      // Enemy encounter: move toward enemy center during jump
      if (currentEnemy.state == ENEMY_WALKING) {
        float distToEnemy = currentEnemy.x - mario_x;
        if (distToEnemy > 2) {
          float approachSpeed = min(walkSpeed * 0.8f, distToEnemy * 0.3f * ts);
          mario_x += approachSpeed;
        }
        currentEnemy.x -= encounterEnemyApproachSpeed * 0.3f;
      }
      // Keep second enemy moving during Mario's jump
      if (secondEnemyActive && secondEnemy.state == ENEMY_WALKING) {
        secondEnemy.x -= encounterEnemyApproachSpeed * 0.5f;
      }

      jump_velocity += GRAVITY * ts;
      mario_jump_y += jump_velocity;

      // Check if Mario's head hits any digit while jumping (bonus bounce)
      int mario_head_y = mario_base_y + (int)mario_jump_y - MARIO_HEAD_OFFSET;
      if (jump_velocity < 0 && mario_head_y <= DIGIT_BOTTOM) {
        for (int i = 0; i < 5; i++) {
          if (i == 2) continue;
          int digitCenter = DIGIT_X[i] + 9;
          if (abs((int)mario_x - digitCenter) < 12 && digit_offset_y[i] == 0) {
            triggerDigitBounce(i);
            break;
          }
        }
      }

      // Check if Mario lands on enemy (trigger at enemy head level, not ground)
      if (jump_velocity > 0 && mario_jump_y >= -5) {
        if (currentEnemy.state == ENEMY_WALKING &&
            abs(mario_x - currentEnemy.x) < 10) {
          mario_x = currentEnemy.x;
          mario_jump_y = 0;
          jump_velocity = 0;

          // Multi-enemy: squash first, immediately bounce to second
          if (encounterVariation == ENCOUNTER_MULTI_ENEMY && secondEnemyActive) {
            currentEnemy.type = ENEMY_NONE;
            currentEnemy.state = ENEMY_DEAD;
            currentEnemy = secondEnemy;
            secondEnemyActive = false;
            jump_velocity = JUMP_POWER * ts;
            mario_jump_y = -5;
          } else if (currentEnemy.type == ENEMY_KOOPA) {
            // Koopa: retreat into shell and slide away
            currentEnemy.state = ENEMY_SHELL_SLIDING;
            currentEnemy.animTimer = 30;  // Shell slides for ~30 frames
            shellSlideSpeed = 4.0f * ts;  // Fast shell slide to the right
            mario_state = MARIO_ENCOUNTER_SQUASH;
            marioCoins++;
          } else {
            currentEnemy.state = ENEMY_SQUASHING;
            currentEnemy.animTimer = SQUASH_FRAMES;
            mario_state = MARIO_ENCOUNTER_SQUASH;
            marioCoins++;
          }
        } else if (mario_jump_y >= 0) {
          mario_jump_y = 0;
          jump_velocity = 0;
          mario_state = MARIO_ENCOUNTER_RETURNING;
          mario_facing_right = true;
        }
      }
      break;
    }

    case MARIO_ENCOUNTER_SHOOTING: {
      updateMarioFireball();
      // Keep second enemy moving
      if (secondEnemyActive && secondEnemy.state == ENEMY_WALKING) {
        secondEnemy.x -= getEnemyWalkSpeed(secondEnemy.type) * ts;
      }

      if (!marioFireball.active) {
        if (currentEnemy.state == ENEMY_HIT) {
          mario_state = MARIO_ENCOUNTER_SQUASH;
        } else {
          mario_state = MARIO_ENCOUNTER_RETURNING;
          mario_facing_right = true;
        }
      }
      break;
    }

    case MARIO_ENCOUNTER_SQUASH: {
      // Shell sliding: Koopa shell slides across screen
      if (currentEnemy.state == ENEMY_SHELL_SLIDING) {
        currentEnemy.x += shellSlideSpeed;
        currentEnemy.walkFrame++;
        if (currentEnemy.x > SCREEN_WIDTH + 15 || currentEnemy.animTimer == 0) {
          currentEnemy.type = ENEMY_NONE;
          currentEnemy.state = ENEMY_DEAD;
          mario_state = MARIO_ENCOUNTER_RETURNING;
          mario_facing_right = true;
        }
        if (currentEnemy.animTimer > 0) currentEnemy.animTimer--;
        break;
      }

      if (currentEnemy.animTimer > 0) {
        currentEnemy.animTimer--;

        if (currentEnemy.state == ENEMY_HIT) {
          enemyFallVY += 0.5f * ts;
          enemyFallY += enemyFallVY;
        }
      } else {
        currentEnemy.type = ENEMY_NONE;
        currentEnemy.state = ENEMY_DEAD;
        mario_state = MARIO_ENCOUNTER_RETURNING;
        mario_facing_right = true;
      }

      if (currentEnemy.state == ENEMY_HIT && enemyFallY > 30) {
        currentEnemy.type = ENEMY_NONE;
        currentEnemy.state = ENEMY_DEAD;
        mario_state = MARIO_ENCOUNTER_RETURNING;
        mario_facing_right = true;
      }
      break;
    }

    case MARIO_ENCOUNTER_RETURNING: {
      // Star power: run faster and count down timer
      float returnSpeed = walkSpeed;
      if (marioStarPowered) {
        returnSpeed = walkSpeed * 2.0f;
        if (marioStarTimer > 0) marioStarTimer--;
        else marioStarPowered = false;
      }
      // Mushroom growth: stays big until off-screen (reset in cleanup below)

      mario_x += returnSpeed;
      mario_facing_right = true;
      mario_walk_frame = (mario_walk_frame + 1) % frameCount;

      if (mario_x > SCREEN_WIDTH + 15) {
        mario_x = MARIO_START_X;
        mario_state = MARIO_IDLE;
        currentEnemy.type = ENEMY_NONE;
        secondEnemyActive = false;
        marioStarPowered = false;
        marioStarTimer = 0;
        marioGrowthTimer = 0;
        lastEncounterEnd = millis();
        nextEncounterDelay = rollEncounterDelay();
      }
      break;
    }

    default:
      break;
  }
}

void updateMarioFireball() {
  if (!marioFireball.active) return;
  float ts = ENCOUNTER_TIME_SCALE;

  marioFireball.x += FIREBALL_SPEED * ts;
  marioFireball.vy += FIREBALL_GRAVITY * ts;
  marioFireball.y += marioFireball.vy;

  if (marioFireball.y >= mario_base_y - 4) {
    marioFireball.y = mario_base_y - 4;
    marioFireball.vy = FIREBALL_BOUNCE * ts;
  }

  if (currentEnemy.type != ENEMY_NONE &&
      currentEnemy.state == ENEMY_WALKING &&
      abs(marioFireball.x - currentEnemy.x) < 8) {
    currentEnemy.state = ENEMY_HIT;
    currentEnemy.animTimer = HIT_FRAMES;
    marioFireball.active = false;
    enemyFallY = 0;
    enemyFallVY = -3.0f * ENCOUNTER_TIME_SCALE;
  }

  if (marioFireball.x > SCREEN_WIDTH + 10) {
    marioFireball.active = false;
  }
}

// ========== Enemy Sprite Drawing ==========

void drawGoomba(int x, int y, int frame, bool squashing) {
  int sx = x - 5;
  int sy = y - 10;

  if (squashing) {
    // Flattened — wide and short (classic SMB1 squash)
    display.fillRect(sx - 1, y - 2, 12, 2, DISPLAY_WHITE);
    return;
  }

  // NES SMB1 Goomba: mushroom cap with angry eyebrows
  // Cap top (narrow)
  display.fillRect(sx + 2, sy, 6, 1, DISPLAY_WHITE);
  // Cap middle
  display.fillRect(sx + 1, sy + 1, 8, 1, DISPLAY_WHITE);
  // Cap brim (full width)
  display.fillRect(sx, sy + 2, 10, 2, DISPLAY_WHITE);

  // Face with angry V-shaped eyebrows (NES signature look)
  display.fillRect(sx + 1, sy + 4, 8, 3, DISPLAY_WHITE);
  // Angry brows: diagonal dark marks pointing inward-down
  display.drawPixel(sx + 1, sy + 4, DISPLAY_BLACK);  // Left brow outer-top
  display.drawPixel(sx + 2, sy + 5, DISPLAY_BLACK);  // Left brow inner-low
  display.drawPixel(sx + 8, sy + 4, DISPLAY_BLACK);  // Right brow outer-top
  display.drawPixel(sx + 7, sy + 5, DISPLAY_BLACK);  // Right brow inner-low
  // Eyes below brows
  display.drawPixel(sx + 3, sy + 5, DISPLAY_BLACK);  // Left eye
  display.drawPixel(sx + 6, sy + 5, DISPLAY_BLACK);  // Right eye

  // Narrow body
  display.fillRect(sx + 2, sy + 7, 6, 1, DISPLAY_WHITE);

  // Walking feet (alternating)
  if ((frame / 4) % 2 == 0) {
    display.fillRect(sx + 1, sy + 8, 3, 2, DISPLAY_WHITE);
    display.fillRect(sx + 6, sy + 8, 3, 2, DISPLAY_WHITE);
  } else {
    display.fillRect(sx + 2, sy + 8, 3, 2, DISPLAY_WHITE);
    display.fillRect(sx + 5, sy + 8, 3, 2, DISPLAY_WHITE);
  }
}

void drawSpiny(int x, int y, int frame, bool hit) {
  int sx = x - 5;
  int sy = y - 10;

  if (hit) {
    // Flipped upside-down (NES death: shell up, feet in air)
    // Feet pointing up
    display.fillRect(sx + 2, sy + 1, 2, 2, DISPLAY_WHITE);
    display.fillRect(sx + 6, sy + 1, 2, 2, DISPLAY_WHITE);
    // Body upside-down
    display.fillRect(sx + 1, sy + 3, 8, 4, DISPLAY_WHITE);
    // Spikes pointing down
    display.drawPixel(sx + 1, sy + 7, DISPLAY_WHITE);
    display.drawPixel(sx + 2, sy + 8, DISPLAY_WHITE);
    display.drawPixel(sx + 4, sy + 7, DISPLAY_WHITE);
    display.drawPixel(sx + 5, sy + 8, DISPLAY_WHITE);
    display.drawPixel(sx + 7, sy + 7, DISPLAY_WHITE);
    display.drawPixel(sx + 8, sy + 8, DISPLAY_WHITE);
    return;
  }

  // NES SMB1 Spiny: triangular spikes on dome shell (10px tall)
  // Triangular spikes (3 spikes, 2px tall each)
  display.drawPixel(sx + 1, sy + 1, DISPLAY_WHITE);
  display.drawPixel(sx + 2, sy, DISPLAY_WHITE);   // Spike 1 tip
  display.drawPixel(sx + 4, sy + 1, DISPLAY_WHITE);
  display.drawPixel(sx + 5, sy, DISPLAY_WHITE);   // Spike 2 tip
  display.drawPixel(sx + 7, sy + 1, DISPLAY_WHITE);
  display.drawPixel(sx + 8, sy, DISPLAY_WHITE);   // Spike 3 tip

  // Dome shell (wider at bottom)
  display.fillRect(sx + 1, sy + 2, 8, 2, DISPLAY_WHITE);
  display.fillRect(sx, sy + 4, 10, 2, DISPLAY_WHITE);

  // Eyes (two dark pixels in lower shell)
  display.drawPixel(sx + 2, sy + 4, DISPLAY_BLACK);
  display.drawPixel(sx + 4, sy + 4, DISPLAY_BLACK);

  // Lower body
  display.fillRect(sx + 1, sy + 6, 8, 2, DISPLAY_WHITE);

  // Walking feet (alternating, aligned with Goomba at sy+8-9)
  if ((frame / 4) % 2 == 0) {
    display.fillRect(sx + 1, sy + 8, 3, 2, DISPLAY_WHITE);
    display.fillRect(sx + 6, sy + 8, 3, 2, DISPLAY_WHITE);
  } else {
    display.fillRect(sx + 2, sy + 8, 3, 2, DISPLAY_WHITE);
    display.fillRect(sx + 5, sy + 8, 3, 2, DISPLAY_WHITE);
  }
}

void drawEnemy(MarioEnemy& e) {
  if (e.type == ENEMY_NONE || e.state == ENEMY_DEAD) return;
  if (e.x < -10 || e.x > SCREEN_WIDTH + 10) return;

  int y = mario_base_y;

  // Fireballed enemy: pop up then fall off bottom (classic Mario death)
  if (e.state == ENEMY_HIT) {
    y = mario_base_y + (int)enemyFallY;
    if (y > SCREEN_HEIGHT + 10) return;
  }

  if (e.type == ENEMY_GOOMBA) {
    drawGoomba((int)e.x, y, e.walkFrame, e.state == ENEMY_SQUASHING);
  } else if (e.type == ENEMY_KOOPA) {
    drawKoopa((int)e.x, y, e.walkFrame, e.state == ENEMY_SHELL_SLIDING);
  } else {
    drawSpiny((int)e.x, y, e.walkFrame, e.state == ENEMY_HIT);
  }
}

void drawMarioFireball(MarioFireball& fb) {
  if (!fb.active) return;
  int fx = (int)fb.x;
  int fy = (int)fb.y;
  // 4x4 diamond/circle shape
  display.fillRect(fx + 1, fy, 2, 1, DISPLAY_WHITE);
  display.fillRect(fx, fy + 1, 4, 2, DISPLAY_WHITE);
  display.fillRect(fx + 1, fy + 3, 2, 1, DISPLAY_WHITE);
}

// NES SMB1 Koopa Troopa (10px tall, turtle with shell)
void drawKoopa(int x, int y, int frame, bool shellOnly) {
  int sx = x - 5;
  int sy = y - 10;

  if (shellOnly) {
    // Shell only (after stomp) — compact rounded shell sliding
    display.fillRect(sx + 1, sy + 4, 8, 4, DISPLAY_WHITE);
    display.fillRect(sx + 2, sy + 3, 6, 1, DISPLAY_WHITE);
    display.fillRect(sx + 2, sy + 8, 6, 1, DISPLAY_WHITE);
    // Shell pattern (dark lines)
    display.drawPixel(sx + 4, sy + 5, DISPLAY_BLACK);
    display.drawPixel(sx + 5, sy + 5, DISPLAY_BLACK);
    display.drawPixel(sx + 4, sy + 6, DISPLAY_BLACK);
    display.drawPixel(sx + 5, sy + 6, DISPLAY_BLACK);
    return;
  }

  // Head (poking out from shell)
  display.fillRect(sx + 7, sy, 3, 3, DISPLAY_WHITE);
  display.drawPixel(sx + 8, sy + 1, DISPLAY_BLACK);  // Eye

  // Shell (dome shape)
  display.fillRect(sx + 2, sy + 2, 6, 2, DISPLAY_WHITE);
  display.fillRect(sx + 1, sy + 4, 8, 3, DISPLAY_WHITE);
  // Shell pattern
  display.drawPixel(sx + 3, sy + 4, DISPLAY_BLACK);
  display.drawPixel(sx + 4, sy + 5, DISPLAY_BLACK);
  display.drawPixel(sx + 6, sy + 4, DISPLAY_BLACK);

  // Lower body
  display.fillRect(sx + 2, sy + 7, 6, 1, DISPLAY_WHITE);

  // Walking feet (alternating)
  if ((frame / 4) % 2 == 0) {
    display.fillRect(sx + 1, sy + 8, 3, 2, DISPLAY_WHITE);
    display.fillRect(sx + 6, sy + 8, 3, 2, DISPLAY_WHITE);
  } else {
    display.fillRect(sx + 2, sy + 8, 3, 2, DISPLAY_WHITE);
    display.fillRect(sx + 5, sy + 8, 3, 2, DISPLAY_WHITE);
  }
}

// Star power-up sprite (7x7, spinning)
static void drawStarSprite(int x, int y, uint8_t frame) {
  int sx = x - 3;
  int sy = (int)y - 3;
  // Rotate between two star shapes for spin effect
  if ((frame / 4) % 2 == 0) {
    // Star shape 1: classic 5-point
    display.drawPixel(sx + 3, sy, DISPLAY_WHITE);      // Top point
    display.fillRect(sx + 1, sy + 1, 5, 1, DISPLAY_WHITE);
    display.fillRect(sx, sy + 2, 7, 2, DISPLAY_WHITE);
    display.fillRect(sx + 1, sy + 4, 5, 1, DISPLAY_WHITE);
    display.drawPixel(sx + 1, sy + 5, DISPLAY_WHITE);  // Bottom-left point
    display.drawPixel(sx + 5, sy + 5, DISPLAY_WHITE);  // Bottom-right point
    // Center hole
    display.drawPixel(sx + 3, sy + 2, DISPLAY_BLACK);
  } else {
    // Star shape 2: rotated slightly
    display.fillRect(sx + 2, sy, 3, 1, DISPLAY_WHITE);
    display.fillRect(sx, sy + 1, 7, 1, DISPLAY_WHITE);
    display.fillRect(sx + 1, sy + 2, 5, 2, DISPLAY_WHITE);
    display.fillRect(sx, sy + 4, 7, 1, DISPLAY_WHITE);
    display.fillRect(sx + 2, sy + 5, 3, 1, DISPLAY_WHITE);
    display.drawPixel(sx + 3, sy + 3, DISPLAY_BLACK);
  }
}

// Mushroom power-up sprite (8x10, classic SMB1 Super Mushroom)
static void drawMushroomSprite(int x, int y, uint8_t frame) {
  int sx = x - 4;
  int sy = y - 10;
  // Cap (dome shape)
  display.fillRect(sx + 2, sy, 4, 1, DISPLAY_WHITE);
  display.fillRect(sx + 1, sy + 1, 6, 1, DISPLAY_WHITE);
  display.fillRect(sx, sy + 2, 8, 3, DISPLAY_WHITE);
  // Cap spots (dark)
  display.drawPixel(sx + 3, sy + 2, DISPLAY_BLACK);
  display.drawPixel(sx + 4, sy + 2, DISPLAY_BLACK);
  display.drawPixel(sx + 3, sy + 3, DISPLAY_BLACK);
  display.drawPixel(sx + 4, sy + 3, DISPLAY_BLACK);
  // Eyes
  display.fillRect(sx + 1, sy + 5, 6, 2, DISPLAY_WHITE);
  display.drawPixel(sx + 2, sy + 5, DISPLAY_BLACK);
  display.drawPixel(sx + 4, sy + 5, DISPLAY_BLACK);
  // Stem
  display.fillRect(sx + 2, sy + 7, 4, 3, DISPLAY_WHITE);
}

// Big Mario sprite (used during star power and mushroom growth)
static void drawBigMario(int x, int y, bool facingRight, int frame) {
  if (x < -12 || x > SCREEN_WIDTH + 12) return;
  int sx = x - 5;
  int sy = y - 13;  // Taller sprite (13px instead of 10)

  // Hat (wider)
  display.fillRect(sx + 2, sy, 6, 2, DISPLAY_WHITE);
  if (facingRight) {
    display.drawPixel(sx + 8, sy + 1, DISPLAY_WHITE);
  } else {
    display.drawPixel(sx + 1, sy + 1, DISPLAY_WHITE);
  }

  // Head
  display.fillRect(sx + 2, sy + 2, 6, 3, DISPLAY_WHITE);

  // Body (wider)
  display.fillRect(sx + 1, sy + 5, 8, 3, DISPLAY_WHITE);

  // Arms
  if (facingRight) {
    display.drawPixel(sx, sy + 6, DISPLAY_WHITE);
    display.drawPixel(sx + 9, sy + 5 + (frame % 2), DISPLAY_WHITE);
  } else {
    display.drawPixel(sx + 9, sy + 6, DISPLAY_WHITE);
    display.drawPixel(sx, sy + 5 + (frame % 2), DISPLAY_WHITE);
  }

  // Legs (wider stance)
  if (frame % 2 == 0) {
    display.fillRect(sx + 1, sy + 8, 3, 4, DISPLAY_WHITE);
    display.fillRect(sx + 5, sy + 8, 3, 4, DISPLAY_WHITE);
  } else {
    display.fillRect(sx, sy + 8, 3, 4, DISPLAY_WHITE);
    display.fillRect(sx + 6, sy + 8, 3, 4, DISPLAY_WHITE);
  }
}
