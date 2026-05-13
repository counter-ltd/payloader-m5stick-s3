#include "fido_bt.h"
#include "fido_hid.h"
#include "fido_u2f.h"
#include "bt_tab.h"
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <Arduino.h>

// FIDO BLE Service UUIDs (FIDO Alliance spec)
#define FIDO_SVC_UUID  "0000FFFD-0000-1000-8000-00805F9B34FB"
#define FIDO_CP_UUID   "F1D0FFF1-DEAA-ECEE-B42F-C9BA7ED623BB"  // Control Point (write)
#define FIDO_ST_UUID   "F1D0FFF2-DEAA-ECEE-B42F-C9BA7ED623BB"  // Status (notify)
#define FIDO_CPL_UUID  "F1D0FFF3-DEAA-ECEE-B42F-C9BA7ED623BB"  // Control Point Length (read)
#define FIDO_REV_UUID  "F1D0FFF4-DEAA-ECEE-B42F-C9BA7ED623BB"  // Service Revision Bitfield

// CTAP BLE commands
#define BLE_CMD_PING       0x81
#define BLE_CMD_KEEPALIVE  0x82
#define BLE_CMD_MSG        0x83
#define BLE_CMD_ERROR      0xBF

// Error codes
#define BLE_ERR_INVALID_CMD  0x01
#define BLE_ERR_OTHER        0x7F

// SW_PENDING from fido_u2f.h
#define SW_PENDING 0xFFFF

static BLECharacteristic* s_status    = nullptr;
static unsigned long      s_waitStart = 0;
static portMUX_TYPE       s_mux       = portMUX_INITIALIZER_UNLOCKED;

// Reassembly
static uint8_t  s_rxBuf[512];
static uint16_t s_rxExpected = 0;
static uint16_t s_rxReceived = 0;
static uint8_t  s_rxCmd      = 0;
static uint8_t  s_rxSeq      = 0;

// Response
static uint8_t  s_respBuf[512];

extern uint16_t u2fProcessApdu(const uint8_t* apdu, uint16_t apduLen,
                               uint8_t* resp, uint16_t* respLen,
                               uint32_t cid, uint8_t hidCmd);
extern void u2fBuildResponse(uint8_t* resp, uint16_t* respLen, uint16_t* sw);

// ---------------------------------------------------------------------------
// BLE framing helpers
// ---------------------------------------------------------------------------
static void sendBleResponse(uint8_t cmd, const uint8_t* data, uint16_t len) {
  BLEServer* srv = btGetServer();
  if (!s_status || !srv || srv->getConnectedCount() == 0) return;
  uint8_t frame[515];
  frame[0] = 0x80 | cmd;
  frame[1] = (len >> 8) & 0xFF;
  frame[2] = len & 0xFF;
  if (data && len > 0) memcpy(frame + 3, data, len < 512 ? len : 512);
  s_status->setValue(frame, 3 + (len < 512 ? len : 512));
  s_status->notify();
}

static void sendBleError(uint8_t err) {
  sendBleResponse(BLE_CMD_ERROR, &err, 1);
}

// ---------------------------------------------------------------------------
// CTAP BLE message dispatch
// ---------------------------------------------------------------------------
static void processMessage(uint8_t cmd, const uint8_t* data, uint16_t len) {
  switch (cmd) {
    case BLE_CMD_PING:
      sendBleResponse(BLE_CMD_PING, data, len);
      break;

    case BLE_CMD_MSG: {
      uint16_t respLen = 0;
      uint16_t sw = u2fProcessApdu(data, len, s_respBuf, &respLen, 0, BLE_CMD_MSG);
      if (sw == SW_PENDING) {
        g_fidoPending.cmd = BLE_CMD_MSG;
        s_waitStart = millis();
      } else {
        s_respBuf[respLen++] = (sw >> 8) & 0xFF;
        s_respBuf[respLen++] = sw & 0xFF;
        sendBleResponse(BLE_CMD_MSG, s_respBuf, respLen);
      }
      break;
    }

    default:
      sendBleError(BLE_ERR_INVALID_CMD);
      break;
  }
}

