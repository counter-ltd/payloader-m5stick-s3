#include "apps_tab.h"
#include "framework.h"
#include "clock_app.h"
#include "security_key_app.h"
#include "bt_key_app.h"
#include "music_app.h"

static ClockApp  clockApp;
static UsbKeyApp usbKeyApp;
static BtKeyApp  btKeyApp;
static MusicApp  musicApp;

static void launchClock()   { Navigator::push(&clockApp); }
static void launchUsbKey()  { Navigator::push(&usbKeyApp); }
static void launchBtKey()   { Navigator::push(&btKeyApp); }
static void launchMusic()   { Navigator::push(&musicApp); }

void appsTabLaunchClock()   { Navigator::push(&clockApp); }
void appsTabLaunchUsbKey()  { Navigator::push(&usbKeyApp); }
void appsTabLaunchBtKey()   { Navigator::push(&btKeyApp); }

static MenuItem APP_ITEMS[] = {
  { "Clock",   nullptr, nullptr, 0, 0, nullptr, nullptr, nullptr, nullptr, nullptr, launchClock  },
  { "USB Key", nullptr, nullptr, 0, 0, nullptr, nullptr, nullptr, nullptr, nullptr, launchUsbKey },
  { "BT Key",  nullptr, nullptr, 0, 0, nullptr, nullptr, nullptr, nullptr, nullptr, launchBtKey  },
  { "Music",   nullptr, nullptr, 0, 0, nullptr, nullptr, nullptr, nullptr, nullptr, launchMusic  },
};

MenuTab appsMenuTab = { "PAYLOADS", APP_ITEMS, 4, nullptr, nullptr, 0xAA00FF };
