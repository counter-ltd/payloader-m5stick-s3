#include "sys_tab.h"
#include <M5Unified.h>

static const char* getBattery() {
  static char buf[8];
  int level = M5.Power.getBatteryLevel();
  if (level < 0) snprintf(buf, sizeof(buf), "N/A");
  else           snprintf(buf, sizeof(buf), "%d%%", level);
  return buf;
}

static const char* getState() {
  return M5.Power.isCharging() ? "CHRG" : "DISCHRG";
}

static uint32_t getStateColor() {
  return M5.Power.isCharging() ? 0x00FF00 : 0xFF0000;
}

static MenuItem SYS_ITEMS[] = {
  { "Battery", nullptr, nullptr, 0, 0, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, getBattery },
  { "State",   nullptr, nullptr, 0, 0, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, getState, getStateColor },
};

MenuTab sysMenuTab = { "SYS", SYS_ITEMS, 2, nullptr, nullptr, 0x03DF };
