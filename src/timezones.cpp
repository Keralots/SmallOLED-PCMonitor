/*
 * timezones.cpp - Timezone database implementation
 * 
 * Comprehensive timezone database with POSIX TZ strings
 * for automatic DST transitions.
 */

#include "timezones.h"

// Timezone database
// Static array of supported timezone regions
static const TimezoneRegion timezoneDatabase[] = {
  // Europe
  {"Central European (Poland, Germany, France, Italy, Spain)", "CET-1CEST,M3.5.0/02:00,M10.5.0/03:00", 60},
  {"Eastern European (Finland, Greece, Romania, Bulgaria)", "EET-2EEST,M3.5.0/03:00,M10.5.0/04:00", 120},
  {"British (UK, Ireland)", "GMT0BST,M3.5.0/01:00,M10.5.0/02:00", 0},
  {"Western European (Portugal)", "WET0WEST,M3.5.0/01:00,M10.5.0/02:00", 0},
  {"Central Balkan (Hungary, Slovakia, Czechia)", "CET-1CEST,M3.5.0/02:00,M10.5.0/03:00", 60},
  {"Scandinavian (Sweden, Norway, Denmark)", "CET-1CEST,M3.5.0/02:00,M10.5.0/03:00", 60},
  {"Baltic (Lithuania, Latvia, Estonia)", "EET-2EEST,M3.5.0/03:00,M10.5.0/04:00", 120},
  {"Ukraine (Kyiv)", "EET-2EEST,M3.5.0/03:00,M10.5.0/04:00", 120},
  {"Russia (Moscow - no DST)", "MSK-3", 180},
  {"Belarus (Minsk - no DST)", "EET-2", 120},

  // Americas - North
  {"US Eastern (New York, Washington)", "EST5EDT,M3.2.0/02:00,M11.1.0/02:00", -300},
  {"US Central (Chicago, Texas)", "CST6CDT,M3.2.0/02:00,M11.1.0/02:00", -360},
  {"US Mountain (Denver, Arizona)", "MST7MDT,M3.2.0/02:00,M11.1.0/02:00", -420},
  {"US Pacific (Los Angeles, San Francisco)", "PST8PDT,M3.2.0/02:00,M11.1.0/02:00", -480},
  {"Alaska (Anchorage)", "AKST9AKDT,M3.2.0/02:00,M11.1.0/02:00", -540},
  {"Hawaii (Honolulu - no DST)", "HST10", -600},

  // Americas - Central & South
  {"Canada Eastern (Toronto, Montreal)", "EST5EDT,M3.2.0/02:00,M11.1.0/02:00", -300},
  {"Canada Central (Winnipeg)", "CST6CDT,M3.2.0/02:00,M11.1.0/02:00", -360},
  {"Canada Mountain (Edmonton)", "MST7MDT,M3.2.0/02:00,M11.1.0/02:00", -420},
  {"Canada Pacific (Vancouver)", "PST8PDT,M3.2.0/02:00,M11.1.0/02:00", -480},
  {"Mexico Central (Mexico City)", "CST6CDT,M4.1.0/02:00,M10.5.0/02:00", -360},
  {"Mexico Pacific (Tijuana)", "PST8PDT,M3.2.0/02:00,M11.1.0/02:00", -480},
  {"South America (Brazil, Argentina - no DST)", "BRT3", -180},
  {"Chile (Santiago)", "CLT4CLST,M9.2.0/00:00,M4.2.0/00:00", -240},
  {"Colombia (Bogota - no DST)", "COT5", -300},
  {"Peru (Lima - no DST)", "PET5", -300},

  // Asia-Pacific
  {"Australian Eastern (Sydney, Melbourne)", "AEST-10AEDT,M10.1.0/02:00,M4.1.0/03:00", 600},
  {"Australian Central (Adelaide)", "ACST-9:30ACDT,M10.1.0/02:00,M4.1.0/03:00", 570},
  {"Australian Western (Perth - no DST)", "AWST-8", 480},
  {"New Zealand (Auckland)", "NZST-12NZDT,M9.5.0/02:00,M4.1.0/03:00", 720},
  {"Japan (Tokyo - no DST)", "JST-9", 540},
  {"China (Shanghai, Beijing - no DST)", "CST-8", 480},
  {"Hong Kong (no DST)", "HKT-8", 480},
  {"Singapore (no DST)", "SGT-8", 480},
  {"South Korea (Seoul - no DST)", "KST-9", 540},
  {"Taiwan (Taipei - no DST)", "CST-8", 480},
  {"Philippines (Manila - no DST)", "PST-8", 480},
  {"Indonesia (Jakarta - no DST)", "WIB-7", 420},
  {"Thailand (Bangkok - no DST)", "ICT-7", 420},
  {"India (Mumbai, Delhi - no DST)", "IST-5:30", 330},

  // Middle East & Africa
  {"Israel (Jerusalem)", "IST-2IDT,M3.4.0/02:00,M10.2.0/02:00", 120},
  {"Turkey (Istanbul - no DST)", "EET-2", 120},
  {"UAE (Dubai - no DST)", "GST-4", 240},
  {"Saudi Arabia (Riyadh - no DST)", "AST-3", 180},
  {"Egypt (Cairo - no DST)", "EET-2", 120},
  {"South Africa (Johannesburg - no DST)", "SAST-2", 120},
  {"Nigeria (Lagos - no DST)", "WAT-1", 60},
  {"Kenya (Nairobi - no DST)", "EAT-3", 180},

  // Additional Zones
  {"Atlantic (Azores)", "AZOT1AZOST,M3.5.0/00:00,M10.5.0/01:00", 60},
  {"Cape Verde (no DST)", "CVT1", 60},
  {"Iceland (Reykjavik - no DST)", "GMT0", 0},
  {"Greenland)", "WGT3WGST,M3.5.0/22:00,M10.5.0/23:00", -180},

  // No DST zones
  {"UTC (Universal Coordinated Time)", "UTC0", 0},
};

