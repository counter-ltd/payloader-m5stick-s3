#include "bt_tab.h"
#include "framework.h"
#include <M5Unified.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEScan.h>
#include <BLEClient.h>
#include <BLE2902.h>
#include <Preferences.h>

#define MAX_SCAN     6
#define MAX_INCOMING 4
#define SCAN_OFFSET  3
#define INC_OFFSET   (SCAN_OFFSET + MAX_SCAN)   // = 9
#define TOTAL_ITEMS  (INC_OFFSET + MAX_INCOMING) // = 13
#define SCAN_SECS    5

// ---------------------------------------------------------------------------
// Global state
// ---------------------------------------------------------------------------
bool g_btActive    = false;
bool g_btConnected = false;
char g_btPeerName[32] = {};
char g_btPeerAddr[18] = {};

// ---------------------------------------------------------------------------
// Internal state
// ---------------------------------------------------------------------------
static BLEServer* s_bleServer   = nullptr;
static BLEScan*   s_bleScan     = nullptr;
static BLEClient* s_bleClient   = nullptr;
static Preferences s_prefs;

static bool s_discoverable = false;
static bool s_scanning     = false;

// Scan results
static int  s_scanCount = 0;
static char s_scanNames[MAX_SCAN][32];
static char s_scanAddrs[MAX_SCAN][18];
static bool s_scanVisible[MAX_SCAN] = {};

// Incoming connections pending confirmation
static int      s_incomingCount = 0;
static char     s_incomingNames[MAX_INCOMING][32];
static char     s_incomingAddrs[MAX_INCOMING][18];
static uint16_t s_incomingConnIds[MAX_INCOMING] = {};
static bool     s_incomingVisible[MAX_INCOMING] = {};

// Active pair screen index
static int s_pairIdx = -1;

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------
static void onBtToggle();
static void onDiscoverToggle();
static void startScan();
static const char* getScanStatus();
static void connectScan0(); static void connectScan1(); static void connectScan2();
static void connectScan3(); static void connectScan4(); static void connectScan5();
static void pairIncoming0(); static void pairIncoming1();
static void pairIncoming2(); static void pairIncoming3();

static void (*s_scanConnFns[MAX_SCAN])() = {
  connectScan0, connectScan1, connectScan2, connectScan3, connectScan4, connectScan5,
};
static void (*s_pairFns[MAX_INCOMING])() = {
  pairIncoming0, pairIncoming1, pairIncoming2, pairIncoming3,
};

// ---------------------------------------------------------------------------
// BtPairScreen — full-screen confirm/deny for incoming BLE connections
// ---------------------------------------------------------------------------
class BtPairScreen : public AppScreen {
public:
  void draw() override {
    auto& d = M5.Display;
    int w = d.width(), h = d.height();
    d.fillScreen(TFT_BLACK);
    d.fillRect(0, 0, w, 28, 0x0066FF);
    d.setTextDatum(MC_DATUM);
    d.setTextColor(TFT_WHITE);
    d.setTextSize(1);
    d.drawString("PAIR REQUEST", w / 2, 14);

    d.setTextSize(1);
    d.setTextColor(0x888888);
    d.drawString("Device:", w / 2, 55);
    d.setTextColor(TFT_WHITE);
    const char* name = (s_pairIdx >= 0) ? s_incomingNames[s_pairIdx] : "?";
    d.drawString(name, w / 2, 75);

    d.setTextSize(1);
    d.setTextColor(TFT_GREEN);
    d.drawString("PRESS A TO ACCEPT", w / 2, 130);
    d.setTextColor(0x555555);
    d.drawString("PRESS B TO DENY",   w / 2, 150);
  }

  void onBtnA() override {   // Accept — keep connection, store device
    if (s_pairIdx >= 0) {
      strncpy(g_btPeerName, s_incomingNames[s_pairIdx], 31); g_btPeerName[31] = '\0';
      strncpy(g_btPeerAddr, s_incomingAddrs[s_pairIdx], 17); g_btPeerAddr[17] = '\0';
      g_btConnected = true;
      // Save to NVS
      s_prefs.begin("btdevs", false);
      int cnt = s_prefs.getInt("cnt", 0);
      char key[8];
      snprintf(key, sizeof(key), "a%d", cnt);  s_prefs.putString(key, g_btPeerAddr);
      snprintf(key, sizeof(key), "n%d", cnt);  s_prefs.putString(key, g_btPeerName);
      s_prefs.putInt("cnt", cnt + 1);
      s_prefs.end();
    }
    removePending(s_pairIdx);
    Navigator::pop();
  }

