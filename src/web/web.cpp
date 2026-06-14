/*
 * SmallOLED-PCMonitor - Web Server Module
 *
 * Web server handlers for configuration interface.
 * Extracted from PCMonitor_WifiPortal.cpp
 */

#include "web.h"
#include "../config/config.h"
#include "../config/settings.h"
#include "../network/network.h"
#include "../utils/utils.h"
#include "../clocks/clocks.h"
#include "../display/display.h"
#include "../timezones.h"
#include "web_pages.h"
#include <WebServer.h>
#include <Update.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <esp_ota_ops.h>

// True when the device is still on the original 4 MB partition table, whose OTA
// app slot is 0x140000 (1,310,720 B). Repartitioned devices have a larger slot
// (e.g. 0x1E0000), so the firmware-too-big-for-OTA warning only applies here.
static bool isLegacyOtaPartition() {
  const esp_partition_t* running = esp_ota_get_running_partition();
  return running && running->size <= 0x140000; // <= 1,310,720 B
}

// ========== Web Server Object ==========
WebServer server(80);

// Runtime mode override flags (defined in main.cpp)
extern bool httpForceClock;
#if TOUCH_BUTTON_ENABLED
extern bool manualClockMode;
#endif

// ========== Web Server Setup ==========
void setupWebServer() {
 server.on("/", handleRoot);
 server.on("/portal.css", HTTP_GET, handlePortalCss);
 server.on("/portal.js", HTTP_GET, handlePortalJs);
 server.on("/save", HTTP_POST, handleSave);
 server.on("/reset", handleReset);
 server.on("/metrics", handleMetricsAPI);
 server.on("/api/info", HTTP_GET, handleDeviceInfo);
 server.on("/api/export", HTTP_GET, handleExportConfig);
 server.on("/api/import", HTTP_POST, handleImportConfig);
 server.on("/api/rename", HTTP_POST, handleRename);

 // Runtime control API (display power, mode, brightness, clock style, reboot)
 server.on("/api/status", HTTP_GET, handleStatus);
 server.on("/api/display/on", HTTP_GET, handleDisplayOn);
 server.on("/api/display/off", HTTP_GET, handleDisplayOff);
 server.on("/api/display/brightness", HTTP_GET, handleSetBrightness);
 server.on("/api/mode/clock", HTTP_GET, handleModeClock);
 server.on("/api/mode/auto", HTTP_GET, handleModeAuto);
 server.on("/api/clock/style", HTTP_GET, handleSetClockStyle);
 server.on("/api/reboot", HTTP_GET, handleReboot);

 // OTA Firmware Update handlers
 server.on("/update", HTTP_POST, []() {
 if (Update.hasError()) {
   // Surface the real reason (non-200 so the UI knows it failed). The common
   // case once the firmware outgrows an older default partition table is a
   // space error - point the user at the browser flasher, which writes the
   // partition table (full flash) and so repartitions to the larger slots.
   String msg = String("Update failed: ") + Update.errorString() +
                ". If this is a size/space error the firmware no longer fits this "
                "device's OTA partition - re-flash once via the browser flasher to repartition.";
   server.send(500, "text/plain", msg);
   return; // keep running the current firmware
 }
 server.send(200, "text/plain", "OK");
 delay(1000);
 ESP.restart();
 }, []() {
 HTTPUpload& upload = server.upload();
 if (upload.status == UPLOAD_FILE_START) {
 Serial.printf("Update: %s\n", upload.filename.c_str());
 if (!Update.begin(UPDATE_SIZE_UNKNOWN)) { // Start with max available size
 Update.printError(Serial);
 }
 } else if (upload.status == UPLOAD_FILE_WRITE) {
 // Write uploaded data
 if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
 Update.printError(Serial);
 }
 } else if (upload.status == UPLOAD_FILE_END) {
 if (Update.end(true)) { // true = set size to current progress
 Serial.printf("Update Success: %u bytes\nRebooting...\n", upload.totalSize);
 } else {
 Update.printError(Serial);
 }
 }
 });

 server.begin();
}

// API endpoint to return current metrics as JSON (uses ArduinoJson for proper string escaping)
void handleMetricsAPI() {
 JsonDocument doc;
 JsonArray metricsArray = doc["metrics"].to<JsonArray>();

 for (int i = 0; i < metricData.count; i++) {
   Metric& m = metricData.metrics[i];
   JsonObject obj = metricsArray.add<JsonObject>();
   obj["id"] = m.id;
   obj["name"] = m.name;
   obj["label"] = m.label;
   obj["unit"] = m.unit;
   obj["value"] = m.value;           // live value (for the 1:1 preview)
   obj["displayOrder"] = m.displayOrder;
   obj["companionId"] = m.companionId;
   obj["position"] = m.position;
   obj["barPosition"] = m.barPosition;
   obj["barMin"] = m.barMin;
   obj["barMax"] = m.barMax;
   obj["barWidth"] = m.barWidth;
   obj["barOffsetX"] = m.barOffsetX;
 }

 // Device-side context for the live preview: connection state + current time.
 doc["online"] = metricData.online;
 struct tm ti;
 char ts[6] = "--:--";
 if (getLocalTime(&ti, 0)) strftime(ts, sizeof(ts), "%H:%M", &ti);
 doc["time"] = ts;

 String json;
 serializeJson(doc, json);
 server.sendHeader("Access-Control-Allow-Origin", "*");
 server.send(200, "application/json", json);
}

// API endpoint to return device info for app discovery
void handleDeviceInfo() {
 JsonDocument doc;
 doc["version"] = FIRMWARE_VERSION;
 doc["mac"] = WiFi.macAddress();
 doc["ip"] = WiFi.localIP().toString();
 doc["hostname"] = String(settings.deviceName) + ".local";
 doc["deviceName"] = settings.deviceName;
 doc["displayType"] = settings.displayType;
 doc["rssi"] = WiFi.RSSI();
 doc["uptime"] = millis() / 1000;
 doc["freeHeap"] = ESP.getFreeHeap();
 doc["model"] = "SmallOLED";

 String json;
 serializeJson(doc, json);
 server.sendHeader("Access-Control-Allow-Origin", "*");
 server.send(200, "application/json", json);
}

// ========== Runtime Control API ==========
// All endpoints are GET (automation-friendly), unauthenticated, and runtime-only
// (no flash writes) so frequent toggling from automations doesn't wear the NVS.

// GET /api/status - report live display/mode state as JSON
void handleStatus() {
 JsonDocument doc;

 bool pcOnline = metricData.online;
#if TOUCH_BUTTON_ENABLED
 bool showStats = pcOnline && !manualClockMode && !httpForceClock;
#else
 bool showStats = pcOnline && !httpForceClock;
#endif

 doc["displayOn"] = !isDisplayForcedOff() && settings.displayBrightness > 0;
 doc["forcedOff"] = isDisplayForcedOff();
 doc["mode"] = showStats ? "metrics" : "clock";
 doc["forcedClock"] = httpForceClock;
 doc["brightness"] = (settings.displayBrightness * 100) / 255; // percent
 doc["clockStyle"] = settings.clockStyle;
 doc["pcOnline"] = pcOnline;

 String json;
 serializeJson(doc, json);
 server.sendHeader("Access-Control-Allow-Origin", "*");
 server.send(200, "application/json", json);
}

// GET /api/display/on - turn the panel back on (restore normal/scheduled brightness)
void handleDisplayOn() {
 setDisplayForcedOff(false);
 server.sendHeader("Access-Control-Allow-Origin", "*");
 server.send(200, "application/json", "{\"success\":true,\"displayOn\":true}");
}

// GET /api/display/off - hold the panel off (suppresses scheduled brightness re-applies)
void handleDisplayOff() {
 setDisplayForcedOff(true);
 server.sendHeader("Access-Control-Allow-Origin", "*");
 server.send(200, "application/json", "{\"success\":true,\"displayOn\":false}");
}

// GET /api/display/brightness?value=0-100 - set brightness percentage
void handleSetBrightness() {
 server.sendHeader("Access-Control-Allow-Origin", "*");
 if (!server.hasArg("value")) {
   server.send(400, "application/json", "{\"error\":\"Missing value (0-100)\"}");
   return;
 }
 int value = server.arg("value").toInt();
 if (value < 0) value = 0;
 if (value > 100) value = 100;
 setDisplayBrightnessPercent((uint8_t)value);
 server.send(200, "application/json",
             "{\"success\":true,\"brightness\":" + String(value) + "}");
}

// GET /api/mode/clock - force clock display even when the PC is online
void handleModeClock() {
 httpForceClock = true;
 server.sendHeader("Access-Control-Allow-Origin", "*");
 server.send(200, "application/json", "{\"success\":true,\"mode\":\"clock\"}");
}

// GET /api/mode/auto - resume automatic mode (metrics when PC online, clock otherwise)
void handleModeAuto() {
 httpForceClock = false;
 server.sendHeader("Access-Control-Allow-Origin", "*");
 server.send(200, "application/json", "{\"success\":true,\"mode\":\"auto\"}");
}

// GET /api/clock/style?id=0-11 - switch the active clock animation
void handleSetClockStyle() {
 server.sendHeader("Access-Control-Allow-Origin", "*");
 if (!server.hasArg("id")) {
   server.send(400, "application/json", "{\"error\":\"Missing id (0-11)\"}");
   return;
 }
 int id = server.arg("id").toInt();
 if (id < 0 || id > 11) {
   server.send(400, "application/json", "{\"error\":\"id must be 0-11\"}");
   return;
 }
 settings.clockStyle = (uint8_t)id;
 resetClockAnimationState();
 server.send(200, "application/json",
             "{\"success\":true,\"clockStyle\":" + String(id) + "}");
}

