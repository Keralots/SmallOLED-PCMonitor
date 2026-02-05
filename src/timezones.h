/*
 * timezones.h - Timezone database with POSIX TZ string support
 * 
 * Provides automatic DST transitions using POSIX timezone strings
 * and configTzTime() function from ESP32 Arduino core.
 *
 * POSIX TZ format: STDoffsetDST[offset],start[/time],end[/time]
 * Example: "CET-1CEST,M3.5.0/02:00,M10.5.0/03:00"
 *   - CET-1: Central European Time, UTC+1 (sign is reversed in POSIX)
 *   - CEST: Central European Summer Time name
 *   - M3.5.0: March, last Sunday (5=last, 0=Sunday)
 *   - /02:00: Transition at 2:00 AM
 *   - M10.5.0/03:00: October, last Sunday, 3:00 AM
 */

#pragma once
#include <Arduino.h>

// Timezone region structure
struct TimezoneRegion {
  const char* name;           // Display name (e.g., "Central European (Poland, Germany)")
  const char* posixString;    // POSIX TZ string (e.g., "CET-1CEST,M3.5.0/02:00,M10.5.0/03:00")
  int gmtOffsetMinutes;       // Standard time offset from GMT in minutes (for sorting)
};

// Get POSIX timezone string for a region name
const char* getTimezoneString(const char* regionName);

// Get default timezone for GMT offset (backward compatibility migration)
const char* getDefaultTimezoneForOffset(int gmtOffsetMinutes);

// Get list of all supported timezones
const TimezoneRegion* getSupportedTimezones(size_t* count);

// Find timezone by POSIX string
const TimezoneRegion* findTimezoneByPosixString(const char* posixString);
