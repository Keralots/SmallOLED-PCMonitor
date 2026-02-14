/*
 * SmallOLED-PCMonitor - Web Server Module
 *
 * Web server handlers for configuration interface.
 * Extracted from PCMonitor_WifiPortal.cpp
 */

#include "web.h"
#include "../config/config.h"
#include "../network/network.h"
#include "../utils/utils.h"
#include "../clocks/clocks.h"
#include "../display/display.h"
#include "../timezones.h"
#include <WebServer.h>
#include <Update.h>
#include <ArduinoJson.h>
#include <WiFi.h>

// ========== Web Server Object ==========
WebServer server(80);

// ========== Web Server Setup ==========
void setupWebServer() {
 server.on("/", handleRoot);
 server.on("/save", HTTP_POST, handleSave);
 server.on("/reset", handleReset);
 server.on("/metrics", handleMetricsAPI); // New API endpoint
 server.on("/api/export", HTTP_GET, handleExportConfig);
 server.on("/api/import", HTTP_POST, handleImportConfig);

 // OTA Firmware Update handlers
 server.on("/update", HTTP_POST, []() {
 server.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
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
   obj["displayOrder"] = m.displayOrder;
   obj["companionId"] = m.companionId;
   obj["position"] = m.position;
   obj["barPosition"] = m.barPosition;
   obj["barMin"] = m.barMin;
   obj["barMax"] = m.barMax;
   obj["barWidth"] = m.barWidth;
   obj["barOffsetX"] = m.barOffsetX;
 }

 String json;
 serializeJson(doc, json);
 server.send(200, "application/json", json);
}

void handleRoot() {
 // Generate hour options for scheduled dimming
 String startHourOptions = "";
 String endHourOptions = "";
 for (int i = 0; i < 24; i++) {
 String hourStr = String(i) + ":00";
 startHourOptions += "<option value=\"" + String(i) + "\"" +
 (settings.dimStartHour == i ? " selected" : "") + ">" +
 hourStr + "</option>";
 endHourOptions += "<option value=\"" + String(i) + "\"" +
 (settings.dimEndHour == i ? " selected" : "") + ">" +
 hourStr + "</option>";
 }

 String html = R"rawliteral(
<!DOCTYPE html><html><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width, initial-scale=1"><title>Mini OLED Configurator v)rawliteral" + String(FIRMWARE_VERSION) + R"rawliteral(</title><style> *{box-sizing:border-box}body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Arial,sans-serif;margin:0;padding:20px;background:linear-gradient(135deg,#0f0c29 0%,#1a1a2e 50%,#24243e 100%);background-attachment:fixed;color:#e0e7ff;min-height:100vh}.container{max-width:420px;margin:0 auto;padding-bottom:100px}h1{color:#fff;text-align:center;font-size:28px;font-weight:700;margin:0 0 8px;text-shadow:0 2px 10px rgba(0,212,255,.3)}.card{background:rgba(22,33,62,.6);backdrop-filter:blur(10px);padding:20px;border-radius:12px;margin-bottom:15px;border:1px solid rgba(0,212,255,.15);box-shadow:0 4px 15px rgba(0,0,0,.2)}label{display:block;margin:15px 0 8px;color:#00d4ff;font-size:14px;font-weight:500;letter-spacing:.3px}select,input[type="number"],input[type="text"]{width:100%;padding:12px 14px;border:2px solid rgba(0,212,255,.2);border-radius:8px;background:rgba(15,52,96,.5);color:#fff;font-size:15px;transition:all .3s ease;cursor:pointer}select:hover,input[type="number"]:hover,input[type="text"]:hover{border-color:rgba(0,212,255,.4);background:rgba(15,52,96,.7)}select:focus,input:focus{outline:none;border-color:#00d4ff;background:rgba(15,52,96,.8);box-shadow:0 0 0 3px rgba(0,212,255,.1)}input[type="checkbox"]{appearance:none;width:20px;height:20px;border:2px solid rgba(0,212,255,.4);border-radius:5px;background:rgba(15,52,96,.5);cursor:pointer;position:relative;transition:all .3s ease;flex-shrink:0}input[type="checkbox"]:hover{border-color:#00d4ff;transform:scale(1.05)}input[type="checkbox"]:checked{background:linear-gradient(135deg,#00d4ff 0%,#0096ff 100%);border-color:#00d4ff}input[type="checkbox"]:checked::after{content:'✓';position:absolute;color:#0f0c29;font-size:14px;font-weight:bold;top:50%;left:50%;transform:translate(-50%,-50%)}button{width:100%;padding:14px;margin-top:20px;border:none;border-radius:8px;font-size:16px;font-weight:600;cursor:pointer;transition:all .3s ease;text-transform:uppercase;letter-spacing:.5px}.save-btn{background:linear-gradient(135deg,#00d4ff 0%,#0096ff 100%);color:#0f0c29;box-shadow:0 4px 15px rgba(0,212,255,.3)}.save-btn:hover{transform:translateY(-2px);box-shadow:0 6px 20px rgba(0,212,255,.4)}.save-btn:active{transform:translateY(0)}.reset-btn{background:linear-gradient(135deg,#ff6b6b 0%,#ee5a52 100%);color:#fff;box-shadow:0 4px 15px rgba(255,107,107,.2)}.reset-btn:hover{transform:translateY(-2px);box-shadow:0 6px 20px rgba(255,107,107,.3)}.reset-btn:active{transform:translateY(0)}.info{text-align:center;color:#94a3b8;font-size:12px;margin-top:20px}.status{background:rgba(15,52,96,.4);padding:12px;border-radius:10px;text-align:center;margin-bottom:20px;border:1px solid rgba(0,212,255,.2);font-size:14px}.section-header{background:linear-gradient(135deg,rgba(15,52,96,.6) 0%,rgba(26,77,122,.4) 100%);padding:16px 18px;border-radius:10px;cursor:pointer;margin-bottom:10px;user-select:none;display:flex;justify-content:space-between;align-items:center;border:1px solid rgba(0,212,255,.15);transition:all .3s ease}.section-header:hover{background:linear-gradient(135deg,rgba(15,52,96,.8) 0%,rgba(26,77,122,.6) 100%);transform:translateX(4px);border-color:rgba(0,212,255,.3)}.section-header h3{margin:0;color:#00d4ff;font-size:16px;font-weight:600}.section-arrow{font-size:14px;transition:transform .3s ease;color:#00d4ff}.section-arrow.collapsed{transform:rotate(-90deg)}.section-content{max-height:10000px;overflow:visible;transition:max-height .3s ease,opacity .3s ease;opacity:1}.section-content.collapsed{max-height:0;overflow:hidden;opacity:0}.config-buttons{display:grid;grid-template-columns:1fr 1fr;gap:10px;margin-bottom:20px}.export-btn{background:linear-gradient(135deg,#10b981 0%,#059669 100%);color:#fff;padding:12px;font-size:14px;margin-top:0;border-radius:8px;font-weight:600;box-shadow:0 4px 12px rgba(16,185,129,.2);transition:all .3s ease}.export-btn:hover{transform:translateY(-2px);box-shadow:0 6px 16px rgba(16,185,129,.3)}.import-btn{background:linear-gradient(135deg,#3b82f6 0%,#2563eb 100%);color:#fff;padding:12px;font-size:14px;margin-top:0;border-radius:8px;font-weight:600;box-shadow:0 4px 12px rgba(59,130,246,.2);transition:all .3s ease}.import-btn:hover{transform:translateY(-2px);box-shadow:0 6px 16px rgba(59,130,246,.3)}.sticky-save{position:fixed;bottom:0;left:0;right:0;background:linear-gradient(to top,rgba(15,12,41,.98) 0%,rgba(15,12,41,.95) 100%);backdrop-filter:blur(10px);padding:12px 20px;box-shadow:0 -4px 20px rgba(0,0,0,.4);z-index:1000;border-top:1px solid rgba(0,212,255,.2)}.sticky-save .container{max-width:420px;margin:0 auto;padding-bottom:0}.sticky-save button{margin-top:0}#importFile{display:none}@media (max-width:480px){body{padding:12px}.container{padding-bottom:90px}h1{font-size:24px}.card{padding:16px}.section-header{padding:14px 16px}.section-header h3{font-size:15px}select,input[type="number"],input[type="text"]{font-size:16px;padding:11px 12px}button{padding:13px;font-size:15px}.sticky-save{padding:10px 12px}}@media (max-width:360px){h1{font-size:22px}.config-buttons{grid-template-columns:1fr;gap:8px}}</style></head><body><div class="container"><h1>&#128421; Mini OLED Configurator <span style="font-size: 0.5em; font-weight: normal;">v)rawliteral" + String(FIRMWARE_VERSION) + R"rawliteral(</span></h1><div class="status"><strong>IP:</strong> )rawliteral" + WiFi.localIP().toString() + R"rawliteral( | <strong>UDP Port:</strong> 4210
 </div><!-- Config Management --><div class="config-buttons"><button type="button" class="export-btn" onclick="exportConfig()">&#128190; Export Config</button><button type="button" class="import-btn" onclick="document.getElementById('importFile').click()">&#128229; Import Config</button></div><input type="file" id="importFile" accept=".json" onchange="importConfig(event)"><form action="/save" method="POST"><!-- Clock Settings Section --><div class="section-header" onclick="toggleSection('clockSection')"><h3>&#128348; Clock Settings</h3><span class="section-arrow">&#9660;</span></div><div id="clockSection" class="section-content collapsed"><div class="card"><label for="clockStyle">Idle Clock Style</label><select name="clockStyle" id="clockStyle" onchange="toggleMarioSettings()"><option value="0" )rawliteral" + String(settings.clockStyle == 0 ? "selected" : "") + R"rawliteral(>Mario Animation</option><option value="1" )rawliteral" + String(settings.clockStyle == 1 ? "selected" : "") + R"rawliteral(>Standard Clock</option><option value="2" )rawliteral" + String(settings.clockStyle == 2 ? "selected" : "") + R"rawliteral(>Large Clock</option><option value="3" )rawliteral" + String(settings.clockStyle == 3 ? "selected" : "") + R"rawliteral(>Space Invaders</option><option value="5" )rawliteral" + String(settings.clockStyle == 5 ? "selected" : "") + R"rawliteral(>Arkanoid</option><option value="6" )rawliteral" + String(settings.clockStyle == 6 ? "selected" : "") + R"rawliteral(>Pac-Man Clock</option></select><!-- Mario Clock Settings (only visible when Mario is selected) --><div id="marioSettings" style="display: )rawliteral" + String(settings.clockStyle == 0 ? "block" : "none") + R"rawliteral(; margin-top: 20px; padding: 15px; background-color: #1a1a2e; border-radius: 8px; border: 1px solid #3b82f6;"><h4 style="color: #3b82f6; margin-top: 0; font-size: 14px;">&#127922; Mario Animation Settings</h4><label for="marioBounceHeight">Bounce Height</label><input type="range" name="marioBounceHeight" id="marioBounceHeight"
 min="10" max="50" step="5"
 value=")rawliteral" + String(settings.marioBounceHeight) + R"rawliteral("
 oninput="document.getElementById('bounceHeightValue').textContent = (this.value / 10).toFixed(1)"><span style="color: #3b82f6; font-size: 14px; margin-left: 10px;"><span id="bounceHeightValue">)rawliteral" + String(settings.marioBounceHeight / 10.0, 1) + R"rawliteral(</span></span><p style="color: #888; font-size: 11px;">
 How high digits bounce when Mario hits them.</p><label for="marioBounceSpeed" style="margin-top: 15px;">Fall Speed</label><input type="range" name="marioBounceSpeed" id="marioBounceSpeed"
 min="2" max="15" step="1"
 value=")rawliteral" + String(settings.marioBounceSpeed) + R"rawliteral("
 oninput="document.getElementById('bounceSpeedValue').textContent = (this.value / 10).toFixed(1)"><span style="color: #3b82f6; font-size: 14px; margin-left: 10px;"><span id="bounceSpeedValue">)rawliteral" + String(settings.marioBounceSpeed / 10.0, 1) + R"rawliteral(</span></span><p style="color: #888; font-size: 12px; margin-top: 5px;">
 How fast digits fall back down. Higher = faster fall. Default: 0.6
 </p><label for="marioWalkSpeed" style="margin-top: 15px;">Walk Speed</label><input type="range" name="marioWalkSpeed" id="marioWalkSpeed"
 min="15" max="35" step="1"
 value=")rawliteral" + String(settings.marioWalkSpeed) + R"rawliteral("
 oninput="document.getElementById('walkSpeedValue').textContent = (this.value / 10).toFixed(1)"><span style="color: #3b82f6; font-size: 14px; margin-left: 10px;"><span id="walkSpeedValue">)rawliteral" + String(settings.marioWalkSpeed / 10.0, 1) + R"rawliteral(</span></span><p style="color: #888; font-size: 12px; margin-top: 5px;">
 How fast Mario walks. Higher = faster. Default: 2.0
 </p><div style="margin-top: 15px;"><label style="display: flex; align-items: center; cursor: pointer;"><input type="checkbox" name="marioSmoothAnimation" id="marioSmoothAnimation"
 )rawliteral" + String(settings.marioSmoothAnimation ? "checked" : "") + R"rawliteral(
 style="margin-right: 10px; width: 18px; height: 18px;"><span>Smooth Animation (4-frame walk cycle)</span></label><p style="color: #888; font-size: 11px;">
 Enable smoother 4-frame walking animation.</p></div></div><!-- Arkanoid Clock Settings (only visible when Arkanoid is selected) --><div id="pongSettings" style="display: )rawliteral" + String(settings.clockStyle == 5 ? "block" : "none") + R"rawliteral(; margin-top: 20px; padding: 15px; background-color: #1a1a2e; border-radius: 8px; border: 1px solid #3b82f6;"><h4 style="color: #3b82f6; margin-top: 0; font-size: 14px;">🎮 Arkanoid Animation Settings</h4><label for="pongBallSpeed">Ball Speed</label><input type="range" name="pongBallSpeed" id="pongBallSpeed"
 min="16" max="30" step="1"
 value=")rawliteral" + String(settings.pongBallSpeed) + R"rawliteral("
 oninput="document.getElementById('ballSpeedValue').textContent = this.value"><span style="color: #3b82f6; font-size: 14px; margin-left: 10px;"><span id="ballSpeedValue">)rawliteral" + String(settings.pongBallSpeed) + R"rawliteral(</span></span><p style="color: #888; font-size: 11px;">
 How fast the ball moves.</p><label for="pongBounceStrength" style="margin-top: 15px;">Bounce Strength</label><input type="range" name="pongBounceStrength" id="pongBounceStrength"
 min="1" max="8" step="1"
 value=")rawliteral" + String(settings.pongBounceStrength) + R"rawliteral("
 oninput="document.getElementById('bounceStrengthValue').textContent = (this.value / 10).toFixed(1)"><span style="color: #3b82f6; font-size: 14px; margin-left: 10px;"><span id="bounceStrengthValue">)rawliteral" + String(settings.pongBounceStrength / 10.0, 1) + R"rawliteral(</span></span><p style="color: #888; font-size: 11px;">
 How much digits wobble when hit.</p><label for="pongBounceDamping" style="margin-top: 15px;">Bounce Damping</label><input type="range" name="pongBounceDamping" id="pongBounceDamping"
 min="50" max="95" step="5"
 value=")rawliteral" + String(settings.pongBounceDamping) + R"rawliteral("
 oninput="document.getElementById('bounceDampingValue').textContent = (this.value / 100).toFixed(2)"><span style="color: #3b82f6; font-size: 14px; margin-left: 10px;"><span id="bounceDampingValue">)rawliteral" + String(settings.pongBounceDamping / 100.0, 2) + R"rawliteral(</span></span><p style="color: #888; font-size: 11px;">
 How quickly wobble stops.</p><label for="pongPaddleWidth" style="margin-top: 15px;">Paddle Width</label><input type="range" name="pongPaddleWidth" id="pongPaddleWidth"
 min="10" max="40" step="2"
 value=")rawliteral" + String(settings.pongPaddleWidth) + R"rawliteral("
 oninput="document.getElementById('paddleWidthValue').textContent = this.value"><span style="color: #3b82f6; font-size: 14px; margin-left: 10px;"><span id="paddleWidthValue">)rawliteral" + String(settings.pongPaddleWidth) + R"rawliteral(</span> px
 </span><p style="color: #888; font-size: 12px; margin-top: 5px;">
 Size of the paddle. Narrower = harder, Wider = easier. Default: 20px
 </p><label style="margin-top: 15px;"><input type="checkbox" name="pongHorizontalBounce"
 )rawliteral" + String(settings.pongHorizontalBounce ? "checked" : "") + R"rawliteral(>
 Horizontal Digit Bounce
 </label><p style="color: #888; font-size: 11px;">
 When enabled, digits bounce sideways when hit from the side.</p></div><!-- Pac-Man Clock Settings (only visible when Pac-Man is selected) --><div id="pacmanSettings" style="display: )rawliteral" + String(settings.clockStyle == 6 ? "block" : "none") + R"rawliteral(; margin-top: 20px; padding: 15px; background-color: #1a1a2e; border-radius: 8px; border: 1px solid #f1c40f;"><h4 style="color: #f1c40f; margin-top: 0; font-size: 14px;">👾 Pac-Man Clock Settings</h4><label for="pacmanSpeed">Patrol Speed</label><input type="range" name="pacmanSpeed" id="pacmanSpeed"
 min="5" max="30" step="1"
 value=")rawliteral" + String(settings.pacmanSpeed) + R"rawliteral("
 oninput="document.getElementById('pacmanSpeedValue').textContent = (this.value / 10).toFixed(1)"><span style="color: #f1c40f; font-size: 14px; margin-left: 10px;"><span id="pacmanSpeedValue">)rawliteral" + String(settings.pacmanSpeed / 10.0, 1) + R"rawliteral(</span> px/frame
 </span><p style="color: #888; font-size: 11px;">
 How fast Pac-Man moves during patrol (at bottom).</p><label for="pacmanEatingSpeed" style="margin-top: 15px;">Digit Eating Speed</label><input type="range" name="pacmanEatingSpeed" id="pacmanEatingSpeed"
 min="10" max="50" step="1"
 value=")rawliteral" + String(settings.pacmanEatingSpeed) + R"rawliteral("
 oninput="document.getElementById('pacmanEatingSpeedValue').textContent = (this.value / 10).toFixed(1)"><span style="color: #f1c40f; font-size: 14px; margin-left: 10px;"><span id="pacmanEatingSpeedValue">)rawliteral" + String(settings.pacmanEatingSpeed / 10.0, 1) + R"rawliteral(</span> px/frame
 </span><p style="color: #888; font-size: 11px;">
 How fast Pac-Man eats digits.</p><label for="pacmanMouthSpeed" style="margin-top: 15px;">Mouth Animation Speed</label><input type="range" name="pacmanMouthSpeed" id="pacmanMouthSpeed"
 min="5" max="20" step="1"
 value=")rawliteral" + String(settings.pacmanMouthSpeed) + R"rawliteral("
 oninput="document.getElementById('pacmanMouthSpeedValue').textContent = (this.value / 10).toFixed(1)"><span style="color: #f1c40f; font-size: 14px; margin-left: 10px;"><span id="pacmanMouthSpeedValue">)rawliteral" + String(settings.pacmanMouthSpeed / 10.0, 1) + R"rawliteral(</span> Hz
 </span><p style="color: #888; font-size: 12px; margin-top: 5px;">
 How fast Pac-Man's mouth opens and closes (waka-waka). Default: 1.0 Hz
 </p><label for="pacmanPelletCount" style="margin-top: 15px;">Number of Pellets</label><input type="range" name="pacmanPelletCount" id="pacmanPelletCount"
 min="0" max="20" step="1"
 value=")rawliteral" + String(settings.pacmanPelletCount) + R"rawliteral("
 oninput="document.getElementById('pacmanPelletCountValue').textContent = this.value"><span style="color: #f1c40f; font-size: 14px; margin-left: 10px;"><span id="pacmanPelletCountValue">)rawliteral" + String(settings.pacmanPelletCount) + R"rawliteral(</span></span><p style="color: #888; font-size: 11px;">
 How many pellets appear during patrol mode.</p><label style="margin-top: 15px;"><input type="checkbox" name="pacmanPelletRandomSpacing"
 )rawliteral" + String(settings.pacmanPelletRandomSpacing ? "checked" : "") + R"rawliteral(>
 Randomize Pellet Spacing
 </label><p style="color: #888; font-size: 11px;">
 When enabled, pellets appear at random positions.</p><label style="margin-top: 15px;"><input type="checkbox" name="pacmanBounceEnabled"
 )rawliteral" + String(settings.pacmanBounceEnabled ? "checked" : "") + R"rawliteral(>
 Bounce Animation for New Digits
 </label><p style="color: #888; font-size: 11px;">
 When enabled, new digits bounce into place after being eaten.</p></div><!-- Space Clock Settings (visible when Invader or Ship is selected) --><div id="spaceSettings" style="display: )rawliteral" + String((settings.clockStyle == 3 || settings.clockStyle == 4) ? "block" : "none") + R"rawliteral(; margin-top: 20px; padding: 15px; background-color: #1a1a2e; border-radius: 8px; border: 1px solid #3b82f6;"><h4 style="color: #3b82f6; margin-top: 0; font-size: 14px;">🚀 Space Clock Animation Settings</h4><label for="spaceCharacterType">Character Type</label><select name="spaceCharacterType" id="spaceCharacterType"><option value="0" )rawliteral" + String(settings.spaceCharacterType == 0 ? "selected" : "") + R"rawliteral(>Space Invader</option><option value="1" )rawliteral" + String(settings.spaceCharacterType == 1 ? "selected" : "") + R"rawliteral(>Space Ship (Default)</option></select><p style="color: #888; font-size: 11px;">
 Choose the character that patrols and attacks the time digits.</p><label for="spacePatrolSpeed" style="margin-top: 15px;">Patrol Speed</label><input type="range" name="spacePatrolSpeed" id="spacePatrolSpeed"
 min="2" max="15" step="1"
 value=")rawliteral" + String(settings.spacePatrolSpeed) + R"rawliteral("
 oninput="document.getElementById('patrolSpeedValue').textContent = (this.value / 10).toFixed(1)"><span style="color: #3b82f6; font-size: 14px; margin-left: 10px;"><span id="patrolSpeedValue">)rawliteral" + String(settings.spacePatrolSpeed / 10.0, 1) + R"rawliteral(</span></span><p style="color: #888; font-size: 11px;">
 How fast the character drifts during patrol.</p><label for="spaceAttackSpeed" style="margin-top: 15px;">Attack Speed</label><input type="range" name="spaceAttackSpeed" id="spaceAttackSpeed"
 min="10" max="40" step="5"
 value=")rawliteral" + String(settings.spaceAttackSpeed) + R"rawliteral("
 oninput="document.getElementById('attackSpeedValue').textContent = (this.value / 10).toFixed(1)"><span style="color: #3b82f6; font-size: 14px; margin-left: 10px;"><span id="attackSpeedValue">)rawliteral" + String(settings.spaceAttackSpeed / 10.0, 1) + R"rawliteral(</span></span><p style="color: #888; font-size: 11px;">
 How fast the character slides to attack position.</p><label for="spaceLaserSpeed" style="margin-top: 15px;">Laser Speed</label><input type="range" name="spaceLaserSpeed" id="spaceLaserSpeed"
 min="20" max="80" step="5"
 value=")rawliteral" + String(settings.spaceLaserSpeed) + R"rawliteral("
 oninput="document.getElementById('laserSpeedValue').textContent = (this.value / 10).toFixed(1)"><span style="color: #3b82f6; font-size: 14px; margin-left: 10px;"><span id="laserSpeedValue">)rawliteral" + String(settings.spaceLaserSpeed / 10.0, 1) + R"rawliteral(</span></span><p style="color: #888; font-size: 11px;">
 How fast the laser extends downward.</p><label for="spaceExplosionGravity" style="margin-top: 15px;">Explosion Intensity</label><input type="range" name="spaceExplosionGravity" id="spaceExplosionGravity"
 min="3" max="10" step="1"
 value=")rawliteral" + String(settings.spaceExplosionGravity) + R"rawliteral("
 oninput="document.getElementById('explosionGravityValue').textContent = (this.value / 10).toFixed(1)"><span style="color: #3b82f6; font-size: 14px; margin-left: 10px;"><span id="explosionGravityValue">)rawliteral" + String(settings.spaceExplosionGravity / 10.0, 1) + R"rawliteral(</span></span><p style="color: #888; font-size: 11px;">
 Controls fragment gravity (how fast debris falls).</p></div><label for="use24Hour">Time Format</label><select name="use24Hour" id="use24Hour"><option value="1" )rawliteral" + String(settings.use24Hour ? "selected" : "") + R"rawliteral(>24-Hour (14:30)</option><option value="0" )rawliteral" + String(!settings.use24Hour ? "selected" : "") + R"rawliteral(>12-Hour (2:30 PM)</option></select><label for="dateFormat">Date Format</label><select name="dateFormat" id="dateFormat"><option value="0" )rawliteral" + String(settings.dateFormat == 0 ? "selected" : "") + R"rawliteral(>DD/MM/YYYY</option><option value="1" )rawliteral" + String(settings.dateFormat == 1 ? "selected" : "") + R"rawliteral(>MM/DD/YYYY</option><option value="2" )rawliteral" + String(settings.dateFormat == 2 ? "selected" : "") + R"rawliteral(>YYYY-MM-DD</option></select></div></div><!-- Display Settings Section --><div class="section-header" onclick="toggleSection('displayPerfSection')"><h3>&#9889; Display Settings</h3><span class="section-arrow">&#9660;</span></div><div id="displayPerfSection" class="section-content collapsed"><div class="card"><label for="colonBlinkMode">Clock Colon Display</label><select name="colonBlinkMode" id="colonBlinkMode"><option value="0" )rawliteral" + String(settings.colonBlinkMode == 0 ? "selected" : "") + R"rawliteral(>On</option><option value="1" )rawliteral" + String(settings.colonBlinkMode == 1 ? "selected" : "") + R"rawliteral(>Blinking</option><option value="2" )rawliteral" + String(settings.colonBlinkMode == 2 ? "selected" : "") + R"rawliteral(>Off</option></select><label for="colonBlinkRate">Blink Rate (Hz)</label><input type="range" name="colonBlinkRate" id="colonBlinkRate"
 min="5" max="50" step="5"
 value=")rawliteral" + String(settings.colonBlinkRate) + R"rawliteral("
 oninput="document.getElementById('blinkRateValue').textContent = (this.value / 10).toFixed(1)"><span style="color: #3b82f6; font-size: 14px; margin-left: 10px;"><span id="blinkRateValue">)rawliteral" + String(settings.colonBlinkRate / 10.0, 1) + R"rawliteral(</span> Hz
 </span><p style="color: #888; font-size: 12px; margin-top: 5px;">
 Blink speed. 1.0Hz = once/second.
 </p><label for="refreshRateMode" style="margin-top: 15px;">Refresh Rate Mode</label><select name="refreshRateMode" id="refreshRateMode" onchange="toggleRefreshRateFields()"><option value="0" )rawliteral" + String(settings.refreshRateMode == 0 ? "selected" : "") + R"rawliteral(>Auto</option><option value="1" )rawliteral" + String(settings.refreshRateMode == 1 ? "selected" : "") + R"rawliteral(>Manual</option></select><div id="refreshRateFields" style="display: )rawliteral" + String(settings.refreshRateMode == 1 ? "block" : "none") + R"rawliteral(;"><label for="refreshRateHz">Manual Refresh Rate (Hz)</label><input type="range" name="refreshRateHz" id="refreshRateHz"
 min="1" max="60" step="1"
 value=")rawliteral" + String(settings.refreshRateHz) + R"rawliteral("
 oninput="document.getElementById('refreshRateValue').textContent = this.value"><span style="color: #3b82f6; font-size: 14px; margin-left: 10px;"><span id="refreshRateValue">)rawliteral" + String(settings.refreshRateHz) + R"rawliteral(</span> Hz
 </span><p style="color: #888; font-size: 12px; margin-top: 5px;">
 Updates/second. Higher = smoother, more power.
 </p></div><div style="margin-top: 15px;"><label style="display: flex; align-items: center; cursor: pointer;"><input type="checkbox" name="boostAnim" id="boostAnim" style="margin-right: 10px;" )rawliteral" + String(settings.boostAnimationRefresh ? "checked" : "") + R"rawliteral(><span style="font-size: 14px;"><strong>Enable Smooth Animations</strong> (Boost refresh during action)
 </span></label></div><label for="displayBrightness" style="margin-top: 15px;">Display Brightness</label><input type="range" name="displayBrightness" id="displayBrightness"
 min="0" max="255" step="5"
 value=")rawliteral" + String(settings.displayBrightness) + R"rawliteral("
 oninput="document.getElementById('brightnessValue').textContent = Math.round((this.value / 255) * 100)"><span style="color: #3b82f6; font-size: 14px; margin-left: 10px;"><span id="brightnessValue">)rawliteral" + String((settings.displayBrightness * 100) / 255) + R"rawliteral(</span>%
 </span><p style="color: #888; font-size: 12px; margin-top: 5px;">
 Brightness control (0-100%). Display remains visible at 0%.
 </p><div style="margin-top: 15px;"><label style="display: flex; align-items: center; cursor: pointer;"><input type="checkbox" name="enableScheduledDimming" id="enableScheduledDimming" style="margin-right: 10px;" )rawliteral" + String(settings.enableScheduledDimming ? "checked" : "") + R"rawliteral( onchange="toggleScheduledDimming()"><span style="font-size: 14px;">
 <strong>&#127749; Scheduled Night Mode</strong>
 </span></label></div><div id="scheduledDimmingFields" style="display: )rawliteral" + String(settings.enableScheduledDimming ? "block" : "none") + R"rawliteral(; padding: 15px; background: #0f172a; border-radius: 8px; border: 1px solid #1e293b; margin-top: 10px;"><div style="display: grid; grid-template-columns: 1fr 1fr; gap: 15px; margin-bottom: 15px;"><div><label for="dimStartHour" style="font-size: 13px; color: #e2e8f0; display: block; margin-bottom: 5px;">Start Dimming At</label><select name="dimStartHour" id="dimStartHour" style="width: 100%; padding: 8px; background: #1e293b; border: 1px solid #334155; border-radius: 6px; color: #f1f5f9; font-size: 13px;">)rawliteral" + startHourOptions + R"rawliteral(</select></div><div><label for="dimEndHour" style="font-size: 13px; color: #e2e8f0; display: block; margin-bottom: 5px;">End Dimming At</label><select name="dimEndHour" id="dimEndHour" style="width: 100%; padding: 8px; background: #1e293b; border: 1px solid #334155; border-radius: 6px; color: #f1f5f9; font-size: 13px;">)rawliteral" + endHourOptions + R"rawliteral(</select></div></div><label for="dimBrightness" style="font-size: 13px; color: #e2e8f0; display: block; margin-bottom: 5px;">Dim Brightness Level</label><input type="range" name="dimBrightness" id="dimBrightness"
 min="0" max="255" step="5"
 value=")rawliteral" + String(settings.dimBrightness) + R"rawliteral("
 oninput="document.getElementById('dimBrightnessValue').textContent = Math.round((this.value / 255) * 100)"><span style="color: #818cf8; font-size: 14px; margin-left: 10px;"><span id="dimBrightnessValue">)rawliteral" + String((settings.dimBrightness * 100) / 255) + R"rawliteral(</span>%
 </span><p style="color: #94a3b8; font-size: 11px; margin-top: 5px;">
 Brightness level during scheduled dim period. Recommended: 10-20% for night use.
 </p></div><script> function toggleScheduledDimming(){const enabled=document.getElementById('enableScheduledDimming').checked;document.getElementById('scheduledDimmingFields').style.display=enabled ? 'block':'none';}</script><label for="ledBrightness" style="margin-top: 15px; display: block;">LED Night Light Brightness</label><input type="range" name="ledBrightness" id="ledBrightness"
 min="0" max="255" step="5"
 value=")rawliteral" + String(settings.ledBrightness) + R"rawliteral("
 oninput="document.getElementById('ledBrightnessValue').textContent = Math.round((this.value / 255) * 100)"><span style="color: #fbbf24; font-size: 14px; margin-left: 10px;"><span id="ledBrightnessValue">)rawliteral" + String((settings.ledBrightness * 100) / 255) + R"rawliteral(</span>%
 </span><p style="color: #888; font-size: 12px; margin-top: 5px;">
 LED brightness control (0-100%). Toggle via touch button long press (hold 1 second). This is optional feature and requires connected LED!
 </p><div style="margin-top: 15px; padding: 10px; background: #0f172a; border-radius: 5px; border-left: 3px solid #3b82f6;"><p style="color: #93c5fd; font-size: 12px; margin: 0;"><strong>&#128161; Refresh Rate Auto Mode:</strong> Adapts refresh rate based on content.<br>
 • Static Clocks: 2 Hz (saves power)<br>
 • Idle Animations: 20 Hz (character movement)<br>
 • Active Animations: 40 Hz (with boost enabled, during bounces/explosions)<br>
 • PC Metrics: 10 Hz (balanced)<br><br><strong>Benefits:</strong> Blinking colon extends OLED life 2×. Dynamic refresh rates balance smoothness with power efficiency.
 </p></div></div></div><!-- Timezone Section --><div class="section-header" onclick="toggleSection('timezoneSection')"><h3>&#127760; Timezone</h3><span class="section-arrow">&#9660;</span></div><div id="timezoneSection" class="section-content collapsed"><div class="card"><label for="timezoneRegion">Timezone Region</label><select name="timezoneRegion" id="timezoneRegion" style="width: 100%; padding: 8px; background: #16213e; border: 1px solid #334155; color: #eee; border-radius: 3px;">
)rawliteral";

 // Generate timezone options from timezone database
 size_t tzCount;
 const TimezoneRegion* regions = getSupportedTimezones(&tzCount);

 // Group timezones by region
 html += "<option value=\"\">-- Select Region --</option>\n";

 bool tzSelected = false;
 for (size_t i = 0; i < tzCount; i++) {
   bool isSelected = !tzSelected && (strcmp(settings.timezoneString, regions[i].posixString) == 0);
   if (isSelected) tzSelected = true;
   html += "<option value=\"" + String(regions[i].posixString) + "\"" + (isSelected ? " selected" : "") + ">" + String(regions[i].name) + "</option>\n";
 }

 html += R"rawliteral(
 </select><p style="color: #888; font-size: 12px; margin-top: 10px;">
 Select your timezone region for automatic DST adjustment. The system will automatically switch between standard and daylight saving time.
 </p></div></div><!-- Network Configuration Section --><div class="section-header" onclick="toggleSection('networkSection')"><h3>&#127760; Network Configuration</h3><span class="section-arrow">&#9660;</span></div><div id="networkSection" class="section-content collapsed"><div class="card"><label for="useStaticIP">IP Address Mode</label><select name="useStaticIP" id="useStaticIP" onchange="toggleStaticIPFields()"><option value="0" )rawliteral" + String(!settings.useStaticIP ? "selected" : "") + R"rawliteral(>DHCP (Automatic)</option><option value="1" )rawliteral" + String(settings.useStaticIP ? "selected" : "") + R"rawliteral(>Static IP</option></select><div id="staticIPFields" style="display: )rawliteral" + String(settings.useStaticIP ? "block" : "none") + R"rawliteral(;"><label for="staticIP" style="margin-top: 15px;">Static IP Address</label><input type="text" name="staticIP" id="staticIP" value=")rawliteral" + String(settings.staticIP) + R"rawliteral(" placeholder="192.168.1.100" pattern="^(?:[0-9]{1,3}\.){3}[0-9]{1,3}$"><label for="gateway">Gateway</label><input type="text" name="gateway" id="gateway" value=")rawliteral" + String(settings.gateway) + R"rawliteral(" placeholder="192.168.1.1" pattern="^(?:[0-9]{1,3}\.){3}[0-9]{1,3}$"><label for="subnet">Subnet Mask</label><input type="text" name="subnet" id="subnet" value=")rawliteral" + String(settings.subnet) + R"rawliteral(" placeholder="255.255.255.0" pattern="^(?:[0-9]{1,3}\.){3}[0-9]{1,3}$"><label for="dns1">Primary DNS</label><input type="text" name="dns1" id="dns1" value=")rawliteral" + String(settings.dns1) + R"rawliteral(" placeholder="8.8.8.8" pattern="^(?:[0-9]{1,3}\.){3}[0-9]{1,3}$"><label for="dns2">Secondary DNS</label><input type="text" name="dns2" id="dns2" value=")rawliteral" + String(settings.dns2) + R"rawliteral(" placeholder="8.8.4.4" pattern="^(?:[0-9]{1,3}\.){3}[0-9]{1,3}$"></div><p style="color: #888; font-size: 12px; margin-top: 15px; background: #0f172a; padding: 10px; border-radius: 5px; border-left: 3px solid #fbbf24;"><strong>&#9888; Warning:</strong> Changing to Static IP will require a device restart. Make sure the IP address does not conflict with other devices on your network.
 </p><hr style="margin: 20px 0; border: none; border-top: 1px solid #333;"><div style="display: flex; align-items: center; margin-top: 15px;"><input type="checkbox" name="showIPAtBoot" id="showIPAtBoot" value="1" )rawliteral" + String(settings.showIPAtBoot ? "checked" : "") + R"rawliteral( style="width: 20px; margin: 0;"><label for="showIPAtBoot" style="margin: 0 0 0 10px; text-align: left; color: #00d4ff;">Show IP address on display at startup (5 seconds)</label></div></div></div><!-- Display Layout Section --><div class="section-header" onclick="toggleSection('layoutSection')"><h3>&#128202; Display Layout (PC Monitor only)</h3><span class="section-arrow">&#9660;</span></div><div id="layoutSection" class="section-content collapsed"><div class="card"><label for="clockPosition">Clock Position</label><select name="clockPosition" id="clockPosition"><option value="0" )rawliteral" + String(settings.clockPosition == 0 ? "selected" : "") + R"rawliteral(>Center (Top)</option><option value="1" )rawliteral" + String(settings.clockPosition == 1 ? "selected" : "") + R"rawliteral(>Left Column (Row 1)</option><option value="2" )rawliteral" + String(settings.clockPosition == 2 ? "selected" : "") + R"rawliteral(>Right Column (Row 1)</option></select><label for="clockOffset" style="margin-top: 15px; display: block;">Clock Offset (pixels)</label><input type="number" name="clockOffset" id="clockOffset" value=")rawliteral" + String(settings.clockOffset) + R"rawliteral(" min="-20" max="20" style="width: 100%; padding: 8px; box-sizing: border-box;"><p style="color: #888; font-size: 12px; margin-top: 10px;">
 Position clock to optimize space for metrics. Use offset to fine-tune horizontal position (-20 to +20 pixels).
 </p><div style="display: flex; align-items: center; margin-top: 15px;"><input type="checkbox" name="showClock" id="showClock" value="1" )rawliteral" + String(settings.showClock ? "checked" : "") + R"rawliteral( style="width: 20px; margin: 0;"><label for="showClock" style="margin: 0 0 0 10px; text-align: left; color: #00d4ff;">Show Clock/Time in metrics display</label></div><hr style="margin: 20px 0; border: none; border-top: 1px solid #333;"><label for="rowMode">Display Row Mode</label><select name="rowMode" id="rowMode" onchange="updateRowMode()"><option value="0" )rawliteral" + String(settings.displayRowMode == 0 ? "selected" : "") + R"rawliteral(>5 Rows (13px spacing - optimized)</option><option value="1" )rawliteral" + String(settings.displayRowMode == 1 ? "selected" : "") + R"rawliteral(>6 Rows (10px spacing - compact)</option><option value="2" )rawliteral" + String(settings.displayRowMode == 2 ? "selected" : "") + R"rawliteral(>Large 2-Row (double size text)</option><option value="3" )rawliteral" + String(settings.displayRowMode == 3 ? "selected" : "") + R"rawliteral(>Large 3-Row (double size text)</option></select><p style="color: #888; font-size: 12px; margin-top: 10px;">
 5-row and 6-row modes use small text in 2-column layout. Large modes use double-size text in single-column layout for better readability at a distance.
 </p><div style="margin-top: 20px;"><label><input type="checkbox" name="rpmKFormat" id="rpmKFormat" )rawliteral" + String(settings.useRpmKFormat ? "checked" : "") + R"rawliteral(>
 Use K-format for RPM values (e.g., 1.8K instead of 1800RPM)
 </label><p style="color: #888; font-size: 12px; margin-top: 5px;">
 Applies to all fan and pump speed metrics with RPM unit.
 </p></div><div style="margin-top: 20px;"><label><input type="checkbox" name="netMBFormat" id="netMBFormat" )rawliteral" + String(settings.useNetworkMBFormat ? "checked" : "") + R"rawliteral(>
 Use M-format for network speeds (e.g., 1.2M instead of 1200KB/s)
 </label><p style="color: #888; font-size: 12px; margin-top: 5px;">
 Applies to all network speed metrics with KB/s unit.
 </p></div></div></div><!-- Visible Metrics Section --><div class="section-header" onclick="toggleSection('metricsSection')"><h3>&#128195; Visible Metrics (PC Monitor only)</h3><span class="section-arrow">&#9660;</span></div><div id="metricsSection" class="section-content collapsed"><div class="card"><p style="color: #888; font-size: 14px; margin-top: 0; text-align: left;">
 Select which metrics to show on OLED
 </p><p style="color: #888; font-size: 12px; margin-top: 10px; background: #0f172a; padding: 10px; border-radius: 5px; border-left: 3px solid #00d4ff;"><strong>&#128161; Tip:</strong> Use <code style="background: #1e293b; padding: 2px 6px; border-radius: 3px;">^</code> character for spacing.<br>
 Example: <code style="background: #1e293b; padding: 2px 6px; border-radius: 3px;">CPU^^</code> displays as <code style="background: #1e293b; padding: 2px 6px; border-radius: 3px;">CPU: 45C</code> (2 spaces after colon)
 </p><div id="metricsContainer"><p style="color: #888;">Loading metrics...</p></div><p style="color: #888; font-size: 12px; margin-top: 15px;"><strong>Note:</strong> Metrics are configured in Python script.<br>
 Select up to 20 in pc_stats_monitor_v2.py (use companion metrics to fit more)
 </p><script> let metricsData=[];let MAX_ROWS=)rawliteral"+String(settings.displayRowMode==0?5:settings.displayRowMode==1?6:settings.displayRowMode==2?2:3)+R"rawliteral(;let IS_LARGE_MODE=)rawliteral"+String(settings.displayRowMode>=2?"true":"false")+R"rawliteral(;function saveFormState(){metricsData.forEach(metric=>{const labelInput=document.querySelector(`input[name="label_${metric.id}"]`);if(labelInput){metric.label=labelInput.value;}const posDropdown=document.getElementById('pos_'+metric.id);if(posDropdown){metric.position=parseInt(posDropdown.value);}const compDropdown=document.getElementById('comp_'+metric.id);if(compDropdown){metric.companionId=parseInt(compDropdown.value);}const barPosDropdown=document.getElementById('barPos_'+metric.id);if(barPosDropdown){metric.barPosition=parseInt(barPosDropdown.value);}const barMinInput=document.querySelector(`input[name="barMin_${metric.id}"]`);if(barMinInput){metric.barMin=parseInt(barMinInput.value)|| 0;}const barMaxInput=document.querySelector(`input[name="barMax_${metric.id}"]`);if(barMaxInput){metric.barMax=parseInt(barMaxInput.value)|| 100;}const barWidthInput=document.querySelector(`input[name="barWidth_${metric.id}"]`);if(barWidthInput){metric.barWidth=parseInt(barWidthInput.value)|| 60;}const barOffsetInput=document.querySelector(`input[name="barOffset_${metric.id}"]`);if(barOffsetInput){metric.barOffsetX=parseInt(barOffsetInput.value)|| 0;}});}function updatePosition(metricId){saveFormState();renderMetrics();}function updateCompanion(metricId){saveFormState();renderMetrics();}function updateRowMode(){const rowMode=parseInt(document.getElementById('rowMode').value);const oldMaxRows=MAX_ROWS;const oldLargeMode=IS_LARGE_MODE;IS_LARGE_MODE=(rowMode>=2);MAX_ROWS=(rowMode===0)?5:(rowMode===1)?6:(rowMode===2)?2:3;const maxPos=IS_LARGE_MODE?MAX_ROWS:MAX_ROWS*2;const oldMaxPos=oldLargeMode?oldMaxRows:oldMaxRows*2;if(maxPos<oldMaxPos){const hiddenMetrics=metricsData.filter(m=>(m.position!==255&&m.position>=maxPos)||(m.barPosition!==255&&m.barPosition>=maxPos));if(hiddenMetrics.length>0){const names=hiddenMetrics.map(m=>m.name).join(', ');if(!confirm(`Warning: ${hiddenMetrics.length} metric(s) (${names}) will be hidden. Continue?`)){IS_LARGE_MODE=oldLargeMode;MAX_ROWS=oldMaxRows;if(oldLargeMode){document.getElementById('rowMode').value=oldMaxRows===2?'2':'3';}else{document.getElementById('rowMode').value=oldMaxRows===5?'0':'1';}return;}}metricsData.forEach(metric=>{if(metric.position!==255&&metric.position>=maxPos){metric.position=255;}if(metric.barPosition!==255&&metric.barPosition>=maxPos){metric.barPosition=255;}});}renderMetrics();}function renderMetrics(){const container=document.getElementById('metricsContainer');container.innerHTML='';const sortedMetrics=[...metricsData].sort((a,b)=>a.displayOrder-b.displayOrder);const header=document.createElement('div');header.style.cssText='background:#1e293b;padding:12px;border-radius:6px;margin-bottom:15px;border:2px solid #00d4ff;';header.innerHTML=`<div style="color:#00d4ff;font-weight:bold;font-size:14px;margin-bottom:5px;">&#128247;OLED Display Preview (`+MAX_ROWS+` Rows${IS_LARGE_MODE?' - Large Text, Single Column':' - 2 Columns'})</div><div style="color:#888;font-size:12px;">Assign each metric to a specific position using the dropdown</div>`;container.appendChild(header);for(let rowIndex=0;rowIndex<MAX_ROWS;rowIndex++){const rowDiv=document.createElement('div');rowDiv.style.cssText='background:#0f172a;border:1px solid #334155;border-radius:6px;margin-bottom:10px;overflow:hidden;';const rowHeader=document.createElement('div');rowHeader.style.cssText='background:#1e293b;padding:6px 10px;color:#00d4ff;font-weight:bold;font-size:12px;border-bottom:1px solid #334155;';rowHeader.textContent=`Row ${rowIndex+1}`;rowDiv.appendChild(rowHeader);if(IS_LARGE_MODE){const metric=sortedMetrics.find(m=>m.position===rowIndex)||null;const rowContent=document.createElement('div');rowContent.style.cssText='background:#0f172a;padding:15px;min-height:60px;';if(metric){const companionName=metric.companionId>0?(metricsData.find(m=>m.id===metric.companionId)?.name||'Unknown'):'None';rowContent.innerHTML=`<div><div style="color:#00d4ff;font-weight:bold;font-size:15px;margin-bottom:2px;">${metric.name} (Large Text)</div><div style="color:#888;font-size:11px;">Label: ${metric.label||metric.name}</div>${metric.companionId>0?`<div style="color:#888;font-size:11px;">Paired with: ${companionName}</div>`:''}</div>`;}else{rowContent.innerHTML='<div style="color:#555;font-size:12px;text-align:center;padding:10px;">Empty<br><span style="font-size:10px;">No metric assigned</span></div>';}rowDiv.appendChild(rowContent);}else{const leftPos=rowIndex*2;const rightPos=rowIndex*2+1;const leftMetric=sortedMetrics.find(m=>m.position===leftPos)||null;const rightMetric=sortedMetrics.find(m=>m.position===rightPos)||null;const rowContent=document.createElement('div');rowContent.style.cssText='display:grid;grid-template-columns:1fr 1fr;gap:1px;background:#334155;';const leftSlot=createMetricSlot(leftMetric,'left',leftPos);rowContent.appendChild(leftSlot);const rightSlot=createMetricSlot(rightMetric,'right',rightPos);rowContent.appendChild(rightSlot);rowDiv.appendChild(rowContent);}container.appendChild(rowDiv);}const metricsListDiv=document.createElement('div');metricsListDiv.style.cssText='background:#1e293b;border:1px solid #334155;border-radius:6px;padding:15px;margin-top:20px;';metricsListDiv.innerHTML='<div style="color:#00d4ff;font-weight:bold;font-size:14px;margin-bottom:10px;">&#9881;All Metrics Configuration</div>';sortedMetrics.forEach(metric=>{const metricDiv=createMetricConfig(metric);metricsListDiv.appendChild(metricDiv);});container.appendChild(metricsListDiv);}function createMetricSlot(metric,side,position){const slot=document.createElement('div');slot.style.cssText='background:#0f172a;padding:15px;min-height:60px;';if(!metric){slot.innerHTML=`<div style="color:#555;font-size:12px;text-align:center;padding:10px;">${side==='left' ? '&#8592;':'&#8594;'}Empty<br><span style="font-size:10px;">No metric assigned</span></div>`;return slot;}const companionName=metric.companionId>0 ?(metricsData.find(m=>m.id===metric.companionId)?.name || 'Unknown'):'None';slot.innerHTML=`<div style="margin-bottom:4px;"><div style="color:#00d4ff;font-weight:bold;font-size:13px;margin-bottom:2px;">${metric.name}</div><div style="color:#888;font-size:10px;">Label:${metric.label || metric.name}</div>${metric.companionId>0 ? `<div style="color:#888;font-size:10px;">Paired with:${companionName}</div>`:''}</div>`;return slot;}function createMetricConfig(metric){const div=document.createElement('div');div.style.cssText='background:#0f172a;padding:12px;border-radius:6px;margin-bottom:8px;border:1px solid #334155;';let positionOptions='<option value="255">None(Hidden)</option>';if(IS_LARGE_MODE){for(let row=0;row<MAX_ROWS;row++){positionOptions+=`<option value="${row}" ${metric.position===row?'selected':''}>Row ${row+1}</option>`;}}else{for(let row=0;row<MAX_ROWS;row++){const leftPos=row*2;const rightPos=row*2+1;positionOptions+=`<option value="${leftPos}" ${metric.position===leftPos?'selected':''}>Row ${row+1}-&#8592;Left</option>`;positionOptions+=`<option value="${rightPos}" ${metric.position===rightPos?'selected':''}>Row ${row+1}-Right &#8594;</option>`;}}let barPositionOptions='<option value="255">None</option>';if(IS_LARGE_MODE){for(let row=0;row<MAX_ROWS;row++){barPositionOptions+=`<option value="${row}" ${metric.barPosition===row?'selected':''}>Row ${row+1}</option>`;}}else{for(let row=0;row<MAX_ROWS;row++){const leftPos=row*2;const rightPos=row*2+1;barPositionOptions+=`<option value="${leftPos}" ${metric.barPosition===leftPos?'selected':''}>Row ${row+1}-&#8592;Left</option>`;barPositionOptions+=`<option value="${rightPos}" ${metric.barPosition===rightPos?'selected':''}>Row ${row+1}-Right &#8594;</option>`;}}let companionOptions='<option value="0">None</option>';metricsData.forEach(m=>{if(m.id !==metric.id){const selected=(metric.companionId===m.id)? 'selected':'';companionOptions+=`<option value="${m.id}" ${selected}>${m.name}(${m.unit})</option>`;}});div.innerHTML=`<div style="margin-bottom:8px;"><div style="color:#00d4ff;font-weight:bold;font-size:13px;">${metric.name}(${metric.unit})</div></div><div style="display:grid;grid-template-columns:1fr 1fr;gap:10px;"><div><label style="color:#888;font-size:10px;display:block;margin-bottom:3px;">Position:</label><select name="position_${metric.id}" id="pos_${metric.id}" onchange="updatePosition(${metric.id})" style="width:100%;padding:6px;background:#16213e;border:1px solid #334155;color:#eee;border-radius:3px;font-size:11px;">${positionOptions}</select></div><div><label style="color:#888;font-size:10px;display:block;margin-bottom:3px;">Pair with:</label><select name="companion_${metric.id}" id="comp_${metric.id}" onchange="updateCompanion(${metric.id})" style="width:100%;padding:6px;background:#16213e;border:1px solid #334155;color:#eee;border-radius:3px;font-size:11px;">${companionOptions}</select></div></div><div style="margin-top:8px;"><label style="color:#888;font-size:10px;display:block;margin-bottom:3px;">Custom Label(10 chars max):</label><input type="text" name="label_${metric.id}" value="${metric.label}" maxlength="10" placeholder="${metric.name}" style="width:100%;padding:6px;background:#16213e;border:1px solid #334155;color:#eee;border-radius:3px;font-size:11px;box-sizing:border-box;"></div><div style="margin-top:10px;padding-top:8px;border-top:1px solid #334155;"><label style="color:#888;font-size:10px;display:block;margin-bottom:3px;">Progress Bar Position:</label><select name="barPosition_${metric.id}" id="barPos_${metric.id}" style="width:100%;padding:6px;background:#16213e;border:1px solid #334155;color:#eee;border-radius:3px;font-size:11px;margin-bottom:8px;">${barPositionOptions}</select><div style="display:grid;grid-template-columns:1fr 1fr;gap:10px;"><div><label style="color:#888;font-size:9px;display:block;margin-bottom:2px;">Min Value:</label><input type="number" name="barMin_${metric.id}" value="${metric.barMin || 0}" style="width:100%;padding:4px;background:#16213e;border:1px solid #334155;color:#eee;border-radius:3px;font-size:10px;box-sizing:border-box;"></div><div><label style="color:#888;font-size:9px;display:block;margin-bottom:2px;">Max Value:</label><input type="number" name="barMax_${metric.id}" value="${metric.barMax || 100}" style="width:100%;padding:4px;background:#16213e;border:1px solid #334155;color:#eee;border-radius:3px;font-size:10px;box-sizing:border-box;"></div></div><div style="display:grid;grid-template-columns:1fr 1fr;gap:10px;margin-top:8px;"><div><label style="color:#888;font-size:9px;display:block;margin-bottom:2px;">Width(px):</label><input type="number" name="barWidth_${metric.id}" value="${metric.barWidth || 60}" min="10" max="64" style="width:100%;padding:4px;background:#16213e;border:1px solid #334155;color:#eee;border-radius:3px;font-size:10px;box-sizing:border-box;"></div><div><label style="color:#888;font-size:9px;display:block;margin-bottom:2px;">Offset X(px):</label><input type="number" name="barOffset_${metric.id}" value="${metric.barOffsetX || 0}" min="0" max="54" style="width:100%;padding:4px;background:#16213e;border:1px solid #334155;color:#eee;border-radius:3px;font-size:10px;box-sizing:border-box;"></div></div></div><input type="hidden" name="order_${metric.id}" value="${metric.displayOrder}">`;return div;}fetch('/metrics').then(res=>res.json()).then(data=>{if(data.metrics && data.metrics.length>0){metricsData=data.metrics;renderMetrics();}else{document.getElementById('metricsContainer').innerHTML='<p style="color:#ff6666;">No metrics received yet. Start Python script.</p>';}}).catch(err=>{document.getElementById('metricsContainer').innerHTML='<p style="color:#ff6666;">Error loading metrics</p>';});</script></div></div></form><!-- Firmware Update Section (Outside main form) --><div class="section-header" onclick="toggleSection('firmwareSection')"><h3>&#128190; Firmware Update</h3><span class="section-arrow">&#9660;</span></div><div id="firmwareSection" class="section-content collapsed"><div class="card"><p style="color: #888; font-size: 14px; margin-top: 0;">
 Upload new firmware (.bin file) to update the device
 </p><form id="uploadForm" method="POST" action="/update" enctype="multipart/form-data" style="margin-top: 15px;"><input type="file" id="firmwareFile" name="firmware" accept=".bin" style="width: 100%; padding: 10px; margin-bottom: 10px; background: #16213e; border: 1px solid #334155; color: #eee; border-radius: 5px;"><button type="submit" style="width: 100%; padding: 14px; background: linear-gradient(135deg, #f59e0b 0%, #d97706 100%); color: #fff; border: none; border-radius: 8px; font-size: 16px; font-weight: 600; cursor: pointer; margin-top: 10px;">
 &#128190; Upload & Update Firmware
 </button></form><div id="uploadProgress" style="display: none; margin-top: 15px;"><div style="background: #1e293b; border-radius: 8px; overflow: hidden; height: 30px; margin-bottom: 10px;"><div id="progressBar" style="background: linear-gradient(135deg, #00d4ff 0%, #0096ff 100%); height: 100%; width: 0%; transition: width 0.3s; display: flex; align-items: center; justify-content: center; color: #0f0c29; font-weight: bold; font-size: 14px;">
 0%
 </div></div><p id="uploadStatus" style="text-align: center; color: #00d4ff; font-size: 14px;">Uploading...</p></div><p style="color: #888; font-size: 12px; margin-top: 15px; background: #0f172a; padding: 10px; border-radius: 5px; border-left: 3px solid #ef4444;"><strong>&#9888; Warning:</strong> Do not disconnect power during firmware update! Device will restart automatically after update completes.
 </p></div></div><form action="/reset" method="GET" onsubmit="return confirm('Reset WiFi settings? Device will restart in AP mode.');"><button type="submit" class="reset-btn">&#128260; Reset WiFi Settings</button></form></div><!-- Sticky Save Button --><div class="sticky-save"><div class="container"><button type="button" class="save-btn" onclick="saveSettings()">&#128190; Save Settings</button><span id="saveMessage" style="margin-left: 15px; color: #4CAF50; font-weight: bold; display: none;">&#10004; Settings Saved!</span></div></div><script> function toggleSection(sectionId){const content=document.getElementById(sectionId);const arrow=event.currentTarget.querySelector('.section-arrow');content.classList.toggle('collapsed');arrow.classList.toggle('collapsed');const isCollapsed=content.classList.contains('collapsed');if(!isCollapsed){localStorage.setItem('lastExpandedSection',sectionId);}}function toggleStaticIPFields(){const useStaticIP=document.getElementById('useStaticIP').value==='1';const staticIPFields=document.getElementById('staticIPFields');staticIPFields.style.display=useStaticIP ? 'block':'none';}function toggleRefreshRateFields(){const refreshRateMode=document.getElementById('refreshRateMode').value==='1';const refreshRateFields=document.getElementById('refreshRateFields');refreshRateFields.style.display=refreshRateMode ? 'block':'none';}function toggleMarioSettings(){const clockStyle=document.getElementById('clockStyle').value;const marioSettings=document.getElementById('marioSettings');const pongSettings=document.getElementById('pongSettings');const pacmanSettings=document.getElementById('pacmanSettings');const spaceSettings=document.getElementById('spaceSettings');marioSettings.style.display=(clockStyle==='0')? 'block':'none';pongSettings.style.display=(clockStyle==='5')? 'block':'none';pacmanSettings.style.display=(clockStyle==='6')? 'block':'none';spaceSettings.style.display=(clockStyle==='3' || clockStyle==='4')? 'block':'none';}function exportConfig(){fetch('/api/export').then(response=>response.json()).then(data=>{const blob=new Blob([JSON.stringify(data,null,2)],{type:'application/json'});const url=URL.createObjectURL(blob);const a=document.createElement('a');a.href=url;a.download='pc-monitor-config.json';document.body.appendChild(a);a.click();document.body.removeChild(a);URL.revokeObjectURL(url);alert('Configuration exported successfully!');}).catch(err=>alert('Error exporting configuration:'+err));}function importConfig(event){const file=event.target.files[0];if(!file)return;const reader=new FileReader();reader.onload=function(e){try{const config=JSON.parse(e.target.result);fetch('/api/import',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(config)}).then(response=>response.json()).then(data=>{if(data.success){alert('Configuration imported successfully! Reloading page...');location.reload();}else{alert('Error importing configuration:'+data.message);}}).catch(err=>alert('Error importing configuration:'+err));}catch(err){alert('Invalid configuration file:'+err);}};reader.readAsText(file);}function saveSettings(){const form=document.querySelector('form[action="/save"]');const formData=new FormData(form);const saveMessage=document.getElementById('saveMessage');const saveBtn=document.querySelector('.save-btn');const urlEncoded=new URLSearchParams(formData);saveBtn.disabled=true;saveBtn.textContent='💾 Saving...';fetch('/save',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:urlEncoded}).then(response=>response.json()).then(data=>{if(data.success){saveMessage.style.display='inline';setTimeout(()=>{saveMessage.style.display='none';},3000);saveBtn.disabled=false;saveBtn.textContent='💾 Save Settings';if(data.networkChanged){alert('Network settings changed! Device is restarting. You may need to reconnect to the new IP address.');setTimeout(()=>{window.location.href='/';},3000);}}else{alert('Error saving settings');saveBtn.disabled=false;saveBtn.textContent='💾 Save Settings';}}).catch(err=>{alert('Error saving settings:'+err);saveBtn.disabled=false;saveBtn.textContent='💾 Save Settings';});}document.getElementById('uploadForm').addEventListener('submit',function(e){e.preventDefault();const fileInput=document.getElementById('firmwareFile');const file=fileInput.files[0];if(!file){alert('Please select a firmware file(.bin)');return;}if(!file.name.endsWith('.bin')){alert('Please select a valid .bin firmware file');return;}document.getElementById('uploadProgress').style.display='block';document.querySelector('#uploadForm button').disabled=true;const xhr=new XMLHttpRequest();xhr.upload.addEventListener('progress',function(e){if(e.lengthComputable){const percent=Math.round((e.loaded/e.total)*100);document.getElementById('progressBar').style.width=percent+'%';document.getElementById('progressBar').textContent=percent+'%';document.getElementById('uploadStatus').textContent='Uploading:'+percent+'%';}});xhr.addEventListener('load',function(){if(xhr.status===200){document.getElementById('progressBar').style.width='100%';document.getElementById('progressBar').textContent='100%';document.getElementById('uploadStatus').textContent='Update successful! Device is rebooting...';document.getElementById('uploadStatus').style.color='#10b981';setTimeout(function(){window.location.href='/';},8000);}else{document.getElementById('uploadStatus').textContent='Upload failed! Please try again.';document.getElementById('uploadStatus').style.color='#ef4444';document.querySelector('#uploadForm button').disabled=false;}});xhr.addEventListener('error',function(){document.getElementById('uploadStatus').textContent='Upload error! Please try again.';document.getElementById('uploadStatus').style.color='#ef4444';document.querySelector('#uploadForm button').disabled=false;});const formData=new FormData();formData.append('firmware',file);xhr.open('POST','/update');xhr.send(formData);});window.addEventListener('DOMContentLoaded',function(){toggleStaticIPFields();toggleRefreshRateFields();const lastExpandedSection=localStorage.getItem('lastExpandedSection');if(lastExpandedSection){const content=document.getElementById(lastExpandedSection);const headers=document.querySelectorAll('.section-header');if(content){for(let header of headers){if(header.getAttribute('onclick')&& header.getAttribute('onclick').includes(lastExpandedSection)){const arrow=header.querySelector('.section-arrow');content.classList.remove('collapsed');if(arrow)arrow.classList.remove('collapsed');break;}}}}});</script></body></html>
)rawliteral";

 // Send HTML in chunks to avoid buffer overflow
 // ESP32 WebServer has limited buffer, so we send in 4KB chunks
 const int chunkSize = 4096;
 int htmlLength = html.length();

 // Send headers first
 server.setContentLength(htmlLength);
 server.send(200, "text/html", "");

 // Send content in chunks
 int bytesSent = 0;
 while (bytesSent < htmlLength) {
 int bytesToSend = min(chunkSize, htmlLength - bytesSent);
 server.sendContent(html.substring(bytesSent, bytesSent + bytesToSend));
 bytesSent += bytesToSend;
 delay(2); // Small delay between chunks
 }
}

