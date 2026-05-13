#include "menu_screen.h"
#include <stdio.h>

static const int TITLE_H = 28;
static const int DOTS_H  = 14;
static const int ITEM_H  = 30;
static const int PADDING =  6;
static const int SQ_SIZE =  6;
static const int SQ_GAP  =  4;

static const uint32_t C_THEME      = 0x03DF;   // #007AFF — vivid blue, matches title
static const uint32_t C_TITLE_BG   = C_THEME;
static const uint32_t C_TITLE_TEXT = TFT_WHITE;
static const uint32_t C_BG         = TFT_BLACK;
static const uint32_t C_ITEM_TEXT  = TFT_WHITE;
static const uint32_t C_SEL_BG     = TFT_DARKGREY;
static const uint32_t C_SEL_TEXT   = TFT_YELLOW;
static const uint32_t C_DIV        = TFT_DARKGREY;
static const uint32_t C_TAB_ACTIVE = C_THEME;
static const uint32_t C_TAB_IDLE   = 0x2945;   // dim grey — recedes vs active
static const uint32_t C_ON         = 0x00FF00;  // RGB888 green
static const uint32_t C_OFF        = 0xFF0000;  // RGB888 red
static const int      IND_SQ       = 8;   // indicator square size

static uint32_t darkenTheme(uint32_t c) {
  // 35% brightness — dark enough for green/red text contrast on any theme hue
  uint8_t r = ((c >> 16) & 0xFF) * 35 / 100;
  uint8_t g = ((c >>  8) & 0xFF) * 35 / 100;
  uint8_t b = ( c        & 0xFF) * 35 / 100;
  return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

MenuScreen::MenuScreen(const MenuTab* tabs, int tabCount)
  : _tabs(tabs), _tabCount(tabCount), _tabIndex(0), _selectedIndex(0), _scrollOffset(0) {}

int MenuScreen::getCount(const MenuTab& tab) const {
  return tab.pCount ? *tab.pCount : tab.itemCount;
}

bool MenuScreen::isVisible(const MenuItem& item) const {
  return !item.showWhen || *item.showWhen;
}

int MenuScreen::maxVisibleItems() const {
  return (M5.Display.height() - TITLE_H - DOTS_H) / ITEM_H;
}

int MenuScreen::visibleRowOf(int rawIndex) const {
  const MenuTab& tab = _tabs[_tabIndex];
  int row = 0;
  for (int i = 0; i < rawIndex && i < getCount(tab); i++) {
    if (isVisible(tab.items[i])) row++;
  }
  return row;
}

void MenuScreen::updateScroll() {
  int row    = visibleRowOf(_selectedIndex);
  int maxVis = maxVisibleItems();
  if (row < _scrollOffset)              _scrollOffset = row;
  if (row >= _scrollOffset + maxVis)    _scrollOffset = row - maxVis + 1;
}

void MenuScreen::draw() {
  auto& d = M5.Display;
  int w = d.width();
  int h = d.height();
  const MenuTab& tab = _tabs[_tabIndex];
  int count = getCount(tab);

  // Title
  uint32_t theme = tab.themeColor;
  d.fillRect(0, 0, w, TITLE_H, theme);
  d.setTextColor(C_TITLE_TEXT);
  d.setTextSize(2);
  d.setTextDatum(MC_DATUM);
  d.drawString(tab.name, w / 2, TITLE_H / 2);

  // Items
  d.fillRect(0, TITLE_H, w, h - TITLE_H - DOTS_H, C_BG);
  d.setTextSize(1);

  int y      = TITLE_H;
  int visRow = 0;
  for (int i = 0; i < count; i++) {
    if (y + ITEM_H > h - DOTS_H) break;
    const MenuItem& item = tab.items[i];
    if (!isVisible(item)) continue;
    if (visRow++ < _scrollOffset) continue;

    bool sel = (i == _selectedIndex);
    d.fillRect(0, y, w, ITEM_H, sel ? darkenTheme(theme) : C_BG);
    d.setTextColor(sel ? C_TITLE_TEXT : C_ITEM_TEXT);

    if (item.pickerOptions) {
      char buf[32];
      snprintf(buf, sizeof(buf), "< %s >", item.pickerOptions[item.pickerIndex]);
      d.setTextDatum(MC_DATUM);
      d.drawString(buf, w / 2, y + ITEM_H / 2);
    } else if (item.toggleState) {
      d.setTextDatum(ML_DATUM);
      d.drawString(item.label, PADDING, y + ITEM_H / 2);
      bool on = *item.toggleState;
      // when selected, keep white — colored text invisible on dark theme bg
      if (!sel) d.setTextColor(on ? C_ON : C_OFF);
      d.setTextDatum(MR_DATUM);
      d.drawString(on ? "[ON]" : "[OFF]", w - PADDING, y + ITEM_H / 2);
    } else if (item.indicatorState) {
      d.setTextDatum(ML_DATUM);
      d.drawString(item.label, PADDING, y + ITEM_H / 2);
      bool on = *item.indicatorState;
      d.fillRect(w - PADDING - IND_SQ, y + (ITEM_H - IND_SQ) / 2, IND_SQ, IND_SQ, on ? C_ON : C_OFF);
    } else if (item.getValue) {
      d.setTextDatum(ML_DATUM);
      d.drawString(item.label, PADDING, y + ITEM_H / 2);
      if (item.getColor) d.setTextColor(item.getColor());
      d.setTextDatum(MR_DATUM);
      d.drawString(item.getValue(), w - PADDING, y + ITEM_H / 2);
    } else {
      d.setTextDatum(ML_DATUM);
      d.drawString(item.label, PADDING, y + ITEM_H / 2);
    }

    d.drawFastHLine(0, y + ITEM_H - 1, w, theme);
    y += ITEM_H;
  }

  // Tab indicator squares
  d.fillRect(0, h - DOTS_H, w, DOTS_H, C_BG);
  int totalW = _tabCount * SQ_SIZE + (_tabCount - 1) * SQ_GAP;
  int startX = (w - totalW) / 2;
  int sqY    = h - DOTS_H / 2 - SQ_SIZE / 2;
  for (int i = 0; i < _tabCount; i++) {
    int x = startX + i * (SQ_SIZE + SQ_GAP);
    d.fillRect(x, sqY, SQ_SIZE, SQ_SIZE, (i == _tabIndex) ? _tabs[i].themeColor : C_TAB_IDLE);
  }
}

void MenuScreen::onBtnB() {
  const MenuTab& tab = _tabs[_tabIndex];
  int count = getCount(tab);
  for (int n = 0; n < count; n++) {
    _selectedIndex = (_selectedIndex + 1) % count;
    if (isVisible(tab.items[_selectedIndex])) break;
  }
  updateScroll();
  draw();
}

void MenuScreen::onBtnBHold() {
  if (_tabs[_tabIndex].onLeave) _tabs[_tabIndex].onLeave();
  _tabIndex      = (_tabIndex + 1) % _tabCount;
  _selectedIndex = 0;
  _scrollOffset  = 0;
  draw();
}

void MenuScreen::onBtnAHold() {
  MenuItem& item = _tabs[_tabIndex].items[_selectedIndex];
  if (item.onHoldRepeat) item.onHoldRepeat();   // repeating hold (+)
  else if (item.onHold)  item.onHold();          // one-shot hold (e.g. delete)
}

void MenuScreen::onBtnAHoldRepeat() {
  MenuItem& item = _tabs[_tabIndex].items[_selectedIndex];
  if (item.onHoldRepeat) item.onHoldRepeat();
}

void MenuScreen::onBtnAHoldAlt() {
  MenuItem& item = _tabs[_tabIndex].items[_selectedIndex];
  if (item.onHoldAltRepeat) item.onHoldAltRepeat();
}

void MenuScreen::onBtnATriple() {
  MenuItem& item = _tabs[_tabIndex].items[_selectedIndex];
  if (item.onTriplePress) item.onTriplePress();
}

void MenuScreen::onBtnAQuad() {
  MenuItem& item = _tabs[_tabIndex].items[_selectedIndex];
  if (item.onQuadPress) item.onQuadPress();
}

void MenuScreen::onBtnA() {
  MenuItem& item = _tabs[_tabIndex].items[_selectedIndex];

  if (item.pickerOptions) {
    item.pickerIndex = (item.pickerIndex + 1) % item.pickerCount;
    if (item.onPickerChange) item.onPickerChange();
    if (!isVisible(_tabs[_tabIndex].items[_selectedIndex])) _selectedIndex = 0;
    updateScroll();
    draw();
  } else if (item.toggleState) {
    *item.toggleState = !*item.toggleState;
    draw();
  } else if (item.indicatorState) {
    *item.indicatorState = !*item.indicatorState;
    if (item.onToggle) item.onToggle();
    draw();
  } else if (item.subScreen) {
    Navigator::push(item.subScreen);
  } else if (item.onSelect) {
    item.onSelect();
  }
}
