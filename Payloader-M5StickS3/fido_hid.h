#pragma once
#include <stdint.h>

// ---------------------------------------------------------------------------
// CTAP HID constants
// ---------------------------------------------------------------------------
#define CTAPHID_MSG        0x83
#define CTAPHID_INIT       0x86
#define CTAPHID_PING       0x81
#define CTAPHID_ERROR      0xBF
#define CTAPHID_WINK       0x88
#define CTAPHID_KEEPALIVE  0xBB

// Error codes
#define CTAP1_ERR_INVALID_CMD   0x01
#define CTAP1_ERR_INVALID_LEN   0x03
#define CTAP1_ERR_INVALID_SEQ   0x04
#define CTAP1_ERR_TIMEOUT       0x05
#define CTAP1_ERR_CHANNEL_BUSY  0x06
#define CTAP1_ERR_OTHER         0x7F

#define CTAP_BROADCAST_CID  0xFFFFFFFFUL
#define HID_PACKET_SIZE     64

// ---------------------------------------------------------------------------
// Shared FIDO state (set by USB task, read/written by app)
// ---------------------------------------------------------------------------
enum FidoState { FIDO_IDLE, FIDO_WAITING_UP, FIDO_CONFIRMED, FIDO_DECLINED };
extern volatile FidoState g_fidoState;

struct FidoPending {
  bool     isRegister;
  uint8_t  appId[32];
  uint8_t  challenge[32];
  uint8_t  keyHandle[64];
  uint8_t  khLen;
  uint32_t cid;
  uint8_t  p1;
  // For deferred response
  uint8_t  cmd;    // CTAPHID_MSG
};
extern FidoPending g_fidoPending;

// Set true while the corresponding key app is on screen — requests decline instantly otherwise
extern bool g_usbKeyActive;
extern bool g_btKeyActive;

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
void fidoHidBegin();
void fidoHidUpdate();

// Called when user confirms/declines a USB-transport request
void fidoCompleteOp();

// Send a keepalive on the current pending CID
void fidoSendKeepalive();