  void onBtnB() override {   // Deny — disconnect and remove
    if (s_pairIdx >= 0 && s_bleServer) {
      s_bleServer->disconnect(s_incomingConnIds[s_pairIdx]);
    }
    removePending(s_pairIdx);
    Navigator::pop();
  }

private:
  static void removePending(int idx) {
    if (idx < 0 || idx >= MAX_INCOMING) return;
    s_incomingVisible[idx] = false;
    s_incomingNames[idx][0] = '\0';
    s_incomingConnIds[idx]  = 0;
    s_incomingCount = 0;
    for (int i = 0; i < MAX_INCOMING; i++)
      if (s_incomingVisible[i]) s_incomingCount++;
    s_pairIdx = -1;
  }
};
static BtPairScreen s_pairScreen;

// ---------------------------------------------------------------------------
// Menu items (fixed layout — visibility via showWhen)
// ---------------------------------------------------------------------------
static MenuItem BT_ITEMS[TOTAL_ITEMS] = {
  // [0] Active
  { "Active",       nullptr,nullptr,0,0,nullptr,nullptr,&g_btActive,   nullptr,        nullptr,nullptr,onBtToggle     },
  // [1] Scan
  { "Scan",         nullptr,nullptr,0,0,nullptr,nullptr,nullptr,        &g_btActive,    nullptr,startScan,nullptr,getScanStatus },
  // [2] Discoverable
  { "Discoverable", nullptr,nullptr,0,0,nullptr,nullptr,&s_discoverable,&g_btActive,   nullptr,nullptr,onDiscoverToggle },
  // [3..8] scan result slots
  { nullptr,nullptr,nullptr,0,0,nullptr,nullptr,nullptr,&s_scanVisible[0],nullptr,nullptr },
  { nullptr,nullptr,nullptr,0,0,nullptr,nullptr,nullptr,&s_scanVisible[1],nullptr,nullptr },
  { nullptr,nullptr,nullptr,0,0,nullptr,nullptr,nullptr,&s_scanVisible[2],nullptr,nullptr },
  { nullptr,nullptr,nullptr,0,0,nullptr,nullptr,nullptr,&s_scanVisible[3],nullptr,nullptr },
  { nullptr,nullptr,nullptr,0,0,nullptr,nullptr,nullptr,&s_scanVisible[4],nullptr,nullptr },
  { nullptr,nullptr,nullptr,0,0,nullptr,nullptr,nullptr,&s_scanVisible[5],nullptr,nullptr },
  // [9..12] incoming pending slots
  { nullptr,nullptr,nullptr,0,0,nullptr,nullptr,nullptr,&s_incomingVisible[0],nullptr,nullptr },
  { nullptr,nullptr,nullptr,0,0,nullptr,nullptr,nullptr,&s_incomingVisible[1],nullptr,nullptr },
  { nullptr,nullptr,nullptr,0,0,nullptr,nullptr,nullptr,&s_incomingVisible[2],nullptr,nullptr },
  { nullptr,nullptr,nullptr,0,0,nullptr,nullptr,nullptr,&s_incomingVisible[3],nullptr,nullptr },
};

MenuTab btMenuTab = { "BLUETOOTH", BT_ITEMS, TOTAL_ITEMS, nullptr, nullptr, 0x0066FF };

// ---------------------------------------------------------------------------
// BLE server callback — handles incoming connections
// ---------------------------------------------------------------------------
class BtMasterServerCb : public BLEServerCallbacks {
  void onConnect(BLEServer* srv, esp_ble_gatts_cb_param_t* param) override {
    if (!s_discoverable) return;
    if (s_incomingCount >= MAX_INCOMING) return;
    // Find free slot
    for (int i = 0; i < MAX_INCOMING; i++) {
      if (!s_incomingVisible[i]) {
        uint8_t* bda = param->connect.remote_bda;
        snprintf(s_incomingAddrs[i], sizeof(s_incomingAddrs[i]),
                 "%02X:%02X:%02X:%02X:%02X:%02X",
                 bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);
        strncpy(s_incomingNames[i], s_incomingAddrs[i], 31);
        s_incomingConnIds[i]  = param->connect.conn_id;
        s_incomingVisible[i]  = true;
        BT_ITEMS[INC_OFFSET + i].label    = s_incomingNames[i];
        BT_ITEMS[INC_OFFSET + i].onSelect = s_pairFns[i];
        s_incomingCount++;
        break;
      }
    }
    srv->startAdvertising();
  }

  void onDisconnect(BLEServer* srv) override {
    srv->startAdvertising();
  }
};

// ---------------------------------------------------------------------------
// BLE scan callback
// ---------------------------------------------------------------------------
class BtScanCb : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice dev) override {
    if (s_scanCount >= MAX_SCAN) return;
    int i = s_scanCount++;
    String name = dev.getName();
    String addr = dev.getAddress().toString();
    strncpy(s_scanNames[i], name.isEmpty() ? addr.c_str() : name.c_str(), 31);
    s_scanNames[i][31] = '\0';
    strncpy(s_scanAddrs[i], addr.c_str(), 17);
    s_scanAddrs[i][17] = '\0';
    BT_ITEMS[SCAN_OFFSET + i].label    = s_scanNames[i];
    BT_ITEMS[SCAN_OFFSET + i].onSelect = s_scanConnFns[i];
    s_scanVisible[i] = true;
  }
};
static BtScanCb s_scanCb;

