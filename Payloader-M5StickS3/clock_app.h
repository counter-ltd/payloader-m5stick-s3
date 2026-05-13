#pragma once
#include "app_screen.h"

class ClockApp : public AppScreen {
public:
  void draw()   override;
  void update() override;
private:
  int _lastSec = -1;
};
