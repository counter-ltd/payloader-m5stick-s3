#pragma once
#include "app_screen.h"

// Music payload (AirPlay receiver) — Stage 1.
//
// On entry the device hosts its own Wi-Fi AP (see wifi_config.h). Join it from
// an iPhone, then (Stage 2+) pick the device in the AirPlay list. Stage 1 only
// brings up the AP and plays a test tone to prove the Wi-Fi + audio path.
//
// Exit with the standard app gesture (double-tap side button + hold), which
// tears the AP down.
class MusicApp : public AppScreen {
public:
  void draw()          override;
  void update()        override;
  void onBtnB()        override;        // replay test tone
  void onBtnBHoldAlt() override;        // exit: tear down AP, then pop

private:
  void enter();                          // lazy start (first draw)
  void leave();                          // stop AP + audio
  void testTone();

  bool     _started     = false;
  int      _lastClients = -1;
  uint32_t _lastPoll    = 0;
};
