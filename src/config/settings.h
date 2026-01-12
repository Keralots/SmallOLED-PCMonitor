/*
 * SmallOLED-PCMonitor - Settings Module Header
 *
 * Declarations for settings persistence functions.
 */

#ifndef SETTINGS_H
#define SETTINGS_H

#include "../config/config.h"
#include <Preferences.h>

// Initialize settings (load from NVS or set defaults)
void loadSettings();

// Save current settings to NVS
void saveSettings();

extern Preferences preferences;

#endif // SETTINGS_H