static void onScanComplete(BLEScanResults) { s_scanning = false; }

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static const char* getScanStatus() {
  if (s_scanning)        return "...";
  if (s_scanCount > 0)   return "done";
  return "";
}

static void startScan() {
  if (s_scanning || !g_btActive || !s_bleScan) return;
  s_scanCount = 0;
  for (int i = 0; i < MAX_SCAN; i++) { s_scanVisible[i] = false; BT_ITEMS[SCAN_OFFSET+i].label = nullptr; }
  s_bleScan->clearResults();
  s_scanning = true;
  s_bleScan->start(SCAN_SECS, onScanComplete, false);
}

static void saveDevice(const char* addr, const char* name) {
  s_prefs.begin("btdevs", false);
  int cnt = s_prefs.getInt("cnt", 0);
  char key[8];
  snprintf(key, sizeof(key), "a%d", cnt);  s_prefs.putString(key, addr);
  snprintf(key, sizeof(key), "n%d", cnt);  s_prefs.putString(key, name);
  s_prefs.putInt("cnt", cnt + 1);
  s_prefs.end();
}

static void connectScan(int idx) {
  if (idx >= s_scanCount) return;
  if (!s_bleClient) s_bleClient = BLEDevice::createClient();
  if (g_btConnected) s_bleClient->disconnect();
  BLEAddress addr(s_scanAddrs[idx]);
  if (s_bleClient->connect(addr)) {
    g_btConnected = true;
    strncpy(g_btPeerName, s_scanNames[idx], 31); g_btPeerName[31] = '\0';
    strncpy(g_btPeerAddr, s_scanAddrs[idx], 17); g_btPeerAddr[17] = '\0';
    saveDevice(s_scanAddrs[idx], s_scanNames[idx]);
  }
}

static void connectScan0() { connectScan(0); } static void connectScan1() { connectScan(1); }
static void connectScan2() { connectScan(2); } static void connectScan3() { connectScan(3); }
static void connectScan4() { connectScan(4); } static void connectScan5() { connectScan(5); }

static void pairIncoming(int idx) {
  s_pairIdx = idx;
  Navigator::push(&s_pairScreen);
}
static void pairIncoming0() { pairIncoming(0); } static void pairIncoming1() { pairIncoming(1); }
static void pairIncoming2() { pairIncoming(2); } static void pairIncoming3() { pairIncoming(3); }

static void onDiscoverToggle() {
  if (!s_bleServer) return;
  if (s_discoverable) {
    BLEDevice::getAdvertising()->start();
  } else {
    BLEDevice::getAdvertising()->stop();
    // Disconnect any pending incoming that weren't confirmed
    for (int i = 0; i < MAX_INCOMING; i++) {
      if (s_incomingVisible[i]) {
        s_bleServer->disconnect(s_incomingConnIds[i]);
        s_incomingVisible[i] = false;
      }
    }
    s_incomingCount = 0;
  }
}

static void onBtToggle() {
  if (g_btActive) {
    // Setup scan now that BT is enabled
    if (!s_bleScan) {
      s_bleScan = BLEDevice::getScan();
      s_bleScan->setAdvertisedDeviceCallbacks(&s_scanCb, false);
      s_bleScan->setActiveScan(true);
      s_bleScan->setInterval(100);
      s_bleScan->setWindow(99);
    }
  } else {
    if (s_scanning && s_bleScan) { s_bleScan->stop(); s_scanning = false; }
    if (s_discoverable) {
      BLEDevice::getAdvertising()->stop();
      s_discoverable = false;
    }
    if (g_btConnected && s_bleClient) { s_bleClient->disconnect(); g_btConnected = false; }
    g_btPeerName[0] = '\0'; g_btPeerAddr[0] = '\0';
    s_scanCount = 0;
    for (int i = 0; i < MAX_SCAN;     i++) s_scanVisible[i]     = false;
    for (int i = 0; i < MAX_INCOMING; i++) s_incomingVisible[i] = false;
    s_incomingCount = 0;
  }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
BLEServer* btGetServer() { return s_bleServer; }

void btTabSetup() {
  BLEDevice::init("M5-EDC");
  s_bleServer = BLEDevice::createServer();
  s_bleServer->setCallbacks(new BtMasterServerCb());
}

void btTabUpdate() {
  // Scan is async; incoming connections are interrupt-driven; nothing to poll
}
