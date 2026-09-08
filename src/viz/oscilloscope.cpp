/*
 * SmallOLED-PCMonitor - CRT-style oscilloscope (1-bit)
 *
 * Trace is trigger-aligned by the companion. No phosphor fade (a 1-bit ghost
 * is as bright as the live trace, so the echo is sparse points) and no
 * deflection gradient. Integer maths: no FPU, and this runs 128x per frame.
 */

#include "oscilloscope.h"
#include "visualizer.h"
#include "../config/config.h"
#include "../display/display.h"
#include <string.h>

// Rows reserved for the corner clock (must match visualizer.cpp).
#define VIZ_CLOCK_BAND 12

namespace {

uint8_t ghost[VIZ_WAVE_POINTS];      // the packet before last - the echo
uint8_t previousWave[VIZ_WAVE_POINTS];  // the last packet seen
bool ghostValid = false;
bool havePrevious = false;
uint32_t seenSerial = 0;
bool everSeen = false;

// Half-pixels: centre 31.5 / half-height 31.5 is exact and full deflection
// reaches rows 0 and 63. Integer 31/31 loses the bottom row.
struct Geometry {
  int centre2;  // 2 * centre row
  int half2;    // 2 * half height
};

Geometry geometry() {
  Geometry g;
  if (settings.vizShowClock) {
    g.centre2 = VIZ_CLOCK_BAND + SCREEN_HEIGHT - 1;
    g.half2 = SCREEN_HEIGHT - 1 - VIZ_CLOCK_BAND;
  } else {
    g.centre2 = SCREEN_HEIGHT - 1;
    g.half2 = SCREEN_HEIGHT - 1;
  }
  return g;
}

inline int sampleRow(uint8_t s, const Geometry& g, int gainPct) {
  int32_t dy2 = ((int32_t)s - 128) * g.half2 * gainPct / (128 * 100);
  if (dy2 > g.half2) dy2 = g.half2;
  if (dy2 < -g.half2) dy2 = -g.half2;
  return (g.centre2 - (int)dy2 + 1) / 2;
}

}  // namespace

void drawOscilloscope(const uint8_t* wave, uint32_t serial, bool reset) {
  if (reset || wave == nullptr) {
    ghostValid = false;
    havePrevious = false;
    everSeen = false;
    seenSerial = serial;
  }

  const Geometry g = geometry();
  const int centre = (g.centre2 + 1) / 2;
  const int top = (g.centre2 - g.half2 + 1) / 2;
  const int bottom = (g.centre2 + g.half2 + 1) / 2;

  if (settings.scopeGrid) {
    // Sparse: a dense graticule is the same white as the trace and swallows it.
    for (int x = 16; x < SCREEN_WIDTH; x += 16) {
      for (int y = top; y <= bottom; y += 4) display.drawPixel(x, y, DISPLAY_WHITE);
    }
    for (int x = 0; x < SCREEN_WIDTH; x += 4) {
      display.drawPixel(x, centre, DISPLAY_WHITE);
    }
  }

  if (wave == nullptr) {
    display.setTextSize(1);
    display.setTextColor(DISPLAY_WHITE);
    display.setCursor(4, centre - 12);
    display.print("Update PC companion");
    display.setCursor(4, centre + 4);
    display.print("for the waveform");
    return;
  }

  const int gainPct = settings.scopeGain;

  // Advances on the packet serial, not on rendered frames. The echo is the
  // packet before last, so promote previous -> ghost before storing current.
  if (serial != seenSerial || !everSeen) {
    if (havePrevious) {
      memcpy(ghost, previousWave, VIZ_WAVE_POINTS);
      ghostValid = true;
    }
    memcpy(previousWave, wave, VIZ_WAVE_POINTS);
    havePrevious = true;
    seenSerial = serial;
    everSeen = true;
  }

  if (settings.scopeTrail > 0 && ghostValid) {
    for (int i = 0; i < VIZ_WAVE_POINTS; i += 3) {
      display.drawPixel(i, sampleRow(ghost[i], g, gainPct), DISPLAY_WHITE);
    }
  }

  int prevY = sampleRow(wave[0], g, gainPct);
  if (settings.scopeFill) display.drawLine(0, centre, 0, prevY, DISPLAY_WHITE);
  else display.drawPixel(0, prevY, DISPLAY_WHITE);

  for (int i = 1; i < VIZ_WAVE_POINTS; i++) {
    int y = sampleRow(wave[i], g, gainPct);
    if (settings.scopeFill) display.drawLine(i, centre, i, y, DISPLAY_WHITE);
    display.drawLine(i - 1, prevY, i, y, DISPLAY_WHITE);
    prevY = y;
  }
}
