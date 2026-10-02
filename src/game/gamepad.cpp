/*
 * SmallOLED-PCMonitor - BLE Gamepad (Xbox Wireless Controller)
 *
 * Xbox BLE input report (16 bytes, little endian):
 *   0-7   LX LY RX RY  uint16, centre 0x8000, Y grows downward
 *   8-11  LT RT        uint16, 10-bit
 *   12    d-pad hat    0 = none, 1 = up, clockwise to 8 = up-left
 *   13    A B - X Y - LB RB
 *   14    - - View Menu Xbox LS RS
 *   15    Share (Series pads only)
 *
 * Rumble output report (8 bytes, written to the writable 0x2A4D report):
 *   enable (bit0 weak, bit1 strong, bit2 RT, bit3 LT), LT, RT, strong, weak
 *   magnitudes 0-100, duration and start delay in 10 ms, loop count.
 *
 * Every BLE operation runs in the gamepad task; rumble and forget requests
 * reach it through a queue, so nothing races a disconnect.
 */

#include "../config/user_config.h"

#if GAMEPAD_ENABLED

#include "gamepad.h"
#include <NimBLEDevice.h>
#include <WiFi.h>
#include <nvs.h>

#define GP_APPEARANCE_GAMEPAD 964
#define GP_STICK_DIGITAL 16000  // stick deflection that counts as a d-pad press
#define GP_TASK_STACK 6144
#define GP_BOND_NAMESPACE "nimble_bond"  // NimBLE's NVS store

static const NimBLEUUID UUID_HID((uint16_t)0x1812);
static const NimBLEUUID UUID_BATTERY((uint16_t)0x180f);
static const NimBLEUUID UUID_BATTERY_LEVEL((uint16_t)0x2a19);
static const NimBLEUUID UUID_REPORT((uint16_t)0x2a4d);

enum GpCmdType : uint8_t { GP_CMD_RUMBLE, GP_CMD_FORGET };
struct GpCmd {
  GpCmdType type;
  uint8_t report[8];
};

static portMUX_TYPE gpMux = portMUX_INITIALIZER_UNLOCKED;
static GamepadState gpState;
static volatile GamepadLink gpLink = GP_LINK_OFF;
static volatile bool gpWanted = false;
static volatile bool gpFound = false;
static volatile uint8_t gpBattery = 0;
static NimBLEAddress gpFoundAddr;
static NimBLEClient *gpClient = nullptr;
static TaskHandle_t gpTask = nullptr;
static QueueHandle_t gpCmdQ = nullptr;
static NimBLERemoteCharacteristic *gpOutput = nullptr;

static void clearState() {
  taskENTER_CRITICAL(&gpMux);
  memset(&gpState, 0, sizeof(gpState));
  taskEXIT_CRITICAL(&gpMux);
}

static void onReport(NimBLERemoteCharacteristic *, uint8_t *d, size_t len, bool) {
  if (len < 15) return;
  int16_t lx = (int32_t)(d[0] | d[1] << 8) - 32768;
  int16_t ly = (int32_t)(d[2] | d[3] << 8) - 32768;
  int16_t rx = (int32_t)(d[4] | d[5] << 8) - 32768;
  int16_t ry = (int32_t)(d[6] | d[7] << 8) - 32768;
  uint16_t lt = (d[8] | d[9] << 8) & 0x3ff;
  uint16_t rt = (d[10] | d[11] << 8) & 0x3ff;

  uint16_t b = 0;
  uint8_t hat = d[12] <= 8 ? d[12] : 0;
  if (hat == 8 || hat == 1 || hat == 2) b |= GP_UP;
  if (hat >= 2 && hat <= 4) b |= GP_RIGHT;
  if (hat >= 4 && hat <= 6) b |= GP_DOWN;
  if (hat >= 6 && hat <= 8) b |= GP_LEFT;
  if (lx < -GP_STICK_DIGITAL) b |= GP_LEFT;
  if (lx > GP_STICK_DIGITAL) b |= GP_RIGHT;
  if (ly < -GP_STICK_DIGITAL) b |= GP_UP;
  if (ly > GP_STICK_DIGITAL) b |= GP_DOWN;

  uint8_t m = d[13];
  if (m & 0x01) b |= GP_A;
  if (m & 0x02) b |= GP_B;
  if (m & 0x08) b |= GP_X;
  if (m & 0x10) b |= GP_Y;
  if (m & 0x40) b |= GP_LB;
  if (m & 0x80) b |= GP_RB;
  uint8_t c = d[14];
  if (c & 0x04) b |= GP_VIEW;
  if (c & 0x08) b |= GP_MENU;
  if (c & 0x10) b |= GP_XBOX;
  if (c & 0x20) b |= GP_LS;
  if (c & 0x40) b |= GP_RS;
  if (len > 15 && (d[15] & 0x01)) b |= GP_SHARE;

  taskENTER_CRITICAL(&gpMux);
  gpState.pressed |= b & ~gpState.buttons;
  gpState.buttons = b;
  gpState.lx = lx;
  gpState.ly = ly;
  gpState.rx = rx;
  gpState.ry = ry;
  gpState.lt = lt;
  gpState.rt = rt;
  gpState.hat = hat;
  taskEXIT_CRITICAL(&gpMux);
}

