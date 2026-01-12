/*
 * SmallOLED-PCMonitor - Display Module
 *
 * Display initialization and global display object.
 * Supports both SSD1306 and SH1106 displays via compile-time selection.
 */

#include "display.h"

// Initialize display - returns true on success
bool initDisplay() {
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);

  for (int attempt = 0; attempt < 3; attempt++) {
#if DISPLAY_TYPE == 1
    // SH1106: Try 0x3C first (most common), then 0x3D
    byte addrToTry = (attempt == 0) ? DISPLAY_I2C_ADDRESS : 0x3D;
    display.begin(addrToTry);
    display.setContrast(255);
    return true;
#else
    if (display.begin(SSD1306_SWITCHCAPVCC, DISPLAY_I2C_ADDRESS)) {
      return true;
    }
#endif
    delay(500);
  }

  return false;
}
