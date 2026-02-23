/*
 * SmallOLED-PCMonitor - Web Server Module
 *
 * Web server handlers for configuration interface.
 */

#ifndef WEB_H
#define WEB_H

#include "../config/config.h"
#include <Update.h>
#include <WebServer.h>


#include "../config/settings.h"

// Global web server object
extern WebServer server;

// ========== Web Server Functions ==========

// Initialize web server with all routes
void setupWebServer();

// Web handlers
void handleRoot();
void handleSave();
void handleReset();
void handleMetricsAPI();
void handleDeviceInfo();
void handleRename();
void handleExportConfig();
void handleImportConfig();

#endif // WEB_H