static void onBattery(NimBLERemoteCharacteristic *, uint8_t *d, size_t len, bool) {
  if (len) gpBattery = d[0];
}

class PadScanCallbacks : public NimBLEAdvertisedDeviceCallbacks {
  void onResult(NimBLEAdvertisedDevice *dev) override {
    if (gpFound) return;
    bool isPad = (dev->haveAppearance() && dev->getAppearance() == GP_APPEARANCE_GAMEPAD) ||
                 dev->getName().find("Xbox") != std::string::npos;
    uint8_t type = dev->getAdvType();
    bool bondedRecall = (type == BLE_HCI_ADV_TYPE_ADV_DIRECT_IND_HD ||
                         type == BLE_HCI_ADV_TYPE_ADV_DIRECT_IND_LD) &&
                        NimBLEDevice::isBonded(dev->getAddress());
    if (!isPad && !bondedRecall) return;
    Serial.printf("Gamepad: found %s \"%s\"\n", dev->getAddress().toString().c_str(),
                  dev->getName().c_str());
    gpFoundAddr = dev->getAddress();
    gpFound = true;
    NimBLEDevice::getScan()->stop();
  }
};

class PadClientCallbacks : public NimBLEClientCallbacks {
  void onDisconnect(NimBLEClient *) override {
    Serial.println("Gamepad: disconnected");
    if (gpLink == GP_LINK_CONNECTED) gpLink = GP_LINK_SCANNING;
    clearState();
  }
};

static PadScanCallbacks scanCallbacks;
static PadClientCallbacks clientCallbacks;

// The pad keeps blinking and sends nothing until the host has walked the
// whole service the way Windows does: every readable characteristic read
// (report map, HID info, reports), every notifiable one subscribed.
static bool subscribeAll(NimBLERemoteService *svc, notify_callback cb) {
  bool any = false;
  for (auto *c : *svc->getCharacteristics(true)) {
    if (c->getUUID() == UUID_REPORT && (c->canWrite() || c->canWriteNoResponse())) gpOutput = c;
    if (c->canRead()) c->readValue();
    if (c->canNotify() && c->subscribe(true, cb, true)) any = true;
  }
  return any;
}

static bool connectPad(const NimBLEAddress &addr) {
  if (!gpClient) {
    gpClient = NimBLEDevice::createClient();
    gpClient->setClientCallbacks(&clientCallbacks, false);
    gpClient->setConnectTimeout(5);
  }
  // 15-30 ms interval keeps input latency below a frame without starving WiFi.
  gpClient->setConnectionParams(12, 24, 0, 400);
  gpOutput = nullptr;
  if (!gpClient->connect(addr, true)) {
    Serial.println("Gamepad: connect failed");
    return false;
  }
  if (!gpClient->secureConnection()) {
    // Stale keys - the pad was paired elsewhere since. Forget it and re-pair.
    Serial.println("Gamepad: pairing failed, dropping bond");
    gpClient->disconnect();
    NimBLEDevice::deleteBond(addr);
    return false;
  }
  NimBLERemoteService *hid = gpClient->getService(UUID_HID);
  if (!hid || !subscribeAll(hid, onReport)) {
    Serial.println("Gamepad: no HID input report");
    gpClient->disconnect();
    return false;
  }
  NimBLERemoteService *bat = gpClient->getService(UUID_BATTERY);
  if (bat) {
    NimBLERemoteCharacteristic *lvl = bat->getCharacteristic(UUID_BATTERY_LEVEL);
    if (lvl && lvl->canRead()) gpBattery = lvl->readValue<uint8_t>();
    subscribeAll(bat, onBattery);
  }
  clearState();
  gpLink = GP_LINK_CONNECTED;
  Serial.printf("Gamepad: connected, battery %u%%\n", gpBattery);
  return true;
}

static void teardown() {
  NimBLEScan *scan = NimBLEDevice::getScan();
  if (scan->isScanning()) scan->stop();
  if (gpClient && gpClient->isConnected()) gpClient->disconnect();
  clearState();
  gpLink = GP_LINK_OFF;
}

