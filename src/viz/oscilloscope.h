/*
 * SmallOLED-PCMonitor - Oscilloscope (1-bit)
 *
 * serial changes once per packet and advances the ghost trace, so the echo
 * follows the audio rather than the (bus-limited, variable) frame rate.
 */

#ifndef OSCILLOSCOPE_H
#define OSCILLOSCOPE_H

#include <stdint.h>

void drawOscilloscope(const uint8_t* wave, uint32_t serial, bool reset);

#endif // OSCILLOSCOPE_H
