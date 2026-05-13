#include "framework.h"

#define CHORD_HOLD_MS  400
#define REPEAT_MS      300
#define BTN_B_REPEAT_MS 600
#define DOUBLE_TAP_MS  350

void Screen::onChord() { Navigator::rotateDisplay(); }

Screen* Navigator::_stack[8]   = {};
int     Navigator::_depth       = 0;
int     Navigator::_portraitRot  = 0;
int     Navigator::_landscapeRot = 1;

void Navigator::begin(Screen* root) {
  _stack[0] = root;
  _depth     = 1;
  root->draw();
}

void Navigator::push(Screen* s) {
  if (_depth < 8) {
    _stack[_depth++] = s;
    s->draw();
  }
}

void Navigator::pop() {
  if (_depth > 1) {
    _depth--;
    _stack[_depth - 1]->draw();
  }
}

Screen* Navigator::current() {
  return _depth > 0 ? _stack[_depth - 1] : nullptr;
}

void Navigator::initRotation() {
  auto& d = M5.Display;
  for (int r = 0; r < 4; r++) {
    d.setRotation(r);
    if (d.height() > d.width()) {
      _portraitRot  = r;
      _landscapeRot = (r + 1) % 4;
      break;
    }
  }
  d.setRotation(_portraitRot);
}

void Navigator::rotateDisplay() {
  auto& d      = M5.Display;
  bool portrait = d.height() > d.width();
  d.setRotation(portrait ? _landscapeRot : _portraitRot);
  d.fillScreen(TFT_BLACK);
  if (current()) current()->draw();
}

void Navigator::update() {
  M5.update();
  Screen* cur = current();
  if (!cur) return;

  // Chord: both buttons held simultaneously → rotate display
  static uint32_t chordStart    = 0;
  static bool     chordFired    = false;
  static bool     btnBHeld      = false;
  static bool     btnAHeld      = false;
  static uint32_t btnBLastHold  = 0;
  static uint32_t btnALastHold  = 0;
  static uint32_t btnALastRel   = 0;
  static int      btnATapCount  = 0;
  static int      btnAHoldType  = 1;  // tap count frozen at hold-time, used by auto-repeat
  static bool     btnAPending   = false; // release action deferred until tap window expires
  static int      btnBTapCount  = 0;
  static uint32_t btnBLastRel   = 0;
  static int      btnBHoldType  = 1;

  bool bothDown  = M5.BtnA.isPressed() && M5.BtnB.isPressed();
  bool eitherDown = M5.BtnA.isPressed() || M5.BtnB.isPressed();

  if (bothDown && chordStart == 0)  chordStart = millis();
  if (!bothDown)                    chordStart = 0;
  if (!eitherDown) {
    if (chordFired) { btnAHeld = false; btnBHeld = false; }
    chordFired = false;
  }

  if (bothDown && !chordFired && (millis() - chordStart >= CHORD_HOLD_MS)) {
    chordFired   = true;
    btnAHeld     = true;
    btnBHeld     = true;
    btnALastHold = millis();
    btnAPending  = false;
    btnATapCount = 0;
    btnBTapCount = 0;
    cur->onChord();
    return;
  }

  if (chordFired) return;   // swallow all events until both buttons released
  if (bothDown)   return;   // suppress individual events while waiting for chord threshold

  // BtnB (side) — nav: hold = tab, press = scroll; double-tap+hold = exit app

  if (M5.BtnB.wasPressed()) {
    if (!btnBHeld && (millis() - btnBLastRel) <= DOUBLE_TAP_MS)
      btnBTapCount++;
    else
      btnBTapCount = 1;
  }
  if (M5.BtnB.wasHold()) {
    btnBHoldType = btnBTapCount;
    btnBTapCount = 0;
    if (btnBHoldType >= 2) cur->onBtnBHoldAlt();
    else                   cur->onBtnBHold();
    btnBHeld     = true;
    btnBLastHold = millis();
  }
  if (btnBHeld && M5.BtnB.isPressed() && millis() - btnBLastHold >= BTN_B_REPEAT_MS) {
    if (btnBHoldType < 2) cur->onBtnBHold();
    btnBLastHold = millis();
  }
  if (M5.BtnB.wasReleased()) {
    btnBLastRel = millis();
    if (!btnBHeld) cur->onBtnB();
    btnBHeld = false;
  }

  // BtnA (front blue) — select/confirm: multi-tap, hold
  if (M5.BtnA.wasPressed()) {
    if (!btnAHeld && (millis() - btnALastRel) <= DOUBLE_TAP_MS)
      btnATapCount++;
    else
      btnATapCount = 1;
  }
  if (M5.BtnA.wasHold()) {
    btnAPending  = false;
    btnAHoldType = btnATapCount;
    btnATapCount = 0;
    if (btnAHoldType >= 2) cur->onBtnAHoldAlt();
    else                   cur->onBtnAHold();
    btnAHeld     = true;
    btnALastHold = millis();
  }
  if (btnAHeld && M5.BtnA.isPressed() && millis() - btnALastHold >= REPEAT_MS) {
    if (btnAHoldType >= 2) cur->onBtnAHoldAlt();
    else                   cur->onBtnAHoldRepeat();
    btnALastHold = millis();
  }
  if (M5.BtnA.wasReleased()) {
    btnALastRel = millis();
    if (!btnAHeld) btnAPending = true;
    btnAHeld = false;
  }
  if (btnAPending && !M5.BtnA.isPressed() && millis() - btnALastRel > DOUBLE_TAP_MS) {
    if      (btnATapCount >= 4) cur->onBtnAQuad();
    else if (btnATapCount == 3) cur->onBtnATriple();
    else                        cur->onBtnA();
    btnAPending  = false;
    btnATapCount = 0;
  }
  cur->update();
}
