/*
 * SmallOLED-PCMonitor - Utility Functions
 */

#include "utils.h"
#include "../config/config.h"
#include "../config/settings.h"
#include <string.h>
#include <math.h>

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

  strncpy(dest, src, maxLen - 1);
  dest[maxLen - 1] = '\0';
  return true;  // Success, no truncation
}

// Bounds checking for settings (logs errors if out of range)
void assertBounds(int value, int minVal, int maxVal, const char* name) {
  if (value < minVal || value > maxVal) {
    Serial.printf("ERROR: %s out of bounds: %d not in [%d,%d]\n",
                  name ? name : "value", value, minVal, maxVal);
  }
}

#if TOUCH_BUTTON_ENABLED
// ========== Touch Button Implementation ==========

// Edges are captured in an interrupt with their own timestamps. Polling the
// pin from loop() lost taps whenever a frame or a clock reset held the loop
// longer than the touch lasted, and stretched or shrank press durations by
// however long the loop was busy.
#define TOUCH_EDGE_QUEUE 16
static volatile uint32_t edgeTime[TOUCH_EDGE_QUEUE];
static volatile uint8_t edgeLevel[TOUCH_EDGE_QUEUE];
static volatile uint8_t edgeHead = 0;
static volatile uint8_t edgeTail = 0;
static volatile uint8_t touchIsrPin = TOUCH_BUTTON_PIN;
static int8_t touchAttachedPin = -1;

static unsigned long buttonPressStartTime = 0;
static bool buttonIsPressed = false;
static bool buttonHandled = false;  // The hold already acted as an LED ramp
static bool mediumPressPending = false;
static unsigned long lastTapTime = 0;
// A pulse shorter than this is noise, not a finger.
static const unsigned long TOUCH_MIN_PRESS_MS = 15;

static const unsigned long MEDIUM_PRESS_THRESHOLD = 500;
static const unsigned long LONG_PRESS_THRESHOLD = 1000;

// The touch pin is configurable at runtime because the GPIO the TTP223 lands
// on is a board decision, not a firmware one - the reference wiring uses GPIO 7
// but the sponsored carrier PCB routes it to GPIO 21. TOUCH_BUTTON_PIN stays
// the default for a device that has never been configured.
bool isValidTouchPin(int pin) {
  if (pin < 0 || pin > TOUCH_PIN_MAX) return false;
#if CONFIG_IDF_TARGET_ESP32S3
  if (pin >= 22 && pin <= 25) return false; // not bonded out
  if (pin >= 26 && pin <= 37) return false; // SPI flash/PSRAM (33-37 on octal modules)
#if ARDUINO_USB_CDC_ON_BOOT
  if (pin == 19 || pin == 20) return false; // USB D-/D+ carries Serial and OTA-less reflashing
#endif
#else
  if (pin >= 11 && pin <= 17) return false; // SPI flash bus + VDD_SPI
#if ARDUINO_USB_CDC_ON_BOOT
  if (pin == 18 || pin == 19) return false; // USB D-/D+ carries Serial and OTA-less reflashing
#endif
#endif
#if DISPLAY_INTERFACE == 1
  if (pin == SPI_MOSI_PIN || pin == SPI_SCK_PIN || pin == SPI_CS_PIN ||
      pin == SPI_DC_PIN) return false;
  if (SPI_RST_PIN >= 0 && pin == SPI_RST_PIN) return false;
#else
  if (pin == I2C_SDA_PIN || pin == I2C_SCL_PIN) return false;
#endif
#if LED_PWM_ENABLED
  if (pin == LED_PWM_PIN) return false;
#endif
  return true;
}

const char* touchPinNote(int pin) {
  switch (pin) {
#if CONFIG_IDF_TARGET_ESP32S3
    case 0: case 3: case 45: case 46: return "strapping";
    case 19: case 20: return "USB";
    case 43: return "UART0 TX";
    case 44: return "UART0 RX";
#else
    case 2: case 8: case 9: return "strapping";
    case 18: case 19: return "USB";
    case 20: return "UART0 RX";
    case 21: return "UART0 TX";
#endif
    default: return "";
  }
}

