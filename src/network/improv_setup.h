/*
 * SmallOLED-PCMonitor - Improv-Serial WiFi Provisioning (Web Flasher)
 *
 * Non-blocking Improv-Serial setup window used by the browser web flasher
 * (docs/). After flashing, ESP Web Tools probes the device for
 * Improv-Serial and presents a "Configure WiFi" dialog right in the browser
 * tab - the user picks their network and the credentials are pushed over USB.
 *
 * The window runs cooperatively while the WiFiManager AP captive portal stays
 * up in parallel, so users who don't catch the browser dialog still get the
 * PCMonitor-Setup hotspot fallback without any extra wait.
 *
 * Enabled via IMPROV_SETUP_ENABLED in src/config/user_config.h.
 *
 * Typical lifecycle from initNetwork() (non-blocking WiFiManager portal):
 *   wifiManager.setConfigPortalBlocking(false);
 *   if (!wifiManager.autoConnect(...)) {
 *     if (!wifiManager.getWiFiIsSaved()) improvSetupBegin(IMPROV_SETUP_WINDOW_MS);
 *     while (!connected && wifiManager.getConfigPortalActive()) {
 *       if (wifiManager.process()) { connected = true; break; }
 *       if (improvSetupTick()) ESP.restart();   // creds saved by callback
 *     }
 *     improvSetupEnd();
 *   }
 */

#ifndef IMPROV_SETUP_H
#define IMPROV_SETUP_H

#include "../config/user_config.h"

#if IMPROV_SETUP_ENABLED

#include <Arduino.h>

// Arm a Serial Improv listener for `windowMs` milliseconds. Pump
// improvSetupTick() from the portal loop; the AP captive portal stays up in
// parallel so legacy / hotspot users keep AP access without any extra wait.
void improvSetupBegin(uint32_t windowMs);

// Pump the Improv parser. Returns true exactly once - when valid WiFi
// credentials have been received AND a STA connection succeeded. The
// credentials are already persisted to the ESP WiFi NVS at that point (so
// WiFiManager.autoConnect() finds them next boot); the caller is expected to
// ESP.restart() so the device boots cleanly into STA mode.
bool improvSetupTick();

// True once the setup window has elapsed without success. Callers should then
// improvSetupEnd() to release resources; the AP stays up regardless.
bool improvSetupExpired();

void improvSetupEnd();

#endif // IMPROV_SETUP_ENABLED
#endif // IMPROV_SETUP_H
