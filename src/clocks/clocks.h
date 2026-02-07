/*
 * SmallOLED-PCMonitor - Clock Display Module
 *
 * All clock style implementations (Mario, Standard, Space, Pong, Pac-Man).
 */

#ifndef CLOCKS_H
#define CLOCKS_H

#include "../config/config.h"

// ========== Common Clock Helpers ==========

// Get time with timeout (defined in main.cpp)
bool getTimeWithTimeout(struct tm* timeinfo, unsigned long timeout_ms = 100);

// Animation detection for adaptive refresh rate
bool isAnimationActive();

// Helper to determine if colon should be displayed (blinking mode)
bool shouldShowColon();

// Digit bounce animation shared by multiple clocks
void triggerDigitBounce(int digitIndex);
void updateDigitBounce();

// Calculate target digits for minute changes
void calculateTargetDigits(int current_hour, int current_min);

// ========== Standard Clock ==========
void displayStandardClock();

// ========== Large Clock ==========
void displayLargeClock();

// ========== Mario Clock ==========
void displayClockWithMario();
void updateMarioAnimation(struct tm* timeinfo);
void drawMario(int x, int y, bool facingRight, int frame, bool jumping);

// ========== Space Invaders Clock ==========
void displayClockWithSpaceInvader();
void updateSpaceAnimation(struct tm* timeinfo);
void handleSpacePatrolState();
void handleSpaceSlidingState();
void handleSpaceShootingState();
void handleSpaceExplodingState();
void handleSpaceMovingNextState();
void handleSpaceReturningState();
bool allSpaceFragmentsInactive();

// ========== Pong Clock ==========
void displayClockWithPong();
void initPongAnimation();
void resetPongAnimation();
void updatePongAnimation(struct tm* timeinfo);
void drawPongBall();
void drawPongDigits();
void drawBreakoutPaddle();
void drawPongFragments();
void updatePongBall(int ballIndex);
void checkPongCollisions(int ballIndex);
void updateBreakoutPaddle();
void updatePongFragments();
void updateAssemblyFragments();
void updateDigitTransitions();
void updateDigitBouncePong();
void spawnPongBall(int ballIndex);
void triggerDigitTransition(int digitIndex, char oldChar, char newChar);

// ========== Pac-Man Clock ==========
void displayClockWithPacman();
void updatePacmanAnimation(struct tm* timeinfo);
void updatePacmanPatrol();
void updatePacmanEating();
void startEatingDigit(uint8_t digitIndex, uint8_t digitValue);
void finishEatingDigit();
void generatePellets();
void updatePellets();
void drawPellets();
void drawPacman(int x, int y, int direction, int mouthFrame);
void updateSpecificDigit(uint8_t digitIndex, uint8_t newValue);

#endif // CLOCKS_H