void applyTouchButtonPin(uint8_t pin) {
  if (!isValidTouchPin(pin)) return;
  if (pin != settings.touchButtonPin) {
    pinMode(settings.touchButtonPin, INPUT); // drop the pulldown on the old pin
    settings.touchButtonPin = pin;
  }
  initTouchButton();
}

static void IRAM_ATTR touchEdgeIsr() {
  uint8_t next = (edgeHead + 1) % TOUCH_EDGE_QUEUE;
  if (next == edgeTail) return;
  edgeTime[edgeHead] = millis();
  edgeLevel[edgeHead] = digitalRead(touchIsrPin);
  edgeHead = next;
}

void resetTouchButtonState();

void initTouchButton() {
  if (!isValidTouchPin(settings.touchButtonPin)) {
    settings.touchButtonPin = TOUCH_BUTTON_PIN;
  }
  if (touchAttachedPin >= 0) detachInterrupt(touchAttachedPin);
  pinMode(settings.touchButtonPin, INPUT_PULLDOWN);
  touchIsrPin = settings.touchButtonPin;
  resetTouchButtonState();
  attachInterrupt(settings.touchButtonPin, touchEdgeIsr, CHANGE);
  touchAttachedPin = settings.touchButtonPin;

  Serial.print("Touch button initialized on GPIO ");
  Serial.print(settings.touchButtonPin);
  Serial.print(" (active ");
  Serial.print(TOUCH_ACTIVE_LEVEL == HIGH ? "HIGH" : "LOW");
  Serial.println(")");
}

// Feed one edge to the press state machine. Returns true when it completes a
// short tap.
static bool touchEdge(bool active, unsigned long t) {
  if (active) {
    if (!buttonIsPressed) {
      buttonIsPressed = true;
      buttonPressStartTime = t;
      buttonHandled = false;
    }
    return false;
  }
  if (!buttonIsPressed) return false;
  buttonIsPressed = false;
  unsigned long duration = t - buttonPressStartTime;
  bool handled = buttonHandled;
  buttonHandled = false;
  if (handled || duration < TOUCH_MIN_PRESS_MS) return false;
#if LED_PWM_ENABLED
  // Quick tap < 500 ms, medium press 500-1000 ms toggles the LED
  if (duration >= MEDIUM_PRESS_THRESHOLD) {
    if (duration < LONG_PRESS_THRESHOLD) mediumPressPending = true;
    return false;
  }
#endif
  lastTapTime = t;
  return true;
}

// Returns true once per completed tap; call until it returns false.
// `when` receives the release time of that tap.
bool checkTouchButtonPressed(unsigned long *when) {
  while (edgeTail != edgeHead) {
    uint8_t i = edgeTail;
    bool active = edgeLevel[i] == TOUCH_ACTIVE_LEVEL;
    unsigned long t = edgeTime[i];
    edgeTail = (i + 1) % TOUCH_EDGE_QUEUE;
    if (touchEdge(active, t)) {
      if (when) *when = lastTapTime;
      return true;
    }
  }
  // Resync if an edge was lost (queue overflow, pin rebind mid-touch).
  bool active = digitalRead(settings.touchButtonPin) == TOUCH_ACTIVE_LEVEL;
  if (edgeTail == edgeHead && active != buttonIsPressed && touchEdge(active, millis())) {
    if (when) *when = lastTapTime;
    return true;
  }
  return false;
}

void resetTouchButtonState() {
  edgeTail = edgeHead;
  buttonIsPressed = false;
  buttonHandled = false;
  mediumPressPending = false;
}

#endif

#if LED_PWM_ENABLED
// ========== LED PWM Night Light Control ==========

void initLEDPWM() {
  ledcSetup(LED_PWM_CHANNEL, LED_PWM_FREQ, LED_PWM_RESOLUTION);
  ledcAttachPin(LED_PWM_PIN, LED_PWM_CHANNEL);
  Serial.print("LED PWM initialized on GPIO ");
  Serial.println(LED_PWM_PIN);
}