class FidoBtCpCb : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* ch) override {
    String val = ch->getValue();
    if (val.isEmpty()) return;
    const uint8_t* data = (const uint8_t*)val.c_str();
    uint16_t len = (uint16_t)val.length();

    uint8_t first = data[0];
    if (first & 0x80) {
      // Init frame
      if (len < 3) { sendBleError(BLE_ERR_INVALID_CMD); return; }
      s_rxCmd      = first & 0x7F;
      s_rxExpected = ((uint16_t)data[1] << 8) | data[2];
      uint16_t chunk = len - 3;
      if (chunk > s_rxExpected) chunk = s_rxExpected;
      memcpy(s_rxBuf, data + 3, chunk);
      s_rxReceived = chunk;
      s_rxSeq      = 0;
      if (s_rxReceived >= s_rxExpected) processMessage(s_rxCmd, s_rxBuf, s_rxExpected);
    } else {
      // Continuation frame
      if (data[0] != s_rxSeq) { sendBleError(BLE_ERR_INVALID_CMD); return; }
      s_rxSeq++;
      uint16_t chunk = len - 1;
      uint16_t remaining = s_rxExpected - s_rxReceived;
      if (chunk > remaining) chunk = remaining;
      memcpy(s_rxBuf + s_rxReceived, data + 1, chunk);
      s_rxReceived += chunk;
      if (s_rxReceived >= s_rxExpected) processMessage(s_rxCmd, s_rxBuf, s_rxExpected);
    }
  }
};

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
void fidoBtCompleteOp() {
  uint16_t respLen = 0, sw = 0;
  u2fBuildResponse(s_respBuf, &respLen, &sw);
  s_respBuf[respLen++] = (sw >> 8) & 0xFF;
  s_respBuf[respLen++] = sw & 0xFF;
  sendBleResponse(BLE_CMD_MSG, s_respBuf, respLen);

  portENTER_CRITICAL(&s_mux);
  g_fidoState = FIDO_IDLE;
  portEXIT_CRITICAL(&s_mux);
}

void fidoBtBegin() {
  // Server created and owned by bt_tab; we just add the FIDO service to it
  BLEServer* srv = btGetServer();
  if (!srv) return;

  BLEService* svc = srv->createService(BLEUUID(FIDO_SVC_UUID));

  // Status — notify
  s_status = svc->createCharacteristic(BLEUUID(FIDO_ST_UUID), BLECharacteristic::PROPERTY_NOTIFY);
  s_status->addDescriptor(new BLE2902());

  // Control Point — write
  BLECharacteristic* cp = svc->createCharacteristic(BLEUUID(FIDO_CP_UUID),
                                                     BLECharacteristic::PROPERTY_WRITE);
  cp->setCallbacks(new FidoBtCpCb());

  // Control Point Length — read, report 512 bytes max
  BLECharacteristic* cpl = svc->createCharacteristic(BLEUUID(FIDO_CPL_UUID),
                                                      BLECharacteristic::PROPERTY_READ);
  uint8_t cplVal[2] = { 0x02, 0x00 };
  cpl->setValue(cplVal, 2);

  // Service Revision Bitfield — U2F 1.1 = 0x20
  BLECharacteristic* rev = svc->createCharacteristic(BLEUUID(FIDO_REV_UUID),
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE_NR |
    BLECharacteristic::PROPERTY_NOTIFY);
  uint8_t revVal = 0x20;
  rev->setValue(&revVal, 1);
  rev->addDescriptor(new BLE2902());

  svc->start();

  // Register FIDO service UUID in advertising (advertising controlled by bt_tab Discoverable toggle)
  BLEDevice::getAdvertising()->addServiceUUID(BLEUUID(FIDO_SVC_UUID));
  BLEDevice::getAdvertising()->setScanResponse(true);
}

void fidoBtUpdate() {
  if (!g_btKeyActive) return;

  FidoState state = g_fidoState;

  if (state == FIDO_WAITING_UP) {
    if (millis() - s_waitStart > 30000UL) {
      g_fidoState = FIDO_DECLINED;
      fidoBtCompleteOp();
    }
  } else if (state == FIDO_CONFIRMED || state == FIDO_DECLINED) {
    fidoBtCompleteOp();
  }
}
