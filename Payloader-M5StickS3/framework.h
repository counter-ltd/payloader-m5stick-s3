#pragma once
#include <M5Unified.h>

class Screen {
public:
  virtual ~Screen() = default;
  virtual void draw() = 0;
  virtual void onBtnB() {}
  virtual void onBtnBHold() {}
  virtual void onBtnBHoldAlt() {}  // double-tap side then hold
  virtual void onBtnA() {}
  virtual void onBtnAHold() {}
  virtual void onBtnAHoldRepeat() {}  // auto-repeat while BtnA held (single press)
  virtual void onBtnAHoldAlt() {}     // double-tap then hold — and its auto-repeat
  virtual void onBtnATriple() {}      // triple-tap
  virtual void onBtnAQuad() {}        // quad-tap
  virtual void onChord();             // both buttons held — default: rotate display
  virtual void update() {}            // called every loop — override for periodic redraws
};

class Navigator {
public:
  static void begin(Screen* root);
  static void push(Screen* s);
  static void pop();
  static void update();
  static Screen* current();
  static void    initRotation();    // call once in setup — detects and locks portrait/landscape rotations
  static void    rotateDisplay();   // toggle between the two locked rotations only

private:
  static Screen* _stack[8];
  static int     _depth;
  static int     _portraitRot;
  static int     _landscapeRot;
};
