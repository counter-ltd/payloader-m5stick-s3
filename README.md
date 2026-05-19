# Payloader — M5StickC S3

A pocket-sized everyday carry hacker tool built on the M5StickC S3 (ESP32-S3). Fits on a keychain. Runs FIDO2/U2F security keys over USB and Bluetooth, captures and blasts IR signals for dozens of TV brands, and gives you a live battery readout — all from a two-button menu system.

---

## Hardware

| Item | Detail |
|------|--------|
| Device | M5StickC S3 |
| Chip | ESP32-S3 |
| Display | ST7789, 135×240, portrait |
| Buttons | BtnA (front, labeled A) · BtnB (side, labeled B) |
| IR TX | G46 (built-in blaster) |
| IR RX | G42 (built-in receiver) |
| BLE | ESP32-S3 integrated |
| USB | Native USB (HID, CDC) via ESP32-S3 |

---

## Features

### APPS tab

#### Clock
Real-time clock synced from the on-board RTC at boot. Falls back to build timestamp when RTC has no valid time, and writes the build time back into RTC so subsequent reboots are accurate.

#### USB Key (FIDO2/U2F over USB HID)
Full U2F authenticator presented as a USB HID device. Works with any browser or service that supports FIDO U2F (GitHub, Google, Dropbox, etc.).

- Registration and authentication flows with user-presence confirmation on-device
- Per-credential key wrapping: private keys are AES-256 encrypted with a master key and stored as key handles — no private key material ever leaves the device unencrypted
- Master key generated randomly on first boot, stored in ESP32 NVS (not in firmware)
- ECDSA P-256 signing via mbedTLS
- Self-signed attestation certificate generated on first use, stored in NVS
- Sign counter persisted in NVS across reboots

#### BT Key (FIDO2/U2F over Bluetooth)
Same FIDO2/U2F implementation as USB Key, exposed over BLE instead of USB HID. Pair with a host once; subsequent authentications are seamless.

---

### IR tab

**Mode picker:** `Reader → Blaster → LG → Samsung → Sony → Vizio → TCL`

#### IR Reader
Captures raw IR signals in real time using IRremoteESP8266. Stores up to 10 decoded signals (protocol + value + bit count) in RAM. Duplicates are silently dropped. Each captured signal is labelled `PROTO:0xVALUE` and can be toggled active for replay in Blaster mode.

#### IR Blaster
Replays any captured signal marked active. Pauses the receiver during transmission to avoid self-capture.

#### TV Presets
Built-in IR codes for five TV brands — no capture required:

| Brand | Protocol | Notes |
|-------|----------|-------|
| LG | NEC 32-bit | Common 2010s–2020s LG TVs |
| Samsung | SAMSUNG 32-bit | Common 2010s–2020s Samsung TVs |
| Sony Bravia | SONY 12/15-bit | Power/vol/ch 12-bit; nav 15-bit |
| Vizio | NEC 32-bit (addr `0x04FB`) | Varies by model year — recapture if needed |
| TCL | NEC 32-bit (addr `0xF807`) | Varies by model year — recapture if needed |

Each brand exposes: Power, Volume (up/down/mute), Channel (up/down), Navigation (up/down/left/right/OK/back), Input, Home, Settings.

---

### BT tab

Bluetooth LE manager. Controls are gated behind an **Active** toggle.

| Item | Action |
|------|--------|
| Scan | Discovers nearby BLE devices (5-second active scan, up to 6 results) |
| Discoverable | Advertises the device so remote hosts can initiate pairing |
| Scan results | Tap a result to connect and save to NVS |
| Incoming | Incoming connection requests show a PAIR REQUEST screen — accept or deny per-device |

Paired device name and address are stored in ESP32 NVS and survive reboots.

---

### SYS tab

Live system readout:

| Item | Detail |
|------|--------|
| Battery | Percentage from PMU |
| State | `CHRG` (green) or `DISCHRG` (red) |

---

## Button Map

| Button | Action | Result |
|--------|--------|--------|
| BtnA (front) | Single press | Scroll / navigate down |
| BtnA (front) | Hold | Next tab |
| BtnA (front) | Double-tap + hold | Exit current app |
| BtnB (side) | Single press | Select / confirm |
| BtnB (side) | Multi-tap gestures | App-specific actions |
| Hardware reset | Press | Back / return to root menu |