// Total number of timezones in database
static const size_t TIMEZONE_COUNT = sizeof(timezoneDatabase) / sizeof(TimezoneRegion);

// Get POSIX timezone string for a region name
const char* getTimezoneString(const char* regionName) {
  if (regionName == nullptr || regionName[0] == '\0') {
    return nullptr;
  }

  for (size_t i = 0; i < TIMEZONE_COUNT; i++) {
    if (strcmp(regionName, timezoneDatabase[i].name) == 0) {
      return timezoneDatabase[i].posixString;
    }
  }

  return nullptr;
}

// Get default timezone for GMT offset (backward compatibility migration)
const char* getDefaultTimezoneForOffset(int gmtOffsetMinutes) {
  // Map common GMT offsets to default timezones
  // This is used for migrating old settings

  switch (gmtOffsetMinutes) {
    // Europe
    case 0:     return "GMT0BST,M3.5.0/01:00,M10.5.0/02:00";  // British (default for GMT+0)
    case 60:    return "CET-1CEST,M3.5.0/02:00,M10.5.0/03:00"; // Central European (default for GMT+1)
    case 120:   return "EET-2EEST,M3.5.0/03:00,M10.5.0/04:00"; // Eastern European (default for GMT+2)
    case 180:   return "MSK-3";                                  // Moscow (default for GMT+3)

    // Americas
    case -300:  return "EST5EDT,M3.2.0/02:00,M11.1.0/02:00";   // US Eastern (default for GMT-5)
    case -360:  return "CST6CDT,M3.2.0/02:00,M11.1.0/02:00";   // US Central (default for GMT-6)
    case -420:  return "MST7MDT,M3.2.0/02:00,M11.1.0/02:00";   // US Mountain (default for GMT-7)
    case -480:  return "PST8PDT,M3.2.0/02:00,M11.1.0/02:00";   // US Pacific (default for GMT-8)

    // Asia-Pacific
    case 540:   return "JST-9";                                  // Japan (default for GMT+9)
    case 480:   return "CST-8";                                  // China (default for GMT+8)
    case 600:   return "AEST-10AEDT,M10.1.0/02:00,M4.1.0/03:00"; // Australian Eastern (default for GMT+10)
    case 720:   return "NZST-12NZDT,M9.5.0/02:00,M4.1.0/03:00"; // New Zealand (default for GMT+12)
    case 330:   return "IST-5:30";                               // India (default for GMT+5:30)

    // Middle East
    case 240:   return "GST-4";                                  // UAE (default for GMT+4)

    default:
      // No automatic timezone for this offset
      return nullptr;
  }
}

// Get list of all supported timezones
const TimezoneRegion* getSupportedTimezones(size_t* count) {
  if (count != nullptr) {
    *count = TIMEZONE_COUNT;
  }
  return timezoneDatabase;
}

// Find timezone by POSIX string
const TimezoneRegion* findTimezoneByPosixString(const char* posixString) {
  if (posixString == nullptr || posixString[0] == '\0') {
    return nullptr;
  }

  for (size_t i = 0; i < TIMEZONE_COUNT; i++) {
    if (strcmp(posixString, timezoneDatabase[i].posixString) == 0) {
      return &timezoneDatabase[i];
    }
  }

  return nullptr;
}
