/*
 * SmallOLED-PCMonitor - BLE Gamepad (Xbox Wireless Controller)
 *
 * BLE central that scans for, bonds with and reads an Xbox controller
 * (model 1708 or 1914+ on firmware 5.x). The ESP32-C3 has no Bluetooth
 * Classic, so older pads that only speak BR/EDR cannot work.
 *
 * All BLE work runs in its own FreeRTOS task, so connecting and pairing
 * never stall the render loop. Input state is read lock-protected.
 */

#ifndef GAMEPAD_H
#define GAMEPAD_H

#include <Arduino.h>

enum GamepadButton : uint16_t {
  GP_A = 1 << 0,
  GP_B = 1 << 1,
  GP_X = 1 << 2,
  GP_Y = 1 << 3,
  GP_LB = 1 << 4,
  GP_RB = 1 << 5,
  GP_VIEW = 1 << 6,
  GP_MENU = 1 << 7,
  GP_XBOX = 1 << 8,
  GP_LS = 1 << 9,
  GP_RS = 1 << 10,
  GP_SHARE = 1 << 11,
  GP_UP = 1 << 12,
  GP_DOWN = 1 << 13,
  GP_LEFT = 1 << 14,
  GP_RIGHT = 1 << 15,
};

enum GamepadLink : uint8_t {
  GP_LINK_OFF,         // BLE idle
  GP_LINK_SCANNING,    // looking for a controller
  GP_LINK_CONNECTING,  // found one, connecting / pairing
  GP_LINK_CONNECTED,   // reports flowing
};

struct GamepadState {
  uint16_t buttons;   // held right now (GamepadButton bits; d-pad and left stick folded in)
  uint16_t pressed;   // went down since the previous gamepadRead()
  int16_t lx, ly;     // sticks, -32768..32767, +y = down
  int16_t rx, ry;
  uint16_t lt, rt;    // triggers, 0..1023
  uint8_t hat;        // raw d-pad: 0 = none, 1 = up, clockwise to 8 = up-left
};

inline bool gamepadHatUp(uint8_t hat) { return hat == 8 || hat == 1 || hat == 2; }

void gamepadStart();
void gamepadStop();
GamepadLink gamepadLink();
// Snapshot of the inputs; clears the `pressed` latch.
void gamepadRead(GamepadState *out);
uint8_t gamepadBattery();
// Magnitudes 0-100; dropped unless a pad is connected.
void gamepadRumble(uint8_t strong, uint8_t weak, uint16_t ms);
// Forget every paired pad; the next one has to be paired with its pair button.
void gamepadForget();
bool gamepadHasBond();

#endif // GAMEPAD_H
