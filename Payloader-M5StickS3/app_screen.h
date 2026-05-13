#pragma once
#include "framework.h"

// Base class for full-screen apps.
// Double-tap then hold BtnB (side) exits back to menu.
class AppScreen : public Screen {
public:
  void onBtnBHoldAlt() override { Navigator::pop(); }  // double-tap side + hold = exit
};