void handleSave() {
 if (server.hasArg("clockStyle")) {
 settings.clockStyle = server.arg("clockStyle").toInt();
 }

 // Handle new timezone region selector
 if (server.hasArg("timezoneRegion")) {
 String tz = server.arg("timezoneRegion");
 if (tz.length() > 0 && tz.length() < 64) {
 strncpy(settings.timezoneString, tz.c_str(), 63);
 settings.timezoneString[63] = '\0';

 // Update gmtOffset for backward compatibility
 const TimezoneRegion* region = findTimezoneByPosixString(settings.timezoneString);
 if (region != nullptr) {
 settings.gmtOffset = region->gmtOffsetMinutes;
 settings.daylightSaving = true; // POSIX strings include DST
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

 // Save display brightness
 if (server.hasArg("displayBrightness")) {
 uint8_t newBrightness = server.arg("displayBrightness").toInt();
 if (newBrightness != settings.displayBrightness) {
 settings.displayBrightness = newBrightness;
 applyDisplayBrightness(); // Apply immediately
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
 settings.enableScheduledDimming = server.hasArg("enableScheduledDimming");
 if (server.hasArg("dimStartHour")) {
 settings.dimStartHour = server.arg("dimStartHour").toInt();
 }
 if (server.hasArg("dimEndHour")) {
 settings.dimEndHour = server.arg("dimEndHour").toInt();
 }
 if (server.hasArg("dimBrightness")) {
 settings.dimBrightness = server.arg("dimBrightness").toInt();
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

 // Save network configuration
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
 assertBounds(settings.clockStyle, 0, 6, "clockStyle");
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

 saveSettings();
 applyTimezone();
 ntpSynced = false; // Force NTP resync after timezone change

 // Reset Mario animation state when switching modes
 mario_state = MARIO_IDLE;
 mario_x = -15;
 animation_triggered = false;
 time_overridden = false;
 last_minute = -1;

 // Reset Space clock animation state when switching modes
 space_state = SPACE_PATROL;
 space_x = 64; // Center of screen (space_y is const at 56)

 // Reset Pong animation state when switching modes
 resetPongAnimation();

 // Reset Pac-Man animation state when switching modes
 pacman_state = PACMAN_PATROL;
 pacman_x = 30.0;
 pacman_y = PACMAN_PATROL_Y;
 pacman_direction = 1;
 pacman_animation_triggered = false;
 last_minute_pacman = -1;
 for (int i = 0; i < 5; i++) {
 digit_being_eaten[i] = false;
 digit_eaten_rows_left[i] = 0;
 digit_eaten_rows_right[i] = 0;
 }
 generatePellets();

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
<!DOCTYPE html><html><head><title>Resetting...</title><style> body{font-family:Arial;background:#1a1a2e;color:#e94560;display:flex;justify-content:center;align-items:center;height:100vh;margin:0}.msg{text-align:center}</style></head><body><div class="msg"><h1>&#128260;</h1><p>Resetting WiFi settings...<br>Connect to "PCMonitor-Setup" to reconfigure.</p></div></body></html>
)rawliteral";

 // Send HTML (small page)
 server.send(200, "text/html", html);
 delay(1000);

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

 server.send(200, "application/json", "{\"success\":true,\"message\":\"Configuration imported successfully\"}");
 } else {
 server.send(400, "application/json", "{\"success\":false,\"message\":\"No data received\"}");
 }
}
