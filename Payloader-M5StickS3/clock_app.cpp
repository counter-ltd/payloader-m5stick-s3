#include "clock_app.h"
#include <M5Unified.h>
#include <sys/time.h>

static const char* DAY_NAMES[] = { "Sun","Mon","Tue","Wed","Thu","Fri","Sat" };

static struct tm getTime() {
  timeval tv;
  gettimeofday(&tv, nullptr);
  struct tm t;
  localtime_r(&tv.tv_sec, &t);
  return t;
}

void ClockApp::draw() {
  auto& d = M5.Display;
  int w = d.width(), h = d.height();

  struct tm t = getTime();
  _lastSec = t.tm_sec;

  d.fillScreen(TFT_BLACK);
  d.setTextDatum(MC_DATUM);

  char buf[8];

  // Hour — white, dominant
  int hourSize = w >= 200 ? 10 : 8;
  d.setTextSize(hourSize);
  d.setTextColor(TFT_WHITE);
  snprintf(buf, sizeof(buf), "%02d", t.tm_hour);
  d.drawString(buf, w / 2, h * 20 / 100);

  // Minutes — device blue
  int minSize = w >= 200 ? 7 : 6;
  d.setTextSize(minSize);
  d.setTextColor(0x2EB5FF);
  snprintf(buf, sizeof(buf), "%02d", t.tm_min);
  d.drawString(buf, w / 2, h * 60 / 100);

  // Seconds — dim blue
  d.setTextSize(3);
  d.setTextColor(0x1A5A7A);
  snprintf(buf, sizeof(buf), "%02d", t.tm_sec);
  d.drawString(buf, w / 2, h * 78 / 100);

  // Date — barely visible gray
  char dateBuf[24];
  snprintf(dateBuf, sizeof(dateBuf), "%s %04d-%02d-%02d",
           DAY_NAMES[t.tm_wday], t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
  d.setTextSize(1);
  d.setTextColor(0x303030);
  d.drawString(dateBuf, w / 2, h * 93 / 100);
}

void ClockApp::update() {
  struct tm t = getTime();
  if (t.tm_sec != _lastSec) draw();
}
