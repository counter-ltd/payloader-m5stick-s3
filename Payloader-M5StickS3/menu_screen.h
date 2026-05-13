#pragma once
#include "framework.h"

struct MenuItem {
  const char*        label;
  Screen*            subScreen      = nullptr;
  // Picker
  const char* const* pickerOptions  = nullptr;
  int                pickerCount    = 0;
  int                pickerIndex    = 0;
  void               (*onPickerChange)() = nullptr;
  // Toggle
  bool*              toggleState    = nullptr;
  // Right-aligned colored square indicator (green=true, red=false); toggles on BtnA
  bool*              indicatorState = nullptr;
  // Visibility
  bool*              showWhen       = nullptr;  // null = always visible
  // Long-press BtnA action
  void               (*onHold)()   = nullptr;
  // BtnB on item with no other handler
  void               (*onSelect)() = nullptr;
  // Called after indicatorState is toggled
  void               (*onToggle)() = nullptr;
  // Right-aligned dynamic value text
  const char*        (*getValue)()          = nullptr;
  // Optional color for getValue text (null = default text color)
  uint32_t           (*getColor)()          = nullptr;
  // Hold BtnA (single press + hold) → fires + repeats
  void               (*onHoldRepeat)()      = nullptr;
  // Double-tap then hold BtnA → fires + repeats
  void               (*onHoldAltRepeat)()   = nullptr;
  // Triple-tap BtnA
  void               (*onTriplePress)()     = nullptr;
  // Quad-tap BtnA
  void               (*onQuadPress)()       = nullptr;
};

struct MenuTab {
  const char* name;
  MenuItem*   items;
  int         itemCount;
  int*        pCount     = nullptr;
  void        (*onLeave)()                      = nullptr;
  uint32_t    themeColor                        = 0x03DF;   // title bar + active indicator color
};

class MenuScreen : public Screen {
public:
  MenuScreen(const MenuTab* tabs, int tabCount);
  void draw() override;
  void onBtnB() override;             // next visible item
  void onBtnBHold() override;         // next tab
  void onBtnA() override;             // select: cycle picker / toggle / push subScreen
  void onBtnAHold() override;         // BtnA hold: onHoldRepeat or onHold
  void onBtnAHoldRepeat() override;   // BtnA hold auto-repeat: onHoldRepeat only
  void onBtnAHoldAlt() override;      // double-tap + hold: onHoldAltRepeat
  void onBtnATriple() override;       // triple-tap: onTriplePress
  void onBtnAQuad() override;         // quad-tap: onQuadPress

private:
  const MenuTab* _tabs;
  int _tabCount;
  int _tabIndex;
  int _selectedIndex;
  int _scrollOffset;

  int  getCount(const MenuTab& tab) const;
  bool isVisible(const MenuItem& item) const;
  int  maxVisibleItems() const;
  int  visibleRowOf(int rawIndex) const;
  void updateScroll();
};
