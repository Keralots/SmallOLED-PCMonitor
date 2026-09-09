/*
 * SmallOLED-PCMonitor - Audio Spectrum Visualizer (1-bit)
 *
 * 1-bit port of the AnimatedPixelClock module. Colour gradient becomes bar
 * segmentation, the peak line hides when it would merge with the bar, and
 * smoothing is time-scaled because the frame rate is bus-limited and varies
 * per panel.
 */

#include "visualizer.h"
#include "oscilloscope.h"

#include "../clocks/clocks.h"
#include "../config/config.h"
#include "../display/display.h"
#include <math.h>

#define VIZ_BAR_W 3            // lit pixels per bar (1px gap -> 32 * 4 = 128)
#define VIZ_SMOOTH_RATE 26.0f  // exponential pull toward the packet value, 1/s
#define VIZ_PEAK_GRAVITY 60.0f // px/s^2

// Rows reserved for the corner clock when it is enabled.
#define VIZ_CLOCK_BAND 12

static uint8_t vizBands[VIZ_BANDS];
static uint8_t vizWave[VIZ_WAVE_POINTS];
static bool vizWaveEver = false;
static uint32_t vizWaveserial = 0;
static unsigned long vizLastReceived = 0;
static bool vizEverReceived = false;
static unsigned long vizLastSignalAt = 0;

// Out of 255, after the companion's AGC. Silence sits at 0 with a little noise.
#define VIZ_SIGNAL_MIN 10

static float barH[VIZ_BANDS];
static float peakY[VIZ_BANDS];
static float peakVel[VIZ_BANDS];
static unsigned long lastVizFrame = 0;
static uint8_t lastStyle = 255;

static unsigned long vizForcedAt = 0;

// ========== Packet ingest ==========

bool vizIngest(const uint8_t* buf, int len) {
  if (len < VIZ_PACKET_LEN || memcmp(buf, "FFT1", 4) != 0) return false;
  memcpy(vizBands, buf + 4, VIZ_BANDS);
  if (len >= VIZ_WAVE_PACKET_LEN) {
    memcpy(vizWave, buf + VIZ_PACKET_LEN, VIZ_WAVE_POINTS);
    vizWaveEver = true;
    ++vizWaveserial;
  } else {
    vizWaveEver = false;  // legacy companion: drop any stale waveform
  }
  vizLastReceived = millis();
  vizEverReceived = true;
  for (int i = 0; i < VIZ_BANDS; i++) {
    if (vizBands[i] >= VIZ_SIGNAL_MIN) { vizLastSignalAt = vizLastReceived; break; }
  }
  return true;
}

const uint8_t* vizWaveform() { return vizWaveEver ? vizWave : nullptr; }
uint32_t vizWaveSerial() { return vizWaveserial; }

bool vizRecentEnough(unsigned long maxAgeMs) {
  return vizEverReceived && (millis() - vizLastReceived) <= maxAgeMs;
}

void vizNoteForced() { vizForcedAt = millis(); }

bool vizHasSignal(unsigned long maxAgeMs) {
  return vizLastSignalAt && (millis() - vizLastSignalAt) <= maxAgeMs;
}

// Gating on signal rather than clearing the caller's flag is deliberate: the
// companion only sends a mode command on a transition, so a display that
// dropped out of viz by itself would never be told to come back mid-track.
// Leaving the flag set means silence hides the visualizer and sound brings it
// straight back, with no round trip to the PC.
bool vizShouldDisplay() {
  if ((millis() - vizForcedAt) < 10000) return true;  // just asked for; show "No audio data"
  if (!vizRecentEnough(10000)) return false;
  unsigned long quiet = (unsigned long)settings.vizSilenceTimeout * 1000UL;
  if (!quiet) return true;
  return vizHasSignal(quiet);
}

// ========== Rendering ==========

static void drawVizClock() {
  struct tm timeinfo;
  if (!peekLocalTime(&timeinfo)) return;
  int displayHour, displayMin;
  bool isPM;
  formatTimeForDisplay(timeinfo.tm_hour, timeinfo.tm_min, displayHour,
                       displayMin, isPM);
  char timeStr[6];
  sprintf(timeStr, "%02d%c%02d", displayHour, shouldShowColon() ? ':' : ' ',
          displayMin);
  display.fillRect(SCREEN_WIDTH - 34, 0, 34, 10, DISPLAY_BLACK);
  display.setTextSize(1);
  display.setTextColor(DISPLAY_WHITE);
  display.setCursor(SCREEN_WIDTH - 31, 1);
  display.print(timeStr);
}

