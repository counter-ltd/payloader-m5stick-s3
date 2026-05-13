#include "fido_hid.h"
#include "fido_u2f.h"
#include <Arduino.h>
#include <USB.h>
#include <USBHID.h>

// ---------------------------------------------------------------------------
// Shared state definitions
// ---------------------------------------------------------------------------
volatile FidoState g_fidoState   = FIDO_IDLE;
FidoPending        g_fidoPending = {};
bool               g_usbKeyActive = false;
bool               g_btKeyActive  = false;

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

// ---------------------------------------------------------------------------
// HID report descriptor — FIDO Alliance usage page 0xF1D0, usage 0x01
// 64-byte IN + 64-byte OUT reports, report ID 0 (no report-ID byte)
// ---------------------------------------------------------------------------
static const uint8_t FIDO_HID_DESCRIPTOR[] = {
  0x06, 0xD0, 0xF1,  // Usage Page (FIDO Alliance)
  0x09, 0x01,        // Usage (U2F HID Authenticator Device)
  0xA1, 0x01,        // Collection (Application)
  // IN report: 64 bytes
  0x09, 0x20,        //   Usage (Input Report Data)
  0x15, 0x00,        //   Logical Minimum (0)
  0x26, 0xFF, 0x00,  //   Logical Maximum (255)
  0x75, 0x08,        //   Report Size (8 bits)
  0x95, 0x40,        //   Report Count (64)
  0x81, 0x02,        //   Input (Data, Variable, Absolute)
  // OUT report: 64 bytes
  0x09, 0x21,        //   Usage (Output Report Data)
  0x15, 0x00,        //   Logical Minimum (0)
  0x26, 0xFF, 0x00,  //   Logical Maximum (255)
  0x75, 0x08,        //   Report Size (8 bits)
  0x95, 0x40,        //   Report Count (64)
  0x91, 0x02,        //   Output (Data, Variable, Absolute)
  0xC0               // End Collection
};

// ---------------------------------------------------------------------------
// Multi-frame reassembly state
// ---------------------------------------------------------------------------
static uint32_t s_rxCid      = 0;
static uint8_t  s_rxCmd      = 0;
static uint16_t s_rxExpected = 0;
static uint16_t s_rxReceived = 0;
static uint8_t  s_rxBuf[7609]; // max U2F msg ~ 7609 bytes
static uint8_t  s_rxSeq      = 0;
static bool     s_rxPending  = false;

// Channel allocation — simple: one allocated channel
static uint32_t s_allocCid = 0x00000001;

// Keepalive timing
static unsigned long s_lastKeepalive = 0;

// Waiting-UP timeout
static unsigned long s_waitStartMs   = 0;

// Post-decline cooldown — reject new requests silently for 3s after a decline
static unsigned long s_declinedUntil = 0;

// Forward declarations
static void onHIDFrame(const uint8_t* frame);
static void processMessage(uint32_t cid, uint8_t cmd, const uint8_t* data, uint16_t len);
static void sendResponse(uint32_t cid, uint8_t cmd, const uint8_t* data, uint16_t len);
static void sendError(uint32_t cid, uint8_t err);

// Defined in fido_u2f.cpp — builds the response payload when operation completes
void u2fBuildResponse(uint8_t* resp, uint16_t* respLen, uint16_t* sw);

// ---------------------------------------------------------------------------
// FidoHIDDevice class
// ---------------------------------------------------------------------------
static USBHID HID;

class FidoHIDDevice : public USBHIDDevice {
public:
  FidoHIDDevice() {
    static bool registered = false;
    if (!registered) {
      HID.addDevice(this, sizeof(FIDO_HID_DESCRIPTOR));
      registered = true;
    }
  }

  uint16_t _onGetDescriptor(uint8_t* dst) override {
    memcpy(dst, FIDO_HID_DESCRIPTOR, sizeof(FIDO_HID_DESCRIPTOR));
    return sizeof(FIDO_HID_DESCRIPTOR);
  }

  void _onOutput(uint8_t report_id, const uint8_t* buf, uint16_t len) override {
    (void)report_id;
    if (len >= HID_PACKET_SIZE) {
      onHIDFrame(buf);
    }
  }

  bool sendFrame(const uint8_t* data) {
    return HID.SendReport(0, data, HID_PACKET_SIZE);
  }
};

static FidoHIDDevice fidoDevice;

// ---------------------------------------------------------------------------
// Public keepalive (called from fidoHidUpdate and fidoCompleteOp)
// ---------------------------------------------------------------------------
void fidoSendKeepalive() {
  uint8_t status = 0x02; // UP needed
  sendResponse(g_fidoPending.cid, CTAPHID_KEEPALIVE, &status, 1);
}