> **Note:** Physical BtnA routes to the framework's `onBtnB()` handler and vice versa — the labels on the hardware are swapped relative to the software conventions. This is a known hardware quirk of the M5StickC S3 pin assignment.

---

## Building & Flashing

### Requirements

- Arduino IDE 2.x or arduino-cli
- [arduino-esp32](https://github.com/espressif/arduino-esp32) board package (ESP32-S3 support)
- Libraries (install via Library Manager):
  - `M5Unified`
  - `IRremoteESP8266`
  - `ESP32 BLE Arduino` (bundled with arduino-esp32)
  - `mbedTLS` (bundled with arduino-esp32)

### Board settings

| Setting | Value |
|---------|-------|
| Board | `ESP32S3 Dev Module` |
| USB Mode | `USB-OTG (TinyUSB)` |
| USB CDC On Boot | `Disabled` |
| Partition Scheme | `Default 4MB with spiffs` |
| Upload Mode | `UART0 / Hardware CDC` |

### Flash

```
# Arduino IDE: select board, set settings above, click Upload
# arduino-cli:
arduino-cli compile --fqbn esp32:esp32:esp32s3 Payloader-M5StickS3
arduino-cli upload  --fqbn esp32:esp32:esp32s3 -p /dev/ttyUSB0 Payloader-M5StickS3
```

On first boot the device generates and stores its FIDO master key and attestation certificate in NVS. This is a one-time operation; subsequent boots use the stored keys.

---

## File Structure

```
Payloader-M5StickS3/
├── Payloader-M5StickS3.ino   # Entry point — hardware init, tab registration
├── framework.h / .cpp        # Screen base class + Navigator stack
├── menu_screen.h / .cpp      # MenuScreen — tabbed UI, scrollable items, pickers
├── app_screen.h              # AppScreen base (full-screen apps)
├── apps_tab.h / .cpp         # APPS tab — Clock, USB Key, BT Key launchers
├── clock_app.h / .cpp        # Clock full-screen app
├── security_key_app.h / .cpp # FIDO U2F over USB HID — UI layer
├── bt_key_app.h / .cpp       # FIDO U2F over BLE — UI layer
├── fido_u2f.h / .cpp         # FIDO U2F core — keygen, signing, attestation
├── fido_hid.h / .cpp         # USB HID transport for FIDO
├── fido_bt.h / .cpp          # BLE transport for FIDO
├── ir_tab.h / .cpp           # IR tab — reader, blaster, TV presets
├── bt_tab.h / .cpp           # BT tab — scan, pair, discoverable
└── sys_tab.h / .cpp          # SYS tab — battery, charge state
```

### UI Framework

- **`Screen`** — abstract base: `draw()`, `onBtnA()`, `onBtnB()`, `onBtnAHold()`, `onBtnBHold()`
- **`Navigator`** — 8-deep screen stack; routes button events to the topmost screen; `push()` / `pop()`
- **`MenuScreen`** — tabbed groups with scrollable rows and inline `< value >` pickers
- **`AppScreen`** — full-screen app base with standard header/footer helpers

To add a new module: subclass `Screen` (or `AppScreen`), define a `MenuTab` and `MenuItem[]`, add to the `TABS[]` array in `Payloader-M5StickS3.ino`.

---

## Planned Modules

- **IR device database** — pre-loaded codes for audio receivers, projectors, AC units
- **BLE HID keyboard** — type payloads from the device like a wireless rubber ducky
- **NFC** (hardware expansion) — read/write NFC tags via Grove port

---

## Security Notes

- All FIDO private key material is generated on-device and never transmitted
- Master key lives in ESP32 NVS flash — physical access to the device is required to extract it
- Attestation certificates are self-signed; they identify the device class but not individual users
- IR codes for Vizio and TCL may not work on all model years — use IR Reader to capture your specific remote first

---

## License

Counter-Limitation License (CLL) v1.2 — see [LICENSE.md](LICENSE.md).

Free for personal, educational, and research use. Commercial use prohibited.