static void drawBar(int x, int h, int baseY) {
  if (h <= 0) return;
  switch (settings.vizBarStyle) {
    case 0:  // Solid
      display.fillRect(x, baseY - h + 1, VIZ_BAR_W, h, DISPLAY_WHITE);
      break;
    case 2:  // Outline
      if (h <= 2) {
        display.fillRect(x, baseY - h + 1, VIZ_BAR_W, h, DISPLAY_WHITE);
      } else {
        display.drawRect(x, baseY - h + 1, VIZ_BAR_W, h, DISPLAY_WHITE);
      }
      break;
    default:  // 1 = Segmented
      for (int y = 0; y < h; y++) {
        if (y % 3 == 2) continue;  // dark separator row
        display.drawFastHLine(x, baseY - y, VIZ_BAR_W, DISPLAY_WHITE);
      }
      break;
  }
}

void displayVisualizer() {
  unsigned long now = millis();

  // Style change or long gap (mode just switched on): restart the physics.
  bool reset = settings.vizStyle != lastStyle || now - lastVizFrame > 250;
  if (reset) {
    lastStyle = settings.vizStyle;
    for (int i = 0; i < VIZ_BANDS; i++) {
      barH[i] = peakY[i] = peakVel[i] = 0.0f;
    }
  }

  float dt = (now - lastVizFrame) / 1000.0f;
  if (dt > 0.1f) dt = 0.1f;
  if (dt < 0.0f) dt = 0.0f;
  lastVizFrame = now;

  bool stale = !vizRecentEnough(2000);

  if (settings.vizStyle == 1) {
    if (!stale) {
      drawOscilloscope(vizWaveform(), vizWaveserial, reset);
    }
  } else {
    const bool mirror = settings.vizStyle == 2;
    const int topLimit = settings.vizShowClock ? VIZ_CLOCK_BAND : 0;

    if (mirror) {
      const int horizon = topLimit + (SCREEN_HEIGHT - topLimit) / 2;
      const int maxH = SCREEN_HEIGHT - horizon - 1;
      for (int i = 0; i < VIZ_BANDS; i++) {
        float target = stale ? 0.0f : vizBands[i] * (maxH / 255.0f);
        barH[i] += (target - barH[i]) * (1.0f - expf(-VIZ_SMOOTH_RATE * dt));
        int h = (int)barH[i];
        int x = i * 4;
        if (h > 0) {
          drawBar(x, h, horizon - 1);
          for (int y = 0; y < h; y++) {
            if (settings.vizBarStyle == 1 && y % 3 == 2) continue;
            display.drawFastHLine(x, horizon + 1 + y, VIZ_BAR_W, DISPLAY_WHITE);
          }
        }
      }
      display.drawFastHLine(0, horizon, SCREEN_WIDTH, DISPLAY_WHITE);
    } else {
      const int baseY = SCREEN_HEIGHT - 1;
      const int maxH = SCREEN_HEIGHT - topLimit - 1;
      for (int i = 0; i < VIZ_BANDS; i++) {
        float target = stale ? 0.0f : vizBands[i] * (maxH / 255.0f);
        barH[i] += (target - barH[i]) * (1.0f - expf(-VIZ_SMOOTH_RATE * dt));

        if (barH[i] >= peakY[i]) {
          peakY[i] = barH[i];
          peakVel[i] = 0.0f;
        } else {
          peakVel[i] += VIZ_PEAK_GRAVITY * dt;
          peakY[i] -= peakVel[i] * dt;
          if (peakY[i] < 0.0f) peakY[i] = 0.0f;
        }

        int h = (int)barH[i];
        int x = i * 4;
        drawBar(x, h, baseY);

        // Same white as the bar, so it only reads as a peak with a dark
        // row between the two.
        if (settings.vizPeakDots) {
          int py = (int)peakY[i];
          if (py >= h + 2) {
            display.drawFastHLine(x, baseY - py, VIZ_BAR_W, DISPLAY_WHITE);
          }
        }
      }
    }
  }

  if (stale) {
    display.setTextSize(1);
    display.setTextColor(DISPLAY_WHITE);
    display.setCursor(16, 28);
    display.print("No audio data...");
  }

  if (settings.vizShowClock) {
    drawVizClock();
  }
}