// ---------------------------------------------------------------------------
// HID framing
// ---------------------------------------------------------------------------
static void onHIDFrame(const uint8_t* frame) {
  uint32_t cid = ((uint32_t)frame[0] << 24) |
                 ((uint32_t)frame[1] << 16) |
                 ((uint32_t)frame[2] <<  8) |
                 ((uint32_t)frame[3]);
  uint8_t cmdOrSeq = frame[4];

  bool isInit = (cmdOrSeq & 0x80) != 0;

  if (isInit) {
    // Init frame
    uint8_t  cmd  = cmdOrSeq & 0x7F;
    uint16_t bcnt = ((uint16_t)frame[5] << 8) | frame[6];

    if (cid == CTAP_BROADCAST_CID && cmd == (CTAPHID_INIT & 0x7F)) {
      // Allocate channel
      uint8_t nonce[8];
      memcpy(nonce, frame + 7, 8);

      uint32_t newCid = ++s_allocCid;

      uint8_t resp[17];
      memcpy(resp, nonce, 8);
      resp[8]  = (newCid >> 24) & 0xFF;
      resp[9]  = (newCid >> 16) & 0xFF;
      resp[10] = (newCid >>  8) & 0xFF;
      resp[11] = (newCid      ) & 0xFF;
      resp[12] = 2;   // protocol version
      resp[13] = 1;   // major device version
      resp[14] = 0;   // minor device version
      resp[15] = 0;   // build device version
      resp[16] = 0x04; // capabilities: WINK

      sendResponse(CTAP_BROADCAST_CID, CTAPHID_INIT, resp, sizeof(resp));
      return;
    }

    // Check channel valid
    if (cid != s_allocCid) {
      sendError(cid, CTAP1_ERR_INVALID_CMD);
      return;
    }

    // Start reassembly
    s_rxCid      = cid;
    s_rxCmd      = cmd;
    s_rxExpected = bcnt;
    s_rxReceived = 0;
    s_rxSeq      = 0;
    s_rxPending  = (bcnt > 57);

    uint16_t chunk = (bcnt < 57) ? bcnt : 57;
    memcpy(s_rxBuf, frame + 7, chunk);
    s_rxReceived += chunk;

    if (s_rxReceived >= s_rxExpected) {
      processMessage(cid, cmd, s_rxBuf, s_rxExpected);
    }
  } else {
    // Continuation frame
    if (!s_rxPending || cid != s_rxCid) {
      sendError(cid, CTAP1_ERR_INVALID_SEQ);
      return;
    }
    uint8_t seq = cmdOrSeq;
    if (seq != s_rxSeq) {
      sendError(cid, CTAP1_ERR_INVALID_SEQ);
      s_rxPending = false;
      return;
    }
    s_rxSeq++;

    uint16_t remaining = s_rxExpected - s_rxReceived;
    uint16_t chunk     = (remaining < 59) ? remaining : 59;
    memcpy(s_rxBuf + s_rxReceived, frame + 5, chunk);
    s_rxReceived += chunk;

    if (s_rxReceived >= s_rxExpected) {
      s_rxPending = false;
      processMessage(s_rxCid, s_rxCmd, s_rxBuf, s_rxExpected);
    }
  }
}

// ---------------------------------------------------------------------------
// Message dispatcher
// ---------------------------------------------------------------------------
static uint8_t s_respBuf[4096];

static void processMessage(uint32_t cid, uint8_t cmd, const uint8_t* data, uint16_t len) {
  switch (cmd | 0x80) {  // cmd stored without bit7; add it back for comparison
    case CTAPHID_PING:
      sendResponse(cid, CTAPHID_PING, data, len);
      break;

    case CTAPHID_WINK: {
      // Acknowledge wink
      sendResponse(cid, CTAPHID_WINK, nullptr, 0);
      break;
    }

    case CTAPHID_MSG: {
      // Cooldown after decline — silently reject retries for 3 seconds
      if (millis() < s_declinedUntil) {
        uint8_t err[2] = { 0x69, 0x85 };  // SW_CONDITIONS_NOT_SATISFIED
        sendResponse(cid, CTAPHID_MSG, err, 2);
        break;
      }
      uint16_t respLen = 0;
      uint16_t sw = u2fProcessApdu(data, len, s_respBuf, &respLen, cid, CTAPHID_MSG);
      if (sw == SW_PENDING) {
        // Non-blocking: response will be sent from fidoCompleteOp()
        // Store cid and cmd for later
        g_fidoPending.cid = cid;
        g_fidoPending.cmd = CTAPHID_MSG;
        s_waitStartMs     = millis();
      } else {
        // Append SW
        s_respBuf[respLen++] = (sw >> 8) & 0xFF;
        s_respBuf[respLen++] = (sw     ) & 0xFF;
        sendResponse(cid, CTAPHID_MSG, s_respBuf, respLen);
      }
      break;
    }

    default:
      sendError(cid, CTAP1_ERR_INVALID_CMD);
      break;
  }
}