// GET /api/reboot - soft restart (non-destructive, unlike /reset which wipes config)
void handleReboot() {
 server.sendHeader("Access-Control-Allow-Origin", "*");
 server.send(200, "application/json", "{\"success\":true,\"message\":\"Rebooting\"}");
 delay(500);
 ESP.restart();
}

void handleRename() {
 server.sendHeader("Access-Control-Allow-Origin", "*");

 if (!server.hasArg("plain")) {
   server.send(400, "application/json", "{\"error\":\"Missing body\"}");
   return;
 }

 JsonDocument doc;
 DeserializationError error = deserializeJson(doc, server.arg("plain"));
 if (error) {
   server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
   return;
 }

 const char* name = doc["name"];
 if (!name || strlen(name) == 0 || strlen(name) > 31) {
   server.send(400, "application/json", "{\"error\":\"Name must be 1-31 characters\"}");
   return;
 }

 // Validate: letters, numbers, hyphens only, must start with letter
 bool valid = isalpha(name[0]);
 for (unsigned int i = 0; valid && i < strlen(name); i++) {
   if (!isalnum(name[i]) && name[i] != '-') valid = false;
 }
 if (!valid) {
   server.send(400, "application/json", "{\"error\":\"Invalid name. Use letters, numbers, hyphens. Must start with a letter.\"}");
   return;
 }

 safeCopyString(settings.deviceName, name, sizeof(settings.deviceName));
 saveSettings();
 initMDNS();

 server.send(200, "application/json", "{\"success\":true,\"name\":\"" + String(settings.deviceName) + "\"}");
}

// ========== Config Page (streamed PROGMEM template) ==========
// The full HTML config page lives in web_pages.h as PAGE_HTML[] (flash).
// resolvePlaceholder() supplies the dynamic values for each %TOKEN%, and
// streamTemplate() walks the template emitting it through a single small
// buffer. Peak heap during a page render is ~2 KB plus the largest single
// placeholder value, instead of the whole ~58 KB page as one String.

