#pragma once
#include <stdint.h>

// ---------------------------------------------------------------------------
// ISO 7816-4 status words
// ---------------------------------------------------------------------------
#define SW_NO_ERROR           0x9000
#define SW_CONDITIONS_NOT_SAT 0x6985
#define SW_WRONG_DATA         0x6A80
#define SW_INS_NOT_SUPPORTED  0x6D00

// Sentinel: operation is pending (non-blocking), response sent later
#define SW_PENDING            0xFFFF

// ---------------------------------------------------------------------------
// U2F APDU processor
//
// Returns SW code. If SW_PENDING, the response will be sent asynchronously
// from fidoCompleteOp() once the user confirms/declines.
// ---------------------------------------------------------------------------
uint16_t u2fProcessApdu(const uint8_t* apdu, uint16_t apduLen,
                        uint8_t* resp, uint16_t* respLen, uint32_t cid, uint8_t hidCmd);
