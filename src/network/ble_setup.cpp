/*
 * SmallOLED-PCMonitor - BLE WiFi Provisioning
 *
 * GATT service UUIDs (SmallOLED provisioning service):
 *   Service:   4FAFC201-1FB5-459E-8FCC-C5C9C3319100
 *   SSID:      4FAFC201-1FB5-459E-8FCC-C5C9C3319101  WRITE
 *   Password:  4FAFC201-1FB5-459E-8FCC-C5C9C3319102  WRITE
 *   Command:   4FAFC201-1FB5-459E-8FCC-C5C9C3319103  WRITE  (0x01 = connect)
 *   Status:    4FAFC201-1FB5-459E-8FCC-C5C9C3319104  READ+NOTIFY
 *     Status bytes: 0x00=idle, 0x01=connecting, 0x02=success(+4 IP bytes), 0x03=failed, 0x04=timeout
 */

#include "ble_setup.h"

#if BLE_SETUP_ENABLED

#include <NimBLEDevice.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include "../display/display.h"
#include "../config/user_config.h"

extern bool displayAvailable;

// ========== UUIDs ==========
#define BLE_SERVICE_UUID     "4FAFC201-1FB5-459E-8FCC-C5C9C3319100"
#define BLE_CHAR_SSID_UUID   "4FAFC201-1FB5-459E-8FCC-C5C9C3319101"
#define BLE_CHAR_PASS_UUID   "4FAFC201-1FB5-459E-8FCC-C5C9C3319102"
#define BLE_CHAR_CMD_UUID    "4FAFC201-1FB5-459E-8FCC-C5C9C3319103"
#define BLE_CHAR_STATUS_UUID "4FAFC201-1FB5-459E-8FCC-C5C9C3319104"

// ========== Status byte values ==========
#define BLE_STATUS_IDLE       0x00
#define BLE_STATUS_CONNECTING 0x01
#define BLE_STATUS_SUCCESS    0x02
#define BLE_STATUS_FAILED     0x03
#define BLE_STATUS_TIMEOUT    0x04

// ========== Shared state (written from BLE callbacks, read from main loop) ==========
static char ble_ssid[33]     = {0};
static char ble_password[65] = {0};
static volatile bool connect_triggered = false;
static NimBLECharacteristic* pStatusChar = nullptr;

// ========== Status notification helpers ==========
static void sendStatus(uint8_t status) {
    if (!pStatusChar) return;
    pStatusChar->setValue(&status, 1);
    pStatusChar->notify();
}

static void sendStatusWithIP(uint8_t status, IPAddress ip) {
    if (!pStatusChar) return;
    uint8_t data[5] = { status, ip[0], ip[1], ip[2], ip[3] };
    pStatusChar->setValue(data, 5);
    pStatusChar->notify();
}

// ========== GATT characteristic callbacks ==========
class SsidCallback : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* pChar) override {
        std::string val = pChar->getValue();
        memset(ble_ssid, 0, sizeof(ble_ssid));
        strncpy(ble_ssid, val.c_str(), sizeof(ble_ssid) - 1);
        Serial.printf("[BLE] SSID received: %s\n", ble_ssid);
    }
};

class PasswordCallback : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* pChar) override {
        std::string val = pChar->getValue();
        memset(ble_password, 0, sizeof(ble_password));
        strncpy(ble_password, val.c_str(), sizeof(ble_password) - 1);
        Serial.println("[BLE] Password received");
    }
};

class CommandCallback : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* pChar) override {
        std::string val = pChar->getValue();
        if (!val.empty() && (uint8_t)val[0] == 0x01) {
            connect_triggered = true;
            Serial.println("[BLE] Connect command received");
        }
    }
};

// ========== OLED display helpers (BLE-specific screens) ==========
static void displayBleSetup() {
    if (!displayAvailable) return;
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(14, 0);
    display.println("Bluetooth Setup");
    display.drawLine(0, 10, 128, 10, DISPLAY_WHITE);
    display.setCursor(0, 14);
    display.println("Open SmallOLED app");
    display.setCursor(0, 26);
    display.println("Tap \"Add Device\"");
    display.setCursor(0, 38);
    display.print("BT: ");
    display.println(BLE_DEVICE_NAME);
    display.setCursor(0, 52);
    display.println("Waiting...");
    display.display();
}

static void displayBleWaiting(uint8_t dots) {
    if (!displayAvailable) return;
    // Redraw only the bottom line to avoid full-screen flicker
    display.fillRect(0, 50, 128, 14, DISPLAY_BLACK);
    display.setCursor(0, 52);
    display.print("Waiting");
    for (uint8_t i = 0; i < dots; i++) display.print(".");
    display.display();
}

static void displayBleConnecting(const char* ssid) {
    if (!displayAvailable) return;
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(10, 0);
    display.println("Connecting WiFi");
    display.drawLine(0, 10, 128, 10, DISPLAY_WHITE);
    display.setCursor(0, 16);
    display.println(ssid);
    display.setCursor(0, 40);
    display.println("Please wait...");
    display.display();
}

static void displayBleFailed() {
    if (!displayAvailable) return;
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(20, 10);
    display.println("BLE Setup Failed");
    display.setCursor(0, 30);
    display.println("Switching to AP mode");
    display.setCursor(0, 48);
    display.print("AP: ");
    display.println(AP_NAME);
    display.display();
}

