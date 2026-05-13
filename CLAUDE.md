# Payloader-M5StickS3

Firmware for an M5StickC S3 (ESP32-S3) built as a pocket-sized everyday carry hacker tool.

## Hardware

- **Device:** M5StickC S3
- **Chip:** ESP32-S3
- **Display:** ST7789, portrait orientation, driven via M5Unified
- **Buttons:**
  - Physical BtnA (G11, front, labeled A) — select/confirm: single press = select, multi-tap gestures; routes to virtual `onBtnB()`
  - Physical BtnB (G12, side, labeled B) — nav: short press = scroll, hold = next tab, double-tap+hold = exit app; routes to virtual `onBtnA()` / `onBtnAHoldAlt()`
  - Hardware reset button — back / return to root

## Firmware

Arduino sketch using **M5Unified** (handles PMU, backlight, display init — do not use bare M5GFX, it causes watchdog resets on S3).

### File structure

| File | Role |
|---|---|
| `Payloader-M5StickS3.ino` | Entry point — hardware init, menu content definitions |
| `framework.h/cpp` | `Screen` base class + `Navigator` stack (push/pop screens) |
| `menu_screen.h/cpp` | `MenuScreen` — tabbed menu with inline pickers |

### UI framework

- `Screen` — abstract base: `draw()`, `onBtnA/B()`, `onBtnAHold/BHold()`
- `Navigator` — 8-deep screen stack, routes button events to current screen
- `MenuScreen` — tabbed groups, scrollable items, inline `< value >` pickers
- New screen types: subclass `Screen`, push via `Navigator::push(&screen)`

### Menu layout

- Blue title bar (tab name)
- Scrollable item rows — regular items or `< picker >` rows
- Bottom tab indicator squares (blue = active)

## Pin Map

| GPIO | Function | Notes |
|------|----------|-------|
| G11 | KEY1 (BtnA / front button, labeled A) | |
| G12 | KEY2 (BtnB / side button, labeled B) | |
| G39 | LCD MOSI | ST7789P3 |
| G40 | LCD SCK | ST7789P3 |
| G45 | LCD RS (D/C) | ST7789P3 |
| G41 | LCD CS | ST7789P3 |
| G21 | LCD RST | ST7789P3 |
| G38 | LCD BL (backlight) | ST7789P3 |
| G46 | IR TX | IR blaster out |
| G42 | IR RX | IR receiver in |
| G47 | I2C SDA | IMU + audio codec |
| G48 | I2C SCL | IMU + audio codec |
| G18 | Audio MCLK | ES8311 (0x18) |
| G14 | Audio DOUT | ES8311 |
| G17 | Audio BCLK | ES8311 |
| G15 | Audio LRCK | ES8311 |
| G16 | Audio DIN | ES8311 |
| G9  | PORT.A yellow | HY2.0-4P Grove |
| G10 | PORT.A white | HY2.0-4P Grove |
| G0  | PYG0 CHG_STAT | M5PM1 power IC |
| G1  | PYG1 IRQ | M5PM1 power IC |
| G2  | PYG2 L3B_EN | M5PM1 power IC |
| G3  | PYG3 SPK_Pulse | M5PM1 power IC |
| G4  | PYG4 IMU_INT | M5PM1 / BMI270 |

**ICs:** BMI270 IMU (0x68), M5PM1 power (0x6E), ES8311 audio codec (0x18)

## Planned modules

- **Infrared** — IR reader, blaster, device presets (LG-TV etc.)
- **More TBD**