void setLEDBrightness(uint8_t brightness) {
  if (settings.ledEnabled) {
    ledcWrite(LED_PWM_CHANNEL, brightness);
  } else {
    ledcWrite(LED_PWM_CHANNEL, 0);  // Off if disabled
  }
}

void enableLED(bool enable) {
  settings.ledEnabled = enable;
  if (enable) {
    if (settings.ledBrightness == 0) {
      settings.ledBrightness = 128;  // Restore to 50% if dimmed to zero
    }
    setLEDBrightness(settings.ledBrightness);
    Serial.print("LED enabled, brightness: ");
    Serial.println(settings.ledBrightness);
  } else {
    ledcWrite(LED_PWM_CHANNEL, 0);
    Serial.println("LED disabled");
  }
}

// ========== LED Gesture Control ==========
// Quick tap (< 500ms): clock action (handled by checkTouchButtonPressed)
// Medium press (500ms-1s, release): toggle LED on/off
// Long hold (> 1s, keep holding): ramp brightness, direction alternates per hold
// Gamma-corrected ramp for natural feel.

// Gamma correction: maps linear position (0-255) to perceived brightness
// Quadratic approximation of gamma ~2.0 — no floats in hot path
// Capped at 254 to avoid PWM→DC transition jump at 100% duty cycle
static uint8_t gammaCorrect(uint8_t pos) {
  uint8_t val = (uint16_t(pos) * pos + pos) >> 8;
  return val >= 255 ? 254 : val;
}

static const unsigned long LED_RAMP_INTERVAL_MS = 10; // 10ms per step → ~2.5s full range

void handleTouchLED() {
  static bool rampActive = false;
  static bool rampUp = true;
  static uint8_t rampPosition = 0;
  static unsigned long lastRampUpdate = 0;

  bool held = buttonIsPressed && !buttonHandled;
  unsigned long pressDuration = millis() - buttonPressStartTime;

  // Detect long press threshold crossing → start ramp
  if (held && pressDuration >= LONG_PRESS_THRESHOLD) {
    buttonHandled = true; // Block short press

    if (!rampActive) {
      // First frame: determine direction. Each hold reverses the previous
      // one, so overshooting is fixed by letting go and holding again. The
      // ends of the range force the only direction that does anything.
      if (!settings.ledEnabled || settings.ledBrightness == 0) {
        rampUp = true;
        rampPosition = 0;
        settings.ledEnabled = true;
      } else {
        rampUp = settings.ledBrightness >= 254 ? false : !rampUp;
        // Inverse gamma to find ramp position from current brightness
        rampPosition = (uint8_t)sqrtf(float(settings.ledBrightness) * 255.0f);
      }
      rampActive = true;
      lastRampUpdate = millis();
    }
  }

  // Continuous ramp while held
  if (rampActive && buttonIsPressed) {
    if (millis() - lastRampUpdate >= LED_RAMP_INTERVAL_MS) {
      lastRampUpdate = millis();
      if (rampUp) {
        if (rampPosition < 255) rampPosition++;
      } else {
        if (rampPosition > 0) rampPosition--;
      }

      settings.ledBrightness = gammaCorrect(rampPosition);
      if (settings.ledBrightness == 0 && !rampUp) {
        settings.ledEnabled = false;
        ledcWrite(LED_PWM_CHANNEL, 0);
      } else {
        ledcWrite(LED_PWM_CHANNEL, settings.ledBrightness);
      }
    }
    return;
  }

  if (rampActive) {
    // Released after a ramp: keep the level
    rampActive = false;
    saveLedState();
  }

  if (mediumPressPending) {
    mediumPressPending = false;
    enableLED(!settings.ledEnabled);
    saveLedState();
    Serial.println(settings.ledEnabled ? "Medium press: LED ON" : "Medium press: LED OFF");
  }
}

#endif // TOUCH_BUTTON_ENABLED