// ---------------------------------------------------------------------------
// Response framing
// ---------------------------------------------------------------------------
static void sendResponse(uint32_t cid, uint8_t cmd, const uint8_t* data, uint16_t len) {
  uint8_t frame[HID_PACKET_SIZE];

  // Init frame
  memset(frame, 0, HID_PACKET_SIZE);
  frame[0] = (cid >> 24) & 0xFF;
  frame[1] = (cid >> 16) & 0xFF;
  frame[2] = (cid >>  8) & 0xFF;
  frame[3] = (cid      ) & 0xFF;
  frame[4] = cmd;  // already has bit7 set for init frames per CTAPHID_* constants
  frame[5] = (len >> 8) & 0xFF;
  frame[6] = (len     ) & 0xFF;

  uint16_t sent = 0;
  uint16_t chunk = (len < 57) ? len : 57;
  if (data && chunk > 0) memcpy(frame + 7, data, chunk);
  sent += chunk;

  fidoDevice.sendFrame(frame);

  // Continuation frames
  uint8_t seq = 0;
  while (sent < len) {
    memset(frame, 0, HID_PACKET_SIZE);
    frame[0] = (cid >> 24) & 0xFF;
    frame[1] = (cid >> 16) & 0xFF;
    frame[2] = (cid >>  8) & 0xFF;
    frame[3] = (cid      ) & 0xFF;
    frame[4] = seq++;

    chunk = (len - sent < 59) ? (len - sent) : 59;
    memcpy(frame + 5, data + sent, chunk);
    sent += chunk;

    fidoDevice.sendFrame(frame);
  }
}

static void sendError(uint32_t cid, uint8_t err) {
  sendResponse(cid, CTAPHID_ERROR, &err, 1);
}

// ---------------------------------------------------------------------------
// fidoCompleteOp — called from fidoHidUpdate when user confirms/declines
// ---------------------------------------------------------------------------
void fidoCompleteOp() {
  // Build and send the U2F response for the pending operation
  uint16_t respLen = 0;
  uint16_t sw      = 0;
  u2fBuildResponse(s_respBuf, &respLen, &sw);

  s_respBuf[respLen++] = (sw >> 8) & 0xFF;
  s_respBuf[respLen++] = (sw     ) & 0xFF;

  sendResponse(g_fidoPending.cid, g_fidoPending.cmd, s_respBuf, respLen);

  portENTER_CRITICAL(&s_mux);
  bool wasDeclined = (g_fidoState == FIDO_DECLINED);
  g_fidoState = FIDO_IDLE;
  portEXIT_CRITICAL(&s_mux);

  if (wasDeclined) s_declinedUntil = millis() + 3000UL;
}

// ---------------------------------------------------------------------------
// fidoHidUpdate — call from Arduino loop()
// ---------------------------------------------------------------------------
void fidoHidUpdate() {
  if (!g_usbKeyActive) return;

  FidoState state;
  portENTER_CRITICAL(&s_mux);
  state = g_fidoState;
  portEXIT_CRITICAL(&s_mux);

  if (state == FIDO_WAITING_UP) {
    // Timeout after 30 seconds
    if (millis() - s_waitStartMs > 30000UL) {
      portENTER_CRITICAL(&s_mux);
      g_fidoState = FIDO_DECLINED;
      portEXIT_CRITICAL(&s_mux);
      fidoCompleteOp();
      return;
    }
    // Send keepalive every 100 ms
    if (millis() - s_lastKeepalive >= 100) {
      s_lastKeepalive = millis();
      fidoSendKeepalive();
    }
  } else if (state == FIDO_CONFIRMED || state == FIDO_DECLINED) {
    fidoCompleteOp();
  }
}

// ---------------------------------------------------------------------------
// fidoHidBegin — call BEFORE M5.begin() in setup()
// ---------------------------------------------------------------------------
void fidoHidBegin() {
  USB.VID(0x239A);
  USB.PID(0xF1D0);
  USB.manufacturerName("M5Stack");
  USB.productName("M5 Security Key");
  HID.begin();
  USB.begin();
}