static void bleUp() {
  // IDF aborts if BT starts while WiFi power save is off, and main.cpp turns
  // it off for snappy web transfers. Modem sleep only while the pad is in use.
  WiFi.setSleep(true);
  NimBLEDevice::init("");
  NimBLEDevice::setSecurityAuth(true, false, false);  // bond, no MITM: pads have no display
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);
  NimBLEScan *scan = NimBLEDevice::getScan();
  scan->setAdvertisedDeviceCallbacks(&scanCallbacks, false);
  scan->setActiveScan(true);
  scan->setInterval(97);
  scan->setWindow(48);  // half duty, so WiFi keeps the radio the other half
}

static void bleDown() {
  teardown();
  vTaskDelay(pdMS_TO_TICKS(200));  // let the disconnect finish
  NimBLEDevice::deinit(true);
  gpClient = nullptr;
  gpOutput = nullptr;
  WiFi.setSleep(false);
}

static void runCommand(const GpCmd &cmd) {
  bool connected = gpClient && gpClient->isConnected();
  if (cmd.type == GP_CMD_RUMBLE) {
    if (connected && gpOutput) gpOutput->writeValue(cmd.report, sizeof(cmd.report), false);
    return;
  }
  if (connected) gpClient->disconnect();
  NimBLEDevice::deleteAllBonds();
  Serial.println("Gamepad: bonds cleared");
}

static void gamepadTask(void *) {
  GpCmd cmd;
  for (;;) {
    while (!gpWanted) ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    bleUp();
    NimBLEScan *scan = NimBLEDevice::getScan();

    while (gpWanted) {
      if (gpClient && gpClient->isConnected()) {
        if (xQueueReceive(gpCmdQ, &cmd, pdMS_TO_TICKS(100))) runCommand(cmd);
        continue;
      }
      while (xQueueReceive(gpCmdQ, &cmd, 0)) runCommand(cmd);

      gpLink = GP_LINK_SCANNING;
      gpFound = false;
      scan->start(2, false);
      scan->clearResults();
      if (!gpWanted || !gpFound) continue;

      gpLink = GP_LINK_CONNECTING;
      if (!connectPad(gpFoundAddr)) {
        gpLink = GP_LINK_SCANNING;
        vTaskDelay(pdMS_TO_TICKS(500));
      }
    }
    bleDown();
    xQueueReset(gpCmdQ);
  }
}

void gamepadStart() {
  gpWanted = true;
  if (!gpTask) {
    gpCmdQ = xQueueCreate(4, sizeof(GpCmd));
    xTaskCreate(gamepadTask, "gamepad", GP_TASK_STACK, nullptr, 2, &gpTask);
  } else {
    xTaskNotifyGive(gpTask);
  }
}

void gamepadStop() {
  gpWanted = false;
  if (gpTask) xTaskNotifyGive(gpTask);
}

GamepadLink gamepadLink() { return gpLink; }

void gamepadRead(GamepadState *out) {
  taskENTER_CRITICAL(&gpMux);
  *out = gpState;
  gpState.pressed = 0;
  taskEXIT_CRITICAL(&gpMux);
}

uint8_t gamepadBattery() { return gpBattery; }

void gamepadRumble(uint8_t strong, uint8_t weak, uint16_t ms) {
  if (!gpCmdQ || gpLink != GP_LINK_CONNECTED) return;
  GpCmd cmd = {GP_CMD_RUMBLE,
               {(uint8_t)((weak ? 0x01 : 0) | (strong ? 0x02 : 0)), 0, 0,
                (uint8_t)min<int>(strong, 100), (uint8_t)min<int>(weak, 100),
                (uint8_t)min<int>(ms / 10, 255), 0, 0}};
  xQueueSend(gpCmdQ, &cmd, 0);  // drop it if the queue is full - rumble is a nicety
}

void gamepadForget() {
  if (gpWanted && gpCmdQ) {
    GpCmd cmd = {GP_CMD_FORGET, {}};
    xQueueSend(gpCmdQ, &cmd, pdMS_TO_TICKS(100));
    return;
  }
  // BLE is down, so NimBLE holds no cached copy - clear its store directly.
  nvs_handle_t h;
  if (nvs_open(GP_BOND_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
    nvs_erase_all(h);
    nvs_commit(h);
    nvs_close(h);
  }
  Serial.println("Gamepad: bonds cleared");
}

bool gamepadHasBond() {
  nvs_iterator_t it = nvs_entry_find(NVS_DEFAULT_PART_NAME, GP_BOND_NAMESPACE, NVS_TYPE_ANY);
  if (!it) return false;
  nvs_release_iterator(it);
  return true;
}

#endif // GAMEPAD_ENABLED
