/*
 * SmallOLED-PCMonitor - Metrics Display Module
 *
 * Functions for displaying PC stats metrics on the OLED display.
 */

#ifndef METRICS_H
#define METRICS_H

#include "../config/config.h"

// ========== Metrics Display Functions ==========

// Main display function - renders metrics grid
void displayStats();

// Compact grid layout with position-based rendering
void displayStatsCompactGrid();

// Helper to display a single metric
void displayMetricCompact(Metric* m);

// Draw progress bar for a metric
void drawProgressBar(int x, int y, int width, Metric* m);

#endif // METRICS_H