// Resolve a single %NAME% placeholder. Returns false for unknown names so the
// streamer leaves the literal text untouched.
static bool resolvePlaceholder(const char* n, String& out) {
  // --- Header / identity ---
  if (!strcmp(n, "VER")) { out = String(FIRMWARE_VERSION); return true; }
  if (!strcmp(n, "IP")) { out = WiFi.localIP().toString(); return true; }
  if (!strcmp(n, "BUILT")) { out = String(__DATE__); return true; }
  if (!strcmp(n, "ASSETVER")) {
    String s = String(__DATE__) + __TIME__;
    s.replace(" ", ""); s.replace(":", ""); // alnum only -> safe in a query string
    out = s; return true;
  }
  if (!strcmp(n, "HEAP")) { out = String(ESP.getFreeHeap() / 1024.0, 1); return true; }
  if (!strcmp(n, "DISPLAYMODEL")) {
    out = (settings.displayType == 2) ? "CH1116" : (settings.displayType == 1) ? "SH1106" : "SSD1306";
    return true;
  }
  // Hides the OTA partition-limit warning on already-repartitioned devices.
  if (!strcmp(n, "OTAWARNSTYLE")) {
    out = isLegacyOtaPartition() ? "" : "display:none";
    return true;
  }

  // --- Brightness help text and minimum (depend on touch-button presence) ---
  if (!strcmp(n, "MINBRIGHT")) { out = String(isZeroBrightnessAllowed() ? 0 : 1); return true; }
  if (!strcmp(n, "HELP_DISPBRIGHT")) {
    out = isZeroBrightnessAllowed()
        ? "Brightness control (0-100%). Set to 0% to turn the OLED off, then tap the touch button to wake it for 10 seconds."
        : "Brightness control (0-100%). This build has no touch button, so the minimum brightness is 1%.";
    return true;
  }
  if (!strcmp(n, "HELP_DIMBRIGHT")) {
    out = isZeroBrightnessAllowed()
        ? "Brightness level during scheduled dim period. Set to 0% for a fully dark screen, then tap the touch button to wake it for 10 seconds."
        : "Brightness level during scheduled dim period. This build has no touch button, so the minimum brightness is 1%.";
    return true;
  }

  // --- Scheduled-dimming hour dropdowns ---
  if (!strcmp(n, "OPT_DIMSTART") || !strcmp(n, "OPT_DIMEND")) {
    uint8_t selHour = (!strcmp(n, "OPT_DIMSTART")) ? settings.dimStartHour : settings.dimEndHour;
    for (int i = 0; i < 24; i++) {
      out += "<option value=\"" + String(i) + "\"" + (selHour == i ? " selected" : "") + ">" + String(i) + ":00</option>";
    }
    return true;
  }

  // --- Timezone region dropdown ---
  if (!strcmp(n, "OPT_TZ")) {
    size_t tzCount;
    const TimezoneRegion* regions = getSupportedTimezones(&tzCount);
    out += "<option value=\"\">-- Select Region --</option>\n";
    for (size_t i = 0; i < tzCount; i++) {
      bool isSelected = (settings.timezoneIndex < 255) ? (i == settings.timezoneIndex)
                                                       : (strcmp(settings.timezoneString, regions[i].posixString) == 0);
      out += "<option value=\"" + String(i) + "\"" + (isSelected ? " selected" : "") + ">" + String(regions[i].name) + "</option>\n";
    }
    return true;
  }

  // --- LED night-light slider (only present when the feature is compiled in) ---
  if (!strcmp(n, "LED_SLIDER")) {
#if LED_PWM_ENABLED
    out = R"LED(<div class="field" style="margin-top:16px"><label class="field-label" for="ledBrightness">LED night light</label><div class="range-row"><input type="range" name="ledBrightness" id="ledBrightness" min="0" max="255" step="5" value=")LED" + String(settings.ledBrightness) + R"LED(" data-pct="1"><span class="range-val" data-for="ledBrightness">)LED" + String((settings.ledBrightness * 100) / 255) + R"LED(%</span></div><p class="field-hint">Optional LED night light (0-100%). Toggle via touch-button long press (hold 1s). Requires a connected LED.</p></div>)LED";
#endif
    return true; // resolved to "" when LED is disabled, so the slider is omitted
  }

  // --- Per-setting placeholders (auto-generated, see gen_template.py) ---
  if (!strcmp(n, "SEL_CLOCKSTYLE_0")) { out = String(settings.clockStyle == 0 ? "selected" : ""); return true; }
  if (!strcmp(n, "SEL_CLOCKSTYLE_1")) { out = String(settings.clockStyle == 1 ? "selected" : ""); return true; }
  if (!strcmp(n, "SEL_CLOCKSTYLE_2")) { out = String(settings.clockStyle == 2 ? "selected" : ""); return true; }
  if (!strcmp(n, "SEL_CLOCKSTYLE_3")) { out = String(settings.clockStyle == 3 ? "selected" : ""); return true; }
  if (!strcmp(n, "SEL_CLOCKSTYLE_5")) { out = String(settings.clockStyle == 5 ? "selected" : ""); return true; }
  if (!strcmp(n, "SEL_CLOCKSTYLE_6")) { out = String(settings.clockStyle == 6 ? "selected" : ""); return true; }
  if (!strcmp(n, "SEL_CLOCKSTYLE_7")) { out = String(settings.clockStyle == 7 ? "selected" : ""); return true; }
  if (!strcmp(n, "SEL_CLOCKSTYLE_8")) { out = String(settings.clockStyle == 8 ? "selected" : ""); return true; }
  if (!strcmp(n, "SEL_CLOCKSTYLE_9")) { out = String(settings.clockStyle == 9 ? "selected" : ""); return true; }
  if (!strcmp(n, "SEL_CLOCKSTYLE_10")) { out = String(settings.clockStyle == 10 ? "selected" : ""); return true; }
  if (!strcmp(n, "SEL_CLOCKSTYLE_11")) { out = String(settings.clockStyle == 11 ? "selected" : ""); return true; }
  if (!strcmp(n, "DSP_CLOCKSTYLE_0")) { out = String(settings.clockStyle == 0 ? "block" : "none"); return true; }
  if (!strcmp(n, "V_MARIOBOUNCEHEIGHT")) { out = String(settings.marioBounceHeight); return true; }
  if (!strcmp(n, "F_MARIOBOUNCEHEIGHT")) { out = String(settings.marioBounceHeight / 10.0, 1); return true; }
  if (!strcmp(n, "V_MARIOBOUNCESPEED")) { out = String(settings.marioBounceSpeed); return true; }
  if (!strcmp(n, "F_MARIOBOUNCESPEED")) { out = String(settings.marioBounceSpeed / 10.0, 1); return true; }
  if (!strcmp(n, "V_MARIOWALKSPEED")) { out = String(settings.marioWalkSpeed); return true; }
  if (!strcmp(n, "F_MARIOWALKSPEED")) { out = String(settings.marioWalkSpeed / 10.0, 1); return true; }
  if (!strcmp(n, "CHK_MARIOSMOOTHANIMATION")) { out = String(settings.marioSmoothAnimation ? "checked" : ""); return true; }
  if (!strcmp(n, "CHK_MARIOIDLEENCOUNTERS")) { out = String(settings.marioIdleEncounters ? "checked" : ""); return true; }
  if (!strcmp(n, "DSP_MARIOIDLEENCOUNTERS")) { out = String(settings.marioIdleEncounters ? "block" : "none"); return true; }
  if (!strcmp(n, "SEL_MARIOENCOUNTERFREQ_0")) { out = String(settings.marioEncounterFreq == 0 ? "selected" : ""); return true; }
  if (!strcmp(n, "SEL_MARIOENCOUNTERFREQ_1")) { out = String(settings.marioEncounterFreq == 1 ? "selected" : ""); return true; }
  if (!strcmp(n, "SEL_MARIOENCOUNTERFREQ_2")) { out = String(settings.marioEncounterFreq == 2 ? "selected" : ""); return true; }
  if (!strcmp(n, "SEL_MARIOENCOUNTERFREQ_3")) { out = String(settings.marioEncounterFreq == 3 ? "selected" : ""); return true; }
  if (!strcmp(n, "SEL_MARIOENCOUNTERSPEED_0")) { out = String(settings.marioEncounterSpeed == 0 ? "selected" : ""); return true; }
  if (!strcmp(n, "SEL_MARIOENCOUNTERSPEED_1")) { out = String(settings.marioEncounterSpeed == 1 ? "selected" : ""); return true; }
  if (!strcmp(n, "SEL_MARIOENCOUNTERSPEED_2")) { out = String(settings.marioEncounterSpeed == 2 ? "selected" : ""); return true; }
  if (!strcmp(n, "DSP_CLOCKSTYLE_5")) { out = String(settings.clockStyle == 5 ? "block" : "none"); return true; }
  if (!strcmp(n, "V_PONGBALLSPEED")) { out = String(settings.pongBallSpeed); return true; }
  if (!strcmp(n, "V_PONGBOUNCESTRENGTH")) { out = String(settings.pongBounceStrength); return true; }
  if (!strcmp(n, "F_PONGBOUNCESTRENGTH")) { out = String(settings.pongBounceStrength / 10.0, 1); return true; }
  if (!strcmp(n, "V_PONGBOUNCEDAMPING")) { out = String(settings.pongBounceDamping); return true; }
  if (!strcmp(n, "F2_PONGBOUNCEDAMPING")) { out = String(settings.pongBounceDamping / 100.0, 2); return true; }
  if (!strcmp(n, "V_PONGPADDLEWIDTH")) { out = String(settings.pongPaddleWidth); return true; }
  if (!strcmp(n, "CHK_PONGHORIZONTALBOUNCE")) { out = String(settings.pongHorizontalBounce ? "checked" : ""); return true; }
  if (!strcmp(n, "DSP_CLOCKSTYLE_6")) { out = String(settings.clockStyle == 6 ? "block" : "none"); return true; }
  if (!strcmp(n, "V_PACMANSPEED")) { out = String(settings.pacmanSpeed); return true; }
  if (!strcmp(n, "F_PACMANSPEED")) { out = String(settings.pacmanSpeed / 10.0, 1); return true; }
  if (!strcmp(n, "V_PACMANEATINGSPEED")) { out = String(settings.pacmanEatingSpeed); return true; }
  if (!strcmp(n, "F_PACMANEATINGSPEED")) { out = String(settings.pacmanEatingSpeed / 10.0, 1); return true; }
  if (!strcmp(n, "V_PACMANMOUTHSPEED")) { out = String(settings.pacmanMouthSpeed); return true; }
  if (!strcmp(n, "F_PACMANMOUTHSPEED")) { out = String(settings.pacmanMouthSpeed / 10.0, 1); return true; }
  if (!strcmp(n, "V_PACMANPELLETCOUNT")) { out = String(settings.pacmanPelletCount); return true; }
  if (!strcmp(n, "CHK_PACMANPELLETRANDOMSPACING")) { out = String(settings.pacmanPelletRandomSpacing ? "checked" : ""); return true; }
  if (!strcmp(n, "CHK_PACMANBOUNCEENABLED")) { out = String(settings.pacmanBounceEnabled ? "checked" : ""); return true; }
  if (!strcmp(n, "DSP_CLOCKSTYLE_34")) { out = String((settings.clockStyle == 3 || settings.clockStyle == 4) ? "block" : "none"); return true; }
  if (!strcmp(n, "SEL_SPACECHARACTERTYPE_0")) { out = String(settings.spaceCharacterType == 0 ? "selected" : ""); return true; }
  if (!strcmp(n, "SEL_SPACECHARACTERTYPE_1")) { out = String(settings.spaceCharacterType == 1 ? "selected" : ""); return true; }
  if (!strcmp(n, "V_SPACEPATROLSPEED")) { out = String(settings.spacePatrolSpeed); return true; }
  if (!strcmp(n, "F_SPACEPATROLSPEED")) { out = String(settings.spacePatrolSpeed / 10.0, 1); return true; }
  if (!strcmp(n, "V_SPACEATTACKSPEED")) { out = String(settings.spaceAttackSpeed); return true; }
  if (!strcmp(n, "F_SPACEATTACKSPEED")) { out = String(settings.spaceAttackSpeed / 10.0, 1); return true; }
  if (!strcmp(n, "V_SPACELASERSPEED")) { out = String(settings.spaceLaserSpeed); return true; }
  if (!strcmp(n, "F_SPACELASERSPEED")) { out = String(settings.spaceLaserSpeed / 10.0, 1); return true; }
  if (!strcmp(n, "V_SPACEEXPLOSIONGRAVITY")) { out = String(settings.spaceExplosionGravity); return true; }
  if (!strcmp(n, "F_SPACEEXPLOSIONGRAVITY")) { out = String(settings.spaceExplosionGravity / 10.0, 1); return true; }
  if (!strcmp(n, "DSP_CLOCKSTYLE_7")) { out = String(settings.clockStyle == 7 ? "block" : "none"); return true; }
  if (!strcmp(n, "V_SNAKESPEED")) { out = String(settings.snakeSpeed); return true; }
  if (!strcmp(n, "F_SNAKESPEED")) { out = String(settings.snakeSpeed / 10.0, 1); return true; }
  if (!strcmp(n, "V_SNAKELENGTH")) { out = String(settings.snakeLength); return true; }
  if (!strcmp(n, "CHK_SNAKEWALLBORDER")) { out = String(settings.snakeWallBorder ? "checked" : ""); return true; }
  if (!strcmp(n, "CHK_SNAKESHOWDATE")) { out = String(settings.snakeShowDate ? "checked" : ""); return true; }
  if (!strcmp(n, "DSP_CLOCKSTYLE_8")) { out = String(settings.clockStyle == 8 ? "block" : "none"); return true; }
  if (!strcmp(n, "V_TETRISFALLSPEED")) { out = String(settings.tetrisFallSpeed); return true; }
  if (!strcmp(n, "F_TETRISFALLSPEED")) { out = String(settings.tetrisFallSpeed / 10.0, 1); return true; }
  if (!strcmp(n, "SEL_TETRISBLOCKSTYLE_0")) { out = String(settings.tetrisBlockStyle == 0 ? "selected" : ""); return true; }
  if (!strcmp(n, "SEL_TETRISBLOCKSTYLE_1")) { out = String(settings.tetrisBlockStyle == 1 ? "selected" : ""); return true; }
  if (!strcmp(n, "CHK_TETRISIDLETUMBLE")) { out = String(settings.tetrisIdleTumble ? "checked" : ""); return true; }
  if (!strcmp(n, "CHK_TETRISDIGITBOUNCE")) { out = String(settings.tetrisDigitBounce ? "checked" : ""); return true; }
  if (!strcmp(n, "CHK_TETRISSMOOTHGAME")) { out = String(settings.tetrisSmoothGame ? "checked" : ""); return true; }
  if (!strcmp(n, "SEL_TETRISANIMSTYLE_0")) { out = String(settings.tetrisAnimStyle == 0 ? "selected" : ""); return true; }
  if (!strcmp(n, "SEL_TETRISANIMSTYLE_1")) { out = String(settings.tetrisAnimStyle == 1 ? "selected" : ""); return true; }
  if (!strcmp(n, "V_TETRISDOTSPEED")) { out = String(settings.tetrisDotSpeed); return true; }
  if (!strcmp(n, "F_TETRISDOTSPEED")) { out = String(settings.tetrisDotSpeed / 10.0, 1); return true; }
  if (!strcmp(n, "SEL_TETRISDOTORDER_0")) { out = String(settings.tetrisDotOrder == 0 ? "selected" : ""); return true; }
  if (!strcmp(n, "SEL_TETRISDOTORDER_1")) { out = String(settings.tetrisDotOrder == 1 ? "selected" : ""); return true; }
  if (!strcmp(n, "CHK_TETRISSHOWDATE")) { out = String(settings.tetrisShowDate ? "checked" : ""); return true; }
  if (!strcmp(n, "SEL_TETRISDATEPOSITION_0")) { out = String(settings.tetrisDatePosition == 0 ? "selected" : ""); return true; }
  if (!strcmp(n, "SEL_TETRISDATEPOSITION_1")) { out = String(settings.tetrisDatePosition == 1 ? "selected" : ""); return true; }
  if (!strcmp(n, "DSP_CLOCKSTYLE_10")) { out = String(settings.clockStyle == 10 ? "block" : "none"); return true; }
  if (!strcmp(n, "V_ASTEROIDSSHIPSPEED")) { out = String(settings.asteroidsShipSpeed); return true; }
  if (!strcmp(n, "F_ASTEROIDSSHIPSPEED")) { out = String(settings.asteroidsShipSpeed / 10.0, 1); return true; }
  if (!strcmp(n, "V_ASTEROIDSROCKCOUNT")) { out = String(settings.asteroidsRockCount); return true; }
  if (!strcmp(n, "V_ASTEROIDSROCKSPEED")) { out = String(settings.asteroidsRockSpeed); return true; }
  if (!strcmp(n, "F_ASTEROIDSROCKSPEED")) { out = String(settings.asteroidsRockSpeed / 10.0, 1); return true; }
  if (!strcmp(n, "CHK_ASTEROIDSSHOWDATE")) { out = String(settings.asteroidsShowDate ? "checked" : ""); return true; }
  if (!strcmp(n, "CHK_ASTEROIDSTRANSPARENT")) { out = String(settings.asteroidsTransparent ? "checked" : ""); return true; }
  if (!strcmp(n, "DSP_CLOCKSTYLE_11")) { out = String(settings.clockStyle == 11 ? "block" : "none"); return true; }
  if (!strcmp(n, "V_DINOSPEED")) { out = String(settings.dinoSpeed); return true; }
  if (!strcmp(n, "F_DINOSPEED")) { out = String(settings.dinoSpeed / 10.0, 1); return true; }
  if (!strcmp(n, "SEL_DINOCACTUSFREQ_0")) { out = String(settings.dinoCactusFreq == 0 ? "selected" : ""); return true; }
  if (!strcmp(n, "SEL_DINOCACTUSFREQ_1")) { out = String(settings.dinoCactusFreq == 1 ? "selected" : ""); return true; }
  if (!strcmp(n, "SEL_DINOCACTUSFREQ_2")) { out = String(settings.dinoCactusFreq == 2 ? "selected" : ""); return true; }
  if (!strcmp(n, "CHK_DINOSHOWCLOUDS")) { out = String(settings.dinoShowClouds ? "checked" : ""); return true; }
  if (!strcmp(n, "CHK_DINOSHOWDATE")) { out = String(settings.dinoShowDate ? "checked" : ""); return true; }
  if (!strcmp(n, "SEL_USE24HOUR")) { out = String(settings.use24Hour ? "selected" : ""); return true; }
  if (!strcmp(n, "SEL_USE24HOUR_NOT")) { out = String(!settings.use24Hour ? "selected" : ""); return true; }
  if (!strcmp(n, "SEL_DATEFORMAT_0")) { out = String(settings.dateFormat == 0 ? "selected" : ""); return true; }
  if (!strcmp(n, "SEL_DATEFORMAT_1")) { out = String(settings.dateFormat == 1 ? "selected" : ""); return true; }
  if (!strcmp(n, "SEL_DATEFORMAT_2")) { out = String(settings.dateFormat == 2 ? "selected" : ""); return true; }
  if (!strcmp(n, "SEL_DATEFORMAT_3")) { out = String(settings.dateFormat == 3 ? "selected" : ""); return true; }
  if (!strcmp(n, "SEL_COLONBLINKMODE_0")) { out = String(settings.colonBlinkMode == 0 ? "selected" : ""); return true; }
  if (!strcmp(n, "SEL_COLONBLINKMODE_1")) { out = String(settings.colonBlinkMode == 1 ? "selected" : ""); return true; }
  if (!strcmp(n, "SEL_COLONBLINKMODE_2")) { out = String(settings.colonBlinkMode == 2 ? "selected" : ""); return true; }
  if (!strcmp(n, "V_COLONBLINKRATE")) { out = String(settings.colonBlinkRate); return true; }
  if (!strcmp(n, "F_COLONBLINKRATE")) { out = String(settings.colonBlinkRate / 10.0, 1); return true; }
  if (!strcmp(n, "SEL_REFRESHRATEMODE_0")) { out = String(settings.refreshRateMode == 0 ? "selected" : ""); return true; }
  if (!strcmp(n, "SEL_REFRESHRATEMODE_1")) { out = String(settings.refreshRateMode == 1 ? "selected" : ""); return true; }
  if (!strcmp(n, "DSP_REFRESHRATEMODE_1")) { out = String(settings.refreshRateMode == 1 ? "block" : "none"); return true; }
  if (!strcmp(n, "V_REFRESHRATEHZ")) { out = String(settings.refreshRateHz); return true; }
  if (!strcmp(n, "CHK_BOOSTANIMATIONREFRESH")) { out = String(settings.boostAnimationRefresh ? "checked" : ""); return true; }
  if (!strcmp(n, "V_DISPLAYBRIGHTNESS")) { out = String(settings.displayBrightness); return true; }
  if (!strcmp(n, "PCT_DISPLAYBRIGHTNESS")) { out = String((settings.displayBrightness * 100) / 255); return true; }
  if (!strcmp(n, "CHK_ENABLESCHEDULEDDIMMING")) { out = String(settings.enableScheduledDimming ? "checked" : ""); return true; }
  if (!strcmp(n, "DSP_ENABLESCHEDULEDDIMMING")) { out = String(settings.enableScheduledDimming ? "block" : "none"); return true; }
  if (!strcmp(n, "V_DIMBRIGHTNESS")) { out = String(settings.dimBrightness); return true; }
  if (!strcmp(n, "PCT_DIMBRIGHTNESS")) { out = String((settings.dimBrightness * 100) / 255); return true; }
  if (!strcmp(n, "V_DEVICENAME")) { out = String(settings.deviceName); return true; }
  if (!strcmp(n, "SEL_USESTATICIP_NOT")) { out = String(!settings.useStaticIP ? "selected" : ""); return true; }
  if (!strcmp(n, "SEL_USESTATICIP")) { out = String(settings.useStaticIP ? "selected" : ""); return true; }
  if (!strcmp(n, "DSP_USESTATICIP")) { out = String(settings.useStaticIP ? "block" : "none"); return true; }
  if (!strcmp(n, "V_STATICIP")) { out = String(settings.staticIP); return true; }
  if (!strcmp(n, "V_GATEWAY")) { out = String(settings.gateway); return true; }
  if (!strcmp(n, "V_SUBNET")) { out = String(settings.subnet); return true; }
  if (!strcmp(n, "V_DNS1")) { out = String(settings.dns1); return true; }
  if (!strcmp(n, "V_DNS2")) { out = String(settings.dns2); return true; }
  if (!strcmp(n, "CHK_SHOWIPATBOOT")) { out = String(settings.showIPAtBoot ? "checked" : ""); return true; }
  if (!strcmp(n, "SEL_CLOCKPOSITION_0")) { out = String(settings.clockPosition == 0 ? "selected" : ""); return true; }
  if (!strcmp(n, "SEL_CLOCKPOSITION_1")) { out = String(settings.clockPosition == 1 ? "selected" : ""); return true; }
  if (!strcmp(n, "SEL_CLOCKPOSITION_2")) { out = String(settings.clockPosition == 2 ? "selected" : ""); return true; }
  if (!strcmp(n, "V_CLOCKOFFSET")) { out = String(settings.clockOffset); return true; }
  if (!strcmp(n, "CHK_SHOWCLOCK")) { out = String(settings.showClock ? "checked" : ""); return true; }
  if (!strcmp(n, "SEL_DISPLAYROWMODE_0")) { out = String(settings.displayRowMode == 0 ? "selected" : ""); return true; }
  if (!strcmp(n, "SEL_DISPLAYROWMODE_1")) { out = String(settings.displayRowMode == 1 ? "selected" : ""); return true; }
  if (!strcmp(n, "SEL_DISPLAYROWMODE_2")) { out = String(settings.displayRowMode == 2 ? "selected" : ""); return true; }
  if (!strcmp(n, "SEL_DISPLAYROWMODE_3")) { out = String(settings.displayRowMode == 3 ? "selected" : ""); return true; }
  if (!strcmp(n, "CHK_USERPMKFORMAT")) { out = String(settings.useRpmKFormat ? "checked" : ""); return true; }
  if (!strcmp(n, "CHK_USENETWORKMBFORMAT")) { out = String(settings.useNetworkMBFormat ? "checked" : ""); return true; }
  if (!strcmp(n, "JS_MAXROWS")) { out = String(settings.displayRowMode==0?5:settings.displayRowMode==1?6:settings.displayRowMode==2?2:3); return true; }
  if (!strcmp(n, "JS_ISLARGE")) { out = String(settings.displayRowMode>=2?"true":"false"); return true; }

  return false;
}

