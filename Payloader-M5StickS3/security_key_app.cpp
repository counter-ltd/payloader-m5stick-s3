#include "security_key_app.h"
#include "fido_hid.h"
#include <M5Unified.h>

static const uint32_t THEME_COLOR = 0x00AA44;  // green

// ---------------------------------------------------------------------------
// draw
// ---------------------------------------------------------------------------
void UsbKeyApp::draw() {
  g_usbKeyActive = true;
  auto& d = M5.Display;
  int   w = d.width();   // 135
  int   h = d.height();  // 240

  FidoState state = g_fidoState;
  _lastDrawState  = state;

  d.fillScreen(TFT_BLACK);

  // Title bar (28 px)
  d.fillRect(0, 0, w, 28, THEME_COLOR);
  d.setTextDatum(MC_DATUM);
  d.setTextColor(TFT_WHITE);
  d.setTextSize(1);
  d.drawString("USB KEY", w / 2, 14);

  d.setTextDatum(MC_DATUM);

  switch (state) {
    // -----------------------------------------------------------------------
    case FIDO_IDLE:
      // Lock symbol (simple text art)
      d.setTextSize(4);
      d.setTextColor(THEME_COLOR);
      d.drawString("@", w / 2, h / 2 - 20);

      d.setTextSize(1);
      d.setTextColor(0x606060);
      d.drawString("READY", w / 2, h / 2 + 28);

      d.setTextSize(1);
      d.setTextColor(0x404040);
      d.drawString("Waiting for auth...", w / 2, h * 80 / 100);
      break;

    // -----------------------------------------------------------------------
    case FIDO_WAITING_UP: {
      // "AUTH REQUEST" header
      d.setTextSize(1);
      d.setTextColor(TFT_YELLOW);
      d.drawString("AUTH REQUEST", w / 2, 50);

      // Show first 12 hex chars of appId
      {
        char appIdStr[13];
        const uint8_t* aid = g_fidoPending.appId;
        snprintf(appIdStr, sizeof(appIdStr),
                 "%02X%02X%02X%02X%02X%02X",
                 aid[0], aid[1], aid[2], aid[3], aid[4], aid[5]);
        d.setTextSize(1);
        d.setTextColor(0x888888);
        d.drawString("AppID:", w / 2, 70);
        d.setTextColor(TFT_WHITE);
        d.drawString(appIdStr, w / 2, 85);
      }

      // Operation type
      d.setTextSize(1);
      d.setTextColor(0xAAAAAA);
      d.drawString(g_fidoPending.isRegister ? "REGISTER" : "AUTHENTICATE",
                   w / 2, 105);

      // Countdown
      int secsLeft = 0;
      if (_waitStartMs > 0) {
        unsigned long elapsed = millis() - _waitStartMs;
        secsLeft = (int)(30000UL > elapsed ? (30000UL - elapsed) / 1000 : 0);
      }
      _lastCountdown = secsLeft;

      char cntBuf[8];
      snprintf(cntBuf, sizeof(cntBuf), "%ds", secsLeft);
      d.setTextSize(2);
      d.setTextColor(secsLeft <= 5 ? TFT_RED : TFT_ORANGE);
      d.drawString(cntBuf, w / 2, 130);

      // Prompt
      d.setTextSize(1);
      d.setTextColor(TFT_GREEN);
      d.drawString("PRESS A TO", w / 2, 165);
      d.drawString("CONFIRM", w / 2, 180);

      d.setTextSize(1);
      d.setTextColor(0x555555);
      d.drawString("PRESS B TO DECLINE", w / 2, 215);
      break;
    }

    // -----------------------------------------------------------------------
    case FIDO_CONFIRMED:
      d.setTextSize(2);
      d.setTextColor(TFT_GREEN);
      d.drawString("CONFIRMED", w / 2, h / 2);

      d.setTextSize(1);
      d.setTextColor(0x404040);
      d.drawString("OK", w / 2, h / 2 + 30);
      break;

    // -----------------------------------------------------------------------
    case FIDO_DECLINED:
      d.setTextSize(2);
      d.setTextColor(TFT_RED);
      d.drawString("DECLINED", w / 2, h / 2);

      d.setTextSize(1);
      d.setTextColor(0x555555);
      d.drawString("Access denied", w / 2, h / 2 + 30);
      break;
  }
}

// ---------------------------------------------------------------------------
// update — called every loop iteration
// ---------------------------------------------------------------------------
void UsbKeyApp::update() {
  FidoState state = g_fidoState;

  // State change: full redraw
  if (state != _lastDrawState) {
    if (state == FIDO_WAITING_UP) {
      _waitStartMs = millis();
    }
    draw();
    return;
  }

  // While waiting: update countdown every second
  if (state == FIDO_WAITING_UP && _waitStartMs > 0) {
    unsigned long elapsed = millis() - _waitStartMs;
    int secsLeft = (int)(30000UL > elapsed ? (30000UL - elapsed) / 1000 : 0);
    if (secsLeft != _lastCountdown) {
      draw();
    }
  }
}

void UsbKeyApp::onBtnA() {
  if (g_fidoState == FIDO_WAITING_UP) {
    g_fidoState = FIDO_CONFIRMED;
    draw();
  }
}

void UsbKeyApp::onBtnB() {
  if (g_fidoState == FIDO_WAITING_UP) {
    g_fidoState = FIDO_DECLINED;
    draw();
  }
}
