#pragma once
#include "app_screen.h"
#include "fido_hid.h"

class UsbKeyApp : public AppScreen {
public:
  void draw()        override;
  void update()      override;
  void onBtnA()         override;   // Confirm (physical BtnA front)
  void onBtnB()         override;   // Decline (physical BtnB side single press)
  void onBtnBHoldAlt()  override {  // Double-tap+hold side = exit + decline
    if (g_fidoState == FIDO_WAITING_UP) g_fidoState = FIDO_DECLINED;
    g_usbKeyActive = false;
    Navigator::pop();
  }

private:
  FidoState    _lastDrawState = (FidoState)99;
  unsigned long _waitStartMs  = 0;
  int           _lastCountdown = -1;
};
