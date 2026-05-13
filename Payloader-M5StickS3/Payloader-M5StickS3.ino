#include <M5Unified.h>
#include <sys/time.h>
#include "framework.h"
#include "menu_screen.h"
#include "apps_tab.h"
#include "ir_tab.h"
#include "sys_tab.h"
#include "bt_tab.h"
#include "fido_hid.h"
#include "fido_bt.h"

#define HOLD_MS 300

static const MenuTab TABS[] = { appsMenuTab, irMenuTab, btMenuTab, sysMenuTab };
static MenuScreen mainMenu(TABS, 4);

void setup() {
  // USB must be initialised before M5.begin() on ESP32-S3
  fidoHidBegin();  // USB HID must be before M5.begin()

  auto cfg = M5.config();
  M5.begin(cfg);

  M5.BtnA.setHoldThresh(HOLD_MS);
  M5.BtnB.setHoldThresh(HOLD_MS);
  Navigator::initRotation();
  irTabSetup();
  btTabSetup();    // creates BLE device + server
  fidoBtBegin();   // adds FIDO service to BLE server

  // Sync time: prefer external RTC if valid, else seed from build timestamp
  {
    static const char* MONTHS = "JanFebMarAprMayJunJulAugSepOctNovDec";
    char mon[4] = {};
    int  day, year, hh, mm, ss;
    sscanf(__DATE__, "%3s %d %d", mon, &day, &year);
    sscanf(__TIME__, "%d:%d:%d",  &hh, &mm,  &ss);
    int mIdx = 1;
    for (int i = 0; i < 12; i++) {
      if (strncmp(mon, MONTHS + i * 3, 3) == 0) { mIdx = i + 1; break; }
    }
    ss += 60; // compensate compile→flash lag
    if (ss >= 60) { ss -= 60; mm++; }
    if (mm >= 60) { mm -= 60; hh++; }
    if (hh >= 24) { hh  = 0;  day++; }

    auto rtc = M5.Rtc.getDateTime();
    bool rtcValid = rtc.date.year >= 2020;

    struct tm t = {};
    if (rtcValid) {
      t.tm_year = rtc.date.year - 1900;
      t.tm_mon  = rtc.date.month - 1;
      t.tm_mday = rtc.date.date;
      t.tm_hour = rtc.time.hours;
      t.tm_min  = rtc.time.minutes;
      t.tm_sec  = rtc.time.seconds;
    } else {
      t.tm_year = year - 1900;
      t.tm_mon  = mIdx - 1;
      t.tm_mday = day;
      t.tm_hour = hh;
      t.tm_min  = mm;
      t.tm_sec  = ss;
      // Push build time into external RTC too
      m5::rtc_datetime_t build{};
      build.date.year = year; build.date.month = mIdx; build.date.date = day;
      build.time.hours = hh;  build.time.minutes = mm;  build.time.seconds = ss;
      M5.Rtc.setDateTime(build);
    }
    time_t epoch = mktime(&t);
    timeval tv   = { epoch, 0 };
    settimeofday(&tv, nullptr);
  }

  M5.Display.fillScreen(TFT_BLACK);
  Navigator::begin(&mainMenu);
  appsTabLaunchClock();
}

void loop() {
  Navigator::update();
  irTabUpdate();
  btTabUpdate();
  fidoHidUpdate();
  fidoBtUpdate();
}
