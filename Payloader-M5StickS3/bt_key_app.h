#pragma once
#include "app_screen.h"
#include "fido_hid.h"

class BtKeyApp : public AppScreen {
public:
  void draw()        override;
  void update()      override;
  void onBtnA()         override;   // Confirm
  void onBtnB()         override;   // Decline
  void onBtnBHoldAlt()  override {  // Double-tap+hold side = exit + decline
    if (g_fidoState == FIDO_WAITING_UP) g_fidoState = FIDO_DECLINED;
    g_btKeyActive = false;
    Navigator::pop();
  }

private:
  FidoState     _lastDrawState = (FidoState)99;
  unsigned long _waitStartMs   = 0;
  int           _lastCountdown = -1;
};