// Stream PAGE_HTML from flash, resolving %TOKEN% placeholders on the fly.
// Literal HTML and resolved values flow through one fixed buffer that is
// flushed to the client only when full (HTTP chunked transfer).
static void streamTemplate(const char* tmpl, size_t tmplLen) {
  static const size_t BUF_SIZE = 2048;
  char* buf = (char*)malloc(BUF_SIZE + 1);
  if (!buf) {
    server.send(503, "text/plain", "Out of memory");
    return;
  }
  size_t bufLen = 0;

  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html", "");

  auto flush = [&]() {
    if (bufLen > 0) {
      buf[bufLen] = '\0';
      server.sendContent(buf);
      bufLen = 0;
    }
  };

  auto emit = [&](const char* data, size_t len) {
    while (len > 0) {
      size_t space = BUF_SIZE - bufLen;
      size_t take = len < space ? len : space;
      memcpy(buf + bufLen, data, take);
      bufLen += take;
      data += take;
      len -= take;
      if (bufLen >= BUF_SIZE) flush();
    }
  };

  // On ESP32 PROGMEM is memory-mapped, so the template is readable directly.
  const char* end = tmpl + tmplLen;
  const char* pos = tmpl;
  const char* literalStart = tmpl;

  while (pos < end) {
    if (*pos != '%') { pos++; continue; }
    if (pos + 1 >= end || !(pos[1] >= 'A' && pos[1] <= 'Z')) { pos++; continue; }

    const char* pEnd = pos + 1;
    while (pEnd < end && *pEnd != '%' && (pEnd - pos) < 40) pEnd++;
    if (pEnd >= end || *pEnd != '%') { pos++; continue; }

    bool valid = true;
    for (const char* c = pos + 1; c < pEnd; c++) {
      if (!((*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9') || *c == '_')) { valid = false; break; }
    }
    if (!valid) { pos++; continue; }

    size_t nameLen = pEnd - pos - 1;
    char name[40];
    if (nameLen >= sizeof(name)) { pos++; continue; }
    memcpy(name, pos + 1, nameLen);
    name[nameLen] = '\0';

    String value;
    if (resolvePlaceholder(name, value)) {
      if (pos > literalStart) emit(literalStart, pos - literalStart);
      if (value.length() > 0) emit(value.c_str(), value.length());
      pos = pEnd + 1;
      literalStart = pos;
    } else {
      pos++;
    }
  }

  if (end > literalStart) emit(literalStart, end - literalStart);
  flush();
  server.sendContent("");
  free(buf);
}

void handleRoot() {
  streamTemplate(PAGE_HTML, sizeof(PAGE_HTML) - 1);
}

// Stream a static PROGMEM asset (CSS/JS) in chunks. These contain no %TOKEN%s,
// so they are emitted verbatim and cached hard by the browser (fetched once).
static void streamStatic(const char* data, size_t len, const char* contentType) {
  server.sendHeader("Cache-Control", "public, max-age=31536000, immutable");
  server.setContentLength(len);
  server.send(200, contentType, "");
  static const size_t CHUNK = 1024;
  char* buf = (char*)malloc(CHUNK + 1);
  if (!buf) { server.sendContent(""); return; }
  size_t pos = 0;
  while (pos < len) {
    size_t take = (len - pos < CHUNK) ? (len - pos) : CHUNK;
    memcpy(buf, data + pos, take); // PROGMEM is memory-mapped on ESP32
    buf[take] = '\0';
    server.sendContent(buf, take);
    pos += take;
  }
  free(buf);
  server.sendContent("");
}

void handlePortalCss() {
  streamStatic(PORTAL_CSS, sizeof(PORTAL_CSS) - 1, "text/css");
}

void handlePortalJs() {
  streamStatic(PORTAL_JS, sizeof(PORTAL_JS) - 1, "application/javascript");
}

void handleSave() {
 if (server.hasArg("clockStyle")) {
 settings.clockStyle = server.arg("clockStyle").toInt();
 }

 // Handle new timezone region selector (value is now an index into timezone database)
 if (server.hasArg("timezoneRegion")) {
 String tzVal = server.arg("timezoneRegion");
 if (tzVal.length() > 0) {
 int idx = tzVal.toInt();
 size_t tzCount;
 const TimezoneRegion* regions = getSupportedTimezones(&tzCount);
 if (idx >= 0 && idx < (int)tzCount) {
 settings.timezoneIndex = (uint8_t)idx;
 strncpy(settings.timezoneString, regions[idx].posixString, 63);
 settings.timezoneString[63] = '\0';
 settings.gmtOffset = regions[idx].gmtOffsetMinutes;
 settings.daylightSaving = true;
 }
 }
 }

 // Legacy: handle old gmtOffset/dst fields if timezoneRegion is not set
 if (server.hasArg("gmtOffset") && strlen(settings.timezoneString) == 0) {
 settings.gmtOffset = server.arg("gmtOffset").toInt();
 }
 if (server.hasArg("dst") && strlen(settings.timezoneString) == 0) {
 settings.daylightSaving = server.arg("dst").toInt() == 1;
 }

 if (server.hasArg("use24Hour")) {
 settings.use24Hour = server.arg("use24Hour").toInt() == 1;
 }
 if (server.hasArg("dateFormat")) {
 settings.dateFormat = server.arg("dateFormat").toInt();
 }

 // Save clock position
 if (server.hasArg("clockPosition")) {
 settings.clockPosition = server.arg("clockPosition").toInt();
 }

 // Save clock offset
 if (server.hasArg("clockOffset")) {
 settings.clockOffset = server.arg("clockOffset").toInt();
 }

 // Save clock visibility checkbox
 settings.showClock = server.hasArg("showClock");

 // Save row mode selection
 if (server.hasArg("rowMode")) {
 settings.displayRowMode = server.arg("rowMode").toInt();
 }

 // Save RPM format preference
 settings.useRpmKFormat = server.hasArg("rpmKFormat");

 // Save network speed format preference
 settings.useNetworkMBFormat = server.hasArg("netMBFormat");

 // Save colon blink settings
 if (server.hasArg("colonBlinkMode")) {
 settings.colonBlinkMode = server.arg("colonBlinkMode").toInt();
 }
 if (server.hasArg("colonBlinkRate")) {
 settings.colonBlinkRate = server.arg("colonBlinkRate").toInt();
 }

 // Save refresh rate settings
 if (server.hasArg("refreshRateMode")) {
 settings.refreshRateMode = server.arg("refreshRateMode").toInt();
 }
 if (server.hasArg("refreshRateHz")) {
 settings.refreshRateHz = server.arg("refreshRateHz").toInt();
 }

 // Save animation boost checkbox
 settings.boostAnimationRefresh = server.hasArg("boostAnim");

 bool brightnessSettingsChanged = false;

 // Save display brightness
 if (server.hasArg("displayBrightness")) {
 uint8_t newBrightness = sanitizeBrightnessValue(server.arg("displayBrightness").toInt());
 if (newBrightness != settings.displayBrightness) {
 settings.displayBrightness = newBrightness;
 brightnessSettingsChanged = true;
 }
 }

#if LED_PWM_ENABLED
 // Save LED brightness
 if (server.hasArg("ledBrightness")) {
 settings.ledBrightness = server.arg("ledBrightness").toInt();
 setLEDBrightness(settings.ledBrightness); // Apply immediately
 }
#endif

 // Save scheduled dimming settings
 bool scheduledDimmingEnabled = server.hasArg("enableScheduledDimming");
 if (scheduledDimmingEnabled != settings.enableScheduledDimming) {
 settings.enableScheduledDimming = scheduledDimmingEnabled;
 brightnessSettingsChanged = true;
 } else {
 settings.enableScheduledDimming = scheduledDimmingEnabled;
 }
 if (server.hasArg("dimStartHour")) {
 uint8_t newDimStartHour = server.arg("dimStartHour").toInt();
 if (newDimStartHour != settings.dimStartHour) {
 settings.dimStartHour = newDimStartHour;
 brightnessSettingsChanged = true;
 }
 }
 if (server.hasArg("dimEndHour")) {
 uint8_t newDimEndHour = server.arg("dimEndHour").toInt();
 if (newDimEndHour != settings.dimEndHour) {
 settings.dimEndHour = newDimEndHour;
 brightnessSettingsChanged = true;
 }
 }
 if (server.hasArg("dimBrightness")) {
 uint8_t newDimBrightness = sanitizeBrightnessValue(server.arg("dimBrightness").toInt());
 if (newDimBrightness != settings.dimBrightness) {
 settings.dimBrightness = newDimBrightness;
 brightnessSettingsChanged = true;
 }
 }

 if (brightnessSettingsChanged) {
 refreshDisplayBrightnessNow();
 }

 // Save Mario bounce settings
 if (server.hasArg("marioBounceHeight")) {
 settings.marioBounceHeight = server.arg("marioBounceHeight").toInt();
 }
 if (server.hasArg("marioBounceSpeed")) {
 settings.marioBounceSpeed = server.arg("marioBounceSpeed").toInt();
 }
 // Save Mario smooth animation checkbox
 settings.marioSmoothAnimation = server.hasArg("marioSmoothAnimation");
 // Save Mario walk speed
 if (server.hasArg("marioWalkSpeed")) {
 settings.marioWalkSpeed = server.arg("marioWalkSpeed").toInt();
 }
 // Save Mario idle encounters
 settings.marioIdleEncounters = server.hasArg("marioIdleEncounters");
 if (server.hasArg("marioEncounterFreq")) {
 settings.marioEncounterFreq = constrain(server.arg("marioEncounterFreq").toInt(), 0, 3);
 }
 if (server.hasArg("marioEncounterSpeed")) {
 settings.marioEncounterSpeed = constrain(server.arg("marioEncounterSpeed").toInt(), 0, 2);
 }

 // Save Pong settings
 if (server.hasArg("pongBallSpeed")) {
 settings.pongBallSpeed = server.arg("pongBallSpeed").toInt();
 }
 if (server.hasArg("pongBounceStrength")) {
 settings.pongBounceStrength = server.arg("pongBounceStrength").toInt();
 }
 if (server.hasArg("pongBounceDamping")) {
 settings.pongBounceDamping = server.arg("pongBounceDamping").toInt();
 }
 if (server.hasArg("pongPaddleWidth")) {
 settings.pongPaddleWidth = server.arg("pongPaddleWidth").toInt();
 }
 settings.pongHorizontalBounce = server.hasArg("pongHorizontalBounce");

 // Save Pac-Man settings
 if (server.hasArg("pacmanSpeed")) {
 settings.pacmanSpeed = server.arg("pacmanSpeed").toInt();
 }
 if (server.hasArg("pacmanEatingSpeed")) {
 settings.pacmanEatingSpeed = server.arg("pacmanEatingSpeed").toInt();
 }
 if (server.hasArg("pacmanMouthSpeed")) {
 settings.pacmanMouthSpeed = server.arg("pacmanMouthSpeed").toInt();
 }
 if (server.hasArg("pacmanPelletCount")) {
 settings.pacmanPelletCount = server.arg("pacmanPelletCount").toInt();
 }
 settings.pacmanPelletRandomSpacing = server.hasArg("pacmanPelletRandomSpacing");
 settings.pacmanBounceEnabled = server.hasArg("pacmanBounceEnabled");

 // Save Space Clock settings
 if (server.hasArg("spaceCharacterType")) {
 settings.spaceCharacterType = server.arg("spaceCharacterType").toInt();
 }
 if (server.hasArg("spacePatrolSpeed")) {
 settings.spacePatrolSpeed = server.arg("spacePatrolSpeed").toInt();
 }
 if (server.hasArg("spaceAttackSpeed")) {
 settings.spaceAttackSpeed = server.arg("spaceAttackSpeed").toInt();
 }
 if (server.hasArg("spaceLaserSpeed")) {
 settings.spaceLaserSpeed = server.arg("spaceLaserSpeed").toInt();
 }
 if (server.hasArg("spaceExplosionGravity")) {
 settings.spaceExplosionGravity = server.arg("spaceExplosionGravity").toInt();
 }

 // Save Snake settings
 if (server.hasArg("snakeSpeed")) {
 settings.snakeSpeed = server.arg("snakeSpeed").toInt();
 }
 if (server.hasArg("snakeLength")) {
 settings.snakeLength = server.arg("snakeLength").toInt();
 }
 settings.snakeWallBorder = server.hasArg("snakeWallBorder");
 settings.snakeShowDate = server.hasArg("snakeShowDate");

 // Save Tetris settings
 if (server.hasArg("tetrisFallSpeed")) {
 settings.tetrisFallSpeed = server.arg("tetrisFallSpeed").toInt();
 }
 if (server.hasArg("tetrisBlockStyle")) {
 settings.tetrisBlockStyle = server.arg("tetrisBlockStyle").toInt();
 }
 settings.tetrisIdleTumble = server.hasArg("tetrisIdleTumble");
 settings.tetrisDigitBounce = server.hasArg("tetrisDigitBounce");
 settings.tetrisSmoothGame = server.hasArg("tetrisSmoothGame");
 if (server.hasArg("tetrisAnimStyle")) {
 settings.tetrisAnimStyle = server.arg("tetrisAnimStyle").toInt();
 }
 settings.tetrisShowDate = server.hasArg("tetrisShowDate");
 if (server.hasArg("tetrisDatePosition")) {
 settings.tetrisDatePosition = server.arg("tetrisDatePosition").toInt();
 }
 if (server.hasArg("tetrisDotSpeed")) {
 settings.tetrisDotSpeed = server.arg("tetrisDotSpeed").toInt();
 }
 if (server.hasArg("tetrisDotOrder")) {
 settings.tetrisDotOrder = server.arg("tetrisDotOrder").toInt();
 }

 // Save Asteroids settings
 if (server.hasArg("asteroidsShipSpeed")) {
 settings.asteroidsShipSpeed = server.arg("asteroidsShipSpeed").toInt();
 }
 if (server.hasArg("asteroidsRockCount")) {
 settings.asteroidsRockCount = server.arg("asteroidsRockCount").toInt();
 }
 if (server.hasArg("asteroidsRockSpeed")) {
 settings.asteroidsRockSpeed = server.arg("asteroidsRockSpeed").toInt();
 }
 settings.asteroidsShowDate = server.hasArg("asteroidsShowDate");
 settings.asteroidsTransparent = server.hasArg("asteroidsTransparent");

 // Save Dino Runner settings
 if (server.hasArg("dinoSpeed")) {
 settings.dinoSpeed = server.arg("dinoSpeed").toInt();
 }
 if (server.hasArg("dinoCactusFreq")) {
 settings.dinoCactusFreq = server.arg("dinoCactusFreq").toInt();
 }
 settings.dinoShowClouds = server.hasArg("dinoShowClouds");
 settings.dinoShowDate = server.hasArg("dinoShowDate");

 // Save network configuration
 if (server.hasArg("deviceName")) {
   String name = server.arg("deviceName");
   name.trim();
   if (name.length() > 0 && name.length() <= 31) {
     // Sanitize: only allow letters, numbers, hyphens
     bool valid = true;
     for (unsigned int i = 0; i < name.length(); i++) {
       char c = name.charAt(i);
       if (!isalnum(c) && c != '-') { valid = false; break; }
     }
     if (valid && isalpha(name.charAt(0))) {
       bool nameChanged = (strcmp(settings.deviceName, name.c_str()) != 0);
       safeCopyString(settings.deviceName, name.c_str(), sizeof(settings.deviceName));
       if (nameChanged) {
         initMDNS();  // Re-register mDNS with new name
       }
     }
   }
 }
 settings.showIPAtBoot = server.hasArg("showIPAtBoot");
 bool previousStaticIPSetting = settings.useStaticIP;
 if (server.hasArg("useStaticIP")) {
 settings.useStaticIP = server.arg("useStaticIP").toInt() == 1;
 }
 if (server.hasArg("staticIP")) {
 String ipStr = server.arg("staticIP");
 if (ipStr.length() > 0 && validateIP(ipStr.c_str())) {
 safeCopyString(settings.staticIP, ipStr.c_str(), sizeof(settings.staticIP));
 } else if (ipStr.length() > 0) {
 Serial.println("WARNING: Invalid static IP format, ignoring");
 }
 }
 if (server.hasArg("gateway")) {
 String ipStr = server.arg("gateway");
 if (ipStr.length() > 0 && validateIP(ipStr.c_str())) {
 safeCopyString(settings.gateway, ipStr.c_str(), sizeof(settings.gateway));
 } else if (ipStr.length() > 0) {
 Serial.println("WARNING: Invalid gateway format, ignoring");
 }
 }
 if (server.hasArg("subnet")) {
 String ipStr = server.arg("subnet");
 if (ipStr.length() > 0 && validateIP(ipStr.c_str())) {
 safeCopyString(settings.subnet, ipStr.c_str(), sizeof(settings.subnet));
 } else if (ipStr.length() > 0) {
 Serial.println("WARNING: Invalid subnet format, ignoring");
 }
 }
 if (server.hasArg("dns1")) {
 String ipStr = server.arg("dns1");
 if (ipStr.length() > 0 && validateIP(ipStr.c_str())) {
 safeCopyString(settings.dns1, ipStr.c_str(), sizeof(settings.dns1));
 } else if (ipStr.length() > 0) {
 Serial.println("WARNING: Invalid DNS1 format, ignoring");
 }
 }
 if (server.hasArg("dns2")) {
 String ipStr = server.arg("dns2");
 if (ipStr.length() > 0 && validateIP(ipStr.c_str())) {
 safeCopyString(settings.dns2, ipStr.c_str(), sizeof(settings.dns2));
 } else if (ipStr.length() > 0) {
 Serial.println("WARNING: Invalid DNS2 format, ignoring");
 }
 }

 // Save custom labels
 for (int i = 0; i < MAX_METRICS; i++) {
 String labelArg = "label_" + String(i + 1);
 if (server.hasArg(labelArg)) {
 String label = server.arg(labelArg);
 label.trim();
 if (label.length() > 0) {
 strncpy(settings.metricLabels[i], label.c_str(), METRIC_NAME_LEN - 1);
 settings.metricLabels[i][METRIC_NAME_LEN - 1] = '\0';
 } else {
 settings.metricLabels[i][0] = '\0'; // Empty = use Python name
 }
 }
 }

 // Save metric display order
 for (int i = 0; i < MAX_METRICS; i++) {
 String orderArg = "order_" + String(i + 1);
 if (server.hasArg(orderArg)) {
 settings.metricOrder[i] = server.arg(orderArg).toInt();
 }
 }

 // Save metric companions
 for (int i = 0; i < MAX_METRICS; i++) {
 String companionArg = "companion_" + String(i + 1);
 if (server.hasArg(companionArg)) {
 settings.metricCompanions[i] = server.arg(companionArg).toInt();
 } else {
 settings.metricCompanions[i] = 0; // No companion
 }
 }

 // Save metric positions
 for (int i = 0; i < MAX_METRICS; i++) {
 String positionArg = "position_" + String(i + 1);
 if (server.hasArg(positionArg)) {
 settings.metricPositions[i] = server.arg(positionArg).toInt();
 } else {
 settings.metricPositions[i] = 255; // Default: None/Hidden
 }
 }

 // Save progress bar settings
 for (int i = 0; i < MAX_METRICS; i++) {
 String barPosArg = "barPosition_" + String(i + 1);
 String minArg = "barMin_" + String(i + 1);
 String maxArg = "barMax_" + String(i + 1);
 String widthArg = "barWidth_" + String(i + 1);
 String offsetArg = "barOffset_" + String(i + 1);

 if (server.hasArg(barPosArg)) {
 settings.metricBarPositions[i] = server.arg(barPosArg).toInt();
 } else {
 settings.metricBarPositions[i] = 255; // Default: No bar
 }

 if (server.hasArg(minArg)) {
 settings.metricBarMin[i] = server.arg(minArg).toInt();
 }
 if (server.hasArg(maxArg)) {
 settings.metricBarMax[i] = server.arg(maxArg).toInt();
 }
 if (server.hasArg(widthArg)) {
 settings.metricBarWidths[i] = server.arg(widthArg).toInt();
 } else {
 settings.metricBarWidths[i] = 60; // Default width
 }
 if (server.hasArg(offsetArg)) {
 settings.metricBarOffsets[i] = server.arg(offsetArg).toInt();
 } else {
 settings.metricBarOffsets[i] = 0; // Default: no offset
 }
 }

 // Hide out-of-range positions based on display mode
 int maxPositions;
 if (settings.displayRowMode == 0) maxPositions = 10;       // 5 rows x 2 cols
 else if (settings.displayRowMode == 1) maxPositions = 12;   // 6 rows x 2 cols
 else if (settings.displayRowMode == 2) maxPositions = 2;    // Large 2-row
 else maxPositions = 3;                                       // Large 3-row

 for (int i = 0; i < MAX_METRICS; i++) {
   if (settings.metricPositions[i] != 255 && settings.metricPositions[i] >= maxPositions) {
     settings.metricPositions[i] = 255; // Hide
   }
   if (settings.metricBarPositions[i] != 255 && settings.metricBarPositions[i] >= maxPositions) {
     settings.metricBarPositions[i] = 255; // Hide bars
   }
 }

 // Apply labels, order, companions, positions, and progress bars to current metrics in memory
 for (int i = 0; i < metricData.count; i++) {
 Metric& m = metricData.metrics[i];
 if (m.id > 0 && m.id <= MAX_METRICS) {
 // Apply custom label if set
 if (settings.metricLabels[m.id - 1][0] != '\0') {
 strncpy(m.label, settings.metricLabels[m.id - 1], METRIC_NAME_LEN - 1);
 m.label[METRIC_NAME_LEN - 1] = '\0';
 } else {
 strncpy(m.label, m.name, METRIC_NAME_LEN - 1);
 m.label[METRIC_NAME_LEN - 1] = '\0';
 }

 // Apply display order
 m.displayOrder = settings.metricOrder[m.id - 1];

 // Apply companion metric
 m.companionId = settings.metricCompanions[m.id - 1];

 // Apply position assignment
 m.position = settings.metricPositions[m.id - 1];

 // Apply progress bar settings
 m.barPosition = settings.metricBarPositions[m.id - 1];
 m.barMin = settings.metricBarMin[m.id - 1];
 m.barMax = settings.metricBarMax[m.id - 1];
 m.barWidth = settings.metricBarWidths[m.id - 1];
 m.barOffsetX = settings.metricBarOffsets[m.id - 1];

 // Store/update the metric name for future validation
 strncpy(settings.metricNames[m.id - 1], m.name, METRIC_NAME_LEN - 1);
 settings.metricNames[m.id - 1][METRIC_NAME_LEN - 1] = '\0';
 }
 }

 // Validate settings bounds before saving
 assertBounds(settings.clockStyle, 0, 11, "clockStyle");
 assertBounds(settings.gmtOffset, -720, 840, "gmtOffset"); // -12h to +14h in minutes
 assertBounds(settings.clockPosition, 0, 2, "clockPosition");
 assertBounds(settings.displayRowMode, 0, 3, "displayRowMode");
 assertBounds(settings.colonBlinkMode, 0, 2, "colonBlinkMode");
 assertBounds(settings.colonBlinkRate, 5, 50, "colonBlinkRate");
 assertBounds(settings.refreshRateMode, 0, 1, "refreshRateMode");
 assertBounds(settings.refreshRateHz, 1, 60, "refreshRateHz");
 assertBounds(settings.marioBounceHeight, 10, 80, "marioBounceHeight");
 assertBounds(settings.marioBounceSpeed, 2, 15, "marioBounceSpeed");
 assertBounds(settings.marioWalkSpeed, 15, 35, "marioWalkSpeed");
 assertBounds(settings.pongBallSpeed, 16, 30, "pongBallSpeed");
 assertBounds(settings.pongBounceStrength, 1, 8, "pongBounceStrength");
 assertBounds(settings.pongBounceDamping, 50, 95, "pongBounceDamping");
 assertBounds(settings.pongPaddleWidth, 10, 40, "pongPaddleWidth");
 assertBounds(settings.pacmanSpeed, 5, 30, "pacmanSpeed");
 assertBounds(settings.pacmanEatingSpeed, 10, 50, "pacmanEatingSpeed");
 assertBounds(settings.pacmanMouthSpeed, 5, 20, "pacmanMouthSpeed");
 assertBounds(settings.pacmanPelletCount, 0, 20, "pacmanPelletCount");
 assertBounds(settings.spaceCharacterType, 0, 1, "spaceCharacterType");
 assertBounds(settings.spacePatrolSpeed, 2, 15, "spacePatrolSpeed");
 assertBounds(settings.spaceAttackSpeed, 10, 40, "spaceAttackSpeed");
 assertBounds(settings.spaceLaserSpeed, 20, 80, "spaceLaserSpeed");
 assertBounds(settings.spaceExplosionGravity, 3, 10, "spaceExplosionGravity");
 assertBounds(settings.snakeSpeed, 5, 30, "snakeSpeed");
 assertBounds(settings.snakeLength, 4, 12, "snakeLength");
 assertBounds(settings.tetrisFallSpeed, 5, 30, "tetrisFallSpeed");
 assertBounds(settings.tetrisBlockStyle, 0, 1, "tetrisBlockStyle");
 assertBounds(settings.tetrisAnimStyle, 0, 1, "tetrisAnimStyle");
 assertBounds(settings.tetrisDatePosition, 0, 1, "tetrisDatePosition");
 assertBounds(settings.tetrisDotSpeed, 5, 30, "tetrisDotSpeed");
 assertBounds(settings.tetrisDotOrder, 0, 1, "tetrisDotOrder");
 assertBounds(settings.asteroidsShipSpeed, 5, 25, "asteroidsShipSpeed");
 assertBounds(settings.asteroidsRockCount, 1, 4, "asteroidsRockCount");
 assertBounds(settings.asteroidsRockSpeed, 3, 20, "asteroidsRockSpeed");
 assertBounds(settings.dinoSpeed, 5, 30, "dinoSpeed");
 assertBounds(settings.dinoCactusFreq, 0, 2, "dinoCactusFreq");

 saveSettings();
 applyTimezone();
 ntpSynced = false; // Force NTP resync after timezone change

 // Reset every clock's animation state (one source of truth in
 // clock_globals.cpp; also clears time_overridden and Pac-Man eat-queue
 // residue).
 resetClockAnimationState();

 // Check if network settings changed - if so, restart is required
 bool networkChanged = (previousStaticIPSetting != settings.useStaticIP);

 // Return JSON response for AJAX
 String json = "{\"success\":true,\"networkChanged\":" + String(networkChanged ? "true" : "false") + "}";
 server.send(200, "application/json", json);

 // If network settings changed, restart after a delay
 if (networkChanged) {
 delay(1000); // Give time for response to be sent
 Serial.println("Network settings changed, restarting...");
 ESP.restart();
 }
}

void handleReset() {
 String html = R"rawliteral(
<!DOCTYPE html><html><head><title>Factory Reset</title><style> body{font-family:Arial;background:#1a1a2e;color:#e94560;display:flex;justify-content:center;align-items:center;height:100vh;margin:0}.msg{text-align:center}</style></head><body><div class="msg"><h1>&#128260;</h1><p>Factory reset in progress...<br>All settings erased.<br>Connect to "PCMonitor-Setup" to reconfigure.</p></div></body></html>
)rawliteral";

 // Send HTML (small page)
 server.send(200, "text/html", html);
 delay(1000);

 // Erase all application settings from NVS
 preferences.begin("pcmonitor", false);
 preferences.clear();
 preferences.end();

 // Erase WiFi credentials
 wifiManager.resetSettings();

 ESP.restart();
}

// Export configuration as JSON
void handleExportConfig() {
 String json = "{";

 // Clock settings
 json += "\"clockStyle\":" + String(settings.clockStyle) + ",";
 json += "\"timezoneString\":\"" + String(settings.timezoneString) + "\",";
 json += "\"gmtOffset\":" + String(settings.gmtOffset) + ",";
 json += "\"daylightSaving\":" + String(settings.daylightSaving ? "true" : "false") + ",";
 json += "\"use24Hour\":" + String(settings.use24Hour ? "true" : "false") + ",";
 json += "\"dateFormat\":" + String(settings.dateFormat) + ",";
 json += "\"clockPosition\":" + String(settings.clockPosition) + ",";
 json += "\"clockOffset\":" + String(settings.clockOffset) + ",";
 json += "\"showClock\":" + String(settings.showClock ? "true" : "false") + ",";
 json += "\"displayRowMode\":" + String(settings.displayRowMode) + ",";
 json += "\"useRpmKFormat\":" + String(settings.useRpmKFormat ? "true" : "false") + ",";
 json += "\"useNetworkMBFormat\":" + String(settings.useNetworkMBFormat ? "true" : "false") + ",";
 json += "\"deviceName\":\"" + String(settings.deviceName) + "\",";
 json += "\"showIPAtBoot\":" + String(settings.showIPAtBoot ? "true" : "false") + ",";

 // Metric labels
 json += "\"metricLabels\":[";
 for (int i = 0; i < MAX_METRICS; i++) {
 if (i > 0) json += ",";
 json += "\"" + String(settings.metricLabels[i]) + "\"";
 }
 json += "],";

 // Metric names
 json += "\"metricNames\":[";
 for (int i = 0; i < MAX_METRICS; i++) {
 if (i > 0) json += ",";
 json += "\"" + String(settings.metricNames[i]) + "\"";
 }
 json += "],";

 // Metric order
 json += "\"metricOrder\":[";
 for (int i = 0; i < MAX_METRICS; i++) {
 if (i > 0) json += ",";
 json += String(settings.metricOrder[i]);
 }
 json += "],";

 // Metric companions
 json += "\"metricCompanions\":[";
 for (int i = 0; i < MAX_METRICS; i++) {
 if (i > 0) json += ",";
 json += String(settings.metricCompanions[i]);
 }
 json += "],";

 // Metric positions
 json += "\"metricPositions\":[";
 for (int i = 0; i < MAX_METRICS; i++) {
 if (i > 0) json += ",";
 json += String(settings.metricPositions[i]);
 }
 json += "],";

 // Progress bar settings
 json += "\"metricBarPositions\":[";
 for (int i = 0; i < MAX_METRICS; i++) {
 if (i > 0) json += ",";
 json += String(settings.metricBarPositions[i]);
 }
 json += "],";

 json += "\"metricBarMin\":[";
 for (int i = 0; i < MAX_METRICS; i++) {
 if (i > 0) json += ",";
 json += String(settings.metricBarMin[i]);
 }
 json += "],";

 json += "\"metricBarMax\":[";
 for (int i = 0; i < MAX_METRICS; i++) {
 if (i > 0) json += ",";
 json += String(settings.metricBarMax[i]);
 }
 json += "],";

 json += "\"metricBarWidths\":[";
 for (int i = 0; i < MAX_METRICS; i++) {
 if (i > 0) json += ",";
 json += String(settings.metricBarWidths[i]);
 }
 json += "],";

 json += "\"metricBarOffsets\":[";
 for (int i = 0; i < MAX_METRICS; i++) {
 if (i > 0) json += ",";
 json += String(settings.metricBarOffsets[i]);
 }
 json += "]";

 json += "}";

 server.sendHeader("Access-Control-Allow-Origin", "*");
 server.send(200, "application/json", json);
}

// Import configuration from JSON
void handleImportConfig() {
 if (server.hasArg("plain")) {
 String body = server.arg("plain");

 JsonDocument doc;
 DeserializationError error = deserializeJson(doc, body);

 if (error) {
 server.send(400, "application/json", "{\"success\":false,\"message\":\"Invalid JSON\"}");
 return;
 }

 // Import clock settings
 if (!doc["clockStyle"].isNull()) settings.clockStyle = doc["clockStyle"];
 if (!doc["timezoneString"].isNull()) {
 const char* tz = doc["timezoneString"];
 if (tz && strlen(tz) < 64) {
 strncpy(settings.timezoneString, tz, 63);
 settings.timezoneString[63] = '\0';
 // Imported string has no database index. Mark as custom (255) so the
 // boot-time auto-heal in settings.cpp does not overwrite it from a
 // stale timezoneIndex left over from a previous dropdown selection.
 settings.timezoneIndex = 255;
 }
 }
 // Legacy: import old gmtOffset/dst if timezoneString is not provided
 if (!doc["gmtOffset"].isNull()) settings.gmtOffset = doc["gmtOffset"];
 if (!doc["daylightSaving"].isNull()) settings.daylightSaving = doc["daylightSaving"];
 if (!doc["use24Hour"].isNull()) settings.use24Hour = doc["use24Hour"];
 if (!doc["dateFormat"].isNull()) settings.dateFormat = doc["dateFormat"];
 if (!doc["clockPosition"].isNull()) settings.clockPosition = doc["clockPosition"];
 if (!doc["clockOffset"].isNull()) settings.clockOffset = doc["clockOffset"];
 if (!doc["showClock"].isNull()) settings.showClock = doc["showClock"];
 if (!doc["displayRowMode"].isNull()) settings.displayRowMode = doc["displayRowMode"];
 if (!doc["useRpmKFormat"].isNull()) settings.useRpmKFormat = doc["useRpmKFormat"];
 if (!doc["useNetworkMBFormat"].isNull()) settings.useNetworkMBFormat = doc["useNetworkMBFormat"];
 if (!doc["showIPAtBoot"].isNull()) settings.showIPAtBoot = doc["showIPAtBoot"];
 if (!doc["deviceName"].isNull()) {
   const char* name = doc["deviceName"];
   if (name && strlen(name) > 0 && strlen(name) <= 31) {
     strncpy(settings.deviceName, name, 31);
     settings.deviceName[31] = '\0';
   }
 }

 // Import metric labels
 if (!doc["metricLabels"].isNull()) {
 JsonArray labels = doc["metricLabels"];
 for (int i = 0; i < MAX_METRICS && i < labels.size(); i++) {
 const char* label = labels[i];
 if (label) {
 strncpy(settings.metricLabels[i], label, METRIC_NAME_LEN - 1);
 settings.metricLabels[i][METRIC_NAME_LEN - 1] = '\0';
 }
 }
 }

 // Import metric names
 if (!doc["metricNames"].isNull()) {
 JsonArray names = doc["metricNames"];
 for (int i = 0; i < MAX_METRICS && i < names.size(); i++) {
 const char* name = names[i];
 if (name) {
 strncpy(settings.metricNames[i], name, METRIC_NAME_LEN - 1);
 settings.metricNames[i][METRIC_NAME_LEN - 1] = '\0';
 }
 }
 }

 // Import metric order
 if (!doc["metricOrder"].isNull()) {
 JsonArray order = doc["metricOrder"];
 for (int i = 0; i < MAX_METRICS && i < order.size(); i++) {
 settings.metricOrder[i] = order[i];
 }
 }

 // Import metric companions
 if (!doc["metricCompanions"].isNull()) {
 JsonArray companions = doc["metricCompanions"];
 for (int i = 0; i < MAX_METRICS && i < companions.size(); i++) {
 settings.metricCompanions[i] = companions[i];
 }
 }

 // Import metric positions
 if (!doc["metricPositions"].isNull()) {
 JsonArray positions = doc["metricPositions"];
 for (int i = 0; i < MAX_METRICS && i < positions.size(); i++) {
 settings.metricPositions[i] = positions[i];
 }
 }

 // Import progress bar settings
 if (!doc["metricBarPositions"].isNull()) {
 JsonArray barPositions = doc["metricBarPositions"];
 for (int i = 0; i < MAX_METRICS && i < barPositions.size(); i++) {
 settings.metricBarPositions[i] = barPositions[i];
 }
 }

 if (!doc["metricBarMin"].isNull()) {
 JsonArray barMin = doc["metricBarMin"];
 for (int i = 0; i < MAX_METRICS && i < barMin.size(); i++) {
 settings.metricBarMin[i] = barMin[i];
 }
 }

 if (!doc["metricBarMax"].isNull()) {
 JsonArray barMax = doc["metricBarMax"];
 for (int i = 0; i < MAX_METRICS && i < barMax.size(); i++) {
 settings.metricBarMax[i] = barMax[i];
 }
 }

 if (!doc["metricBarWidths"].isNull()) {
 JsonArray barWidths = doc["metricBarWidths"];
 for (int i = 0; i < MAX_METRICS && i < barWidths.size(); i++) {
 settings.metricBarWidths[i] = barWidths[i];
 }
 }

 if (!doc["metricBarOffsets"].isNull()) {
 JsonArray barOffsets = doc["metricBarOffsets"];
 for (int i = 0; i < MAX_METRICS && i < barOffsets.size(); i++) {
 settings.metricBarOffsets[i] = barOffsets[i];
 }
 }

 // Hide out-of-range positions based on imported display mode
 {
   int maxPos;
   if (settings.displayRowMode == 0) maxPos = 10;
   else if (settings.displayRowMode == 1) maxPos = 12;
   else if (settings.displayRowMode == 2) maxPos = 2;
   else maxPos = 3;
   for (int i = 0; i < MAX_METRICS; i++) {
     if (settings.metricPositions[i] != 255 && settings.metricPositions[i] >= maxPos) {
       settings.metricPositions[i] = 255;
     }
     if (settings.metricBarPositions[i] != 255 && settings.metricBarPositions[i] >= maxPos) {
       settings.metricBarPositions[i] = 255;
     }
   }
 }

 // Save imported settings
 saveSettings();
 applyTimezone();
 ntpSynced = false; // Force NTP resync after config import

 // Imported config can change clockStyle. Reset every clock's animation
 // state so a previous in-flight animation doesn't carry stale time
 // override + queue residue into the new style.
 resetClockAnimationState();

 server.sendHeader("Access-Control-Allow-Origin", "*");
 server.send(200, "application/json", "{\"success\":true,\"message\":\"Configuration imported successfully\"}");
 } else {
 server.sendHeader("Access-Control-Allow-Origin", "*");
 server.send(400, "application/json", "{\"success\":false,\"message\":\"No data received\"}");
 }
}