// ========== tryConnectSavedWiFi ==========
bool tryConnectSavedWiFi() {
    WiFi.mode(WIFI_STA);

    // Read saved credentials from ESP32's internal WiFi NVS storage
    wifi_config_t conf;
    memset(&conf, 0, sizeof(conf));
    if (esp_wifi_get_config(WIFI_IF_STA, &conf) != ESP_OK) {
        Serial.println("[BLE] Could not read WiFi config");
        return false;
    }

    if (strlen((char*)conf.sta.ssid) == 0) {
        Serial.println("[BLE] No saved WiFi credentials");
        return false;
    }

    Serial.printf("[BLE] Trying saved WiFi: %s\n", (char*)conf.sta.ssid);

    if (displayAvailable) {
        display.clearDisplay();
        display.setTextSize(1);
        display.setCursor(10, 16);
        display.println("Connecting...");
        display.setCursor(10, 32);
        display.println((char*)conf.sta.ssid);
        display.display();
    }

    WiFi.begin(); // Use saved credentials

    for (int i = 0; i < 15; i++) {
        if (WiFi.status() == WL_CONNECTED) {
            Serial.println("[BLE] Connected with saved credentials!");
            return true;
        }
        delay(1000);
    }

    WiFi.disconnect(true);
    Serial.println("[BLE] Saved credentials failed, starting BLE provisioning");
    return false;
}

// ========== runBleProvisioning ==========
bool runBleProvisioning() {
    Serial.println("[BLE] Starting BLE provisioning");

    // Reset shared state
    memset(ble_ssid, 0, sizeof(ble_ssid));
    memset(ble_password, 0, sizeof(ble_password));
    connect_triggered = false;

    displayBleSetup();

    // Initialize NimBLE
    NimBLEDevice::init(BLE_DEVICE_NAME);
    NimBLEDevice::setPower(ESP_PWR_LVL_P9); // Max TX power for range

    // Create GATT server and service
    NimBLEServer*  pServer  = NimBLEDevice::createServer();
    NimBLEService* pService = pServer->createService(BLE_SERVICE_UUID);

    // SSID (write, no response for speed)
    NimBLECharacteristic* pSsidChar = pService->createCharacteristic(
        BLE_CHAR_SSID_UUID,
        NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
    );
    pSsidChar->setCallbacks(new SsidCallback());

    // Password (write, no response)
    NimBLECharacteristic* pPassChar = pService->createCharacteristic(
        BLE_CHAR_PASS_UUID,
        NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
    );
    pPassChar->setCallbacks(new PasswordCallback());

    // Command (write with response so app can confirm it was received)
    NimBLECharacteristic* pCmdChar = pService->createCharacteristic(
        BLE_CHAR_CMD_UUID,
        NIMBLE_PROPERTY::WRITE
    );
    pCmdChar->setCallbacks(new CommandCallback());

    // Status (read + notify — app subscribes before sending credentials)
    pStatusChar = pService->createCharacteristic(
        BLE_CHAR_STATUS_UUID,
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY
    );
    uint8_t idle = BLE_STATUS_IDLE;
    pStatusChar->setValue(&idle, 1);

    pService->start();

    // Start advertising with service UUID so app can filter by service
    NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(BLE_SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    pAdvertising->start();

    Serial.printf("[BLE] Advertising as \"%s\"\n", BLE_DEVICE_NAME);

    // --- Phase 1: Wait for credentials + connect command ---
    const unsigned long WAIT_TIMEOUT_MS = 180000; // 3 minutes
    unsigned long startWait = millis();
    unsigned long lastDisplayUpdate = 0;
    uint8_t dotCount = 0;

    while (!connect_triggered) {
        if (millis() - startWait > WAIT_TIMEOUT_MS) {
            Serial.println("[BLE] Provisioning timeout — falling back to AP mode");
            sendStatus(BLE_STATUS_TIMEOUT);
            delay(500);
            pAdvertising->stop();
            NimBLEDevice::deinit(true);
            pStatusChar = nullptr;
            displayBleFailed();
            delay(2000);
            return false;
        }

        // Animated dots on OLED every 1.5 seconds
        if (millis() - lastDisplayUpdate > 1500) {
            lastDisplayUpdate = millis();
            displayBleWaiting(dotCount);
            dotCount = (dotCount + 1) % 4;
        }

        delay(50);
    }

    // --- Phase 2: Connect to WiFi with received credentials ---
    Serial.printf("[BLE] Connecting to WiFi: \"%s\"\n", ble_ssid);
    sendStatus(BLE_STATUS_CONNECTING);
    displayBleConnecting(ble_ssid);

    WiFi.mode(WIFI_STA);
    WiFi.begin(ble_ssid, ble_password);
    // ESP32 WiFi stack automatically saves credentials to NVS on successful connect

    const unsigned long CONNECT_TIMEOUT_MS = 30000; // 30 seconds
    unsigned long startConnect = millis();

    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - startConnect > CONNECT_TIMEOUT_MS) {
            Serial.println("[BLE] WiFi connection timeout");
            sendStatus(BLE_STATUS_FAILED);
            delay(1500);
            WiFi.disconnect(true);
            pAdvertising->stop();
            NimBLEDevice::deinit(true);
            pStatusChar = nullptr;
            displayBleFailed();
            delay(2000);
            return false;
        }
        sendStatus(BLE_STATUS_CONNECTING);
        delay(500);
    }

    // --- Success ---
    IPAddress ip = WiFi.localIP();
    Serial.printf("[BLE] WiFi connected! IP: %s\n", ip.toString().c_str());
    sendStatusWithIP(BLE_STATUS_SUCCESS, ip);

    delay(2000); // Give app time to receive IP before BLE goes away

    pAdvertising->stop();
    NimBLEDevice::deinit(true); // Fully release BLE resources — returns ~25KB RAM
    pStatusChar = nullptr;

    return true;
}

#endif // BLE_SETUP_ENABLED
