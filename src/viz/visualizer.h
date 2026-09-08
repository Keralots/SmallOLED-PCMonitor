/*
 * SmallOLED-PCMonitor - Audio Spectrum Visualizer
 *
 * 32-band bar EQ and oscilloscope fed by the PC companion over UDP ("FFT1"
 * packets, ~25 Hz, same port as the stats JSON). Forced mode, not a clock
 * style. Packet format is shared with AnimatedPixelClock - one companion
 * feeds both, so it must not diverge.
 */

#ifndef VISUALIZER_H
#define VISUALIZER_H

#include <Arduino.h>

#define VIZ_BANDS 32
#define VIZ_PACKET_LEN 36  // "FFT1" magic + 32 amplitude bytes
#define VIZ_WAVE_POINTS 128
#define VIZ_WAVE_PACKET_LEN (VIZ_PACKET_LEN + VIZ_WAVE_POINTS)

// Returns true when the packet was a spectrum packet. The caller MUST return
// immediately: handleUDP() stamps lastReceived unconditionally, so falling
// through would pin metricData.online true for as long as music plays.
bool vizIngest(const uint8_t* buf, int len);

bool vizRecentEnough(unsigned long maxAgeMs);

// Opens a 10s grace window so "No audio data" shows before the first packet.
void vizNoteForced();
bool vizShouldDisplay();

// 128 samples centred on 128; null when the companion is too old to send one.
const uint8_t* vizWaveform();
uint32_t vizWaveSerial();

void displayVisualizer();

#endif // VISUALIZER_H
