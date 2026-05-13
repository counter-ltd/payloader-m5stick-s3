#include "ir_tab.h"
#include "framework.h"
#include <M5Unified.h>

#define IR_ENABLED 1

#if IR_ENABLED
#include <IRrecv.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>
#include <IRutils.h>
#define IR_RX_PIN           42
#define IR_TX_PIN           46
#define IR_CAPTURE_BUF_SIZE 1024
static IRrecv         irrecv(IR_RX_PIN, IR_CAPTURE_BUF_SIZE, 50, false);
static IRsend         irsend(IR_TX_PIN);
static decode_results irResults;
#endif

static const int MAX_IR_SIGNALS = 10;

static bool irReaderMode  = true;
static bool irBlasterMode = false;
static bool tvLG          = false;
static bool tvSamsung     = false;
static bool tvSony        = false;
static bool tvVizio       = false;
static bool tvTCL         = false;
static bool irActive      = false;

struct IRSignal {
  char          label[32];
  bool          active;
  decode_type_t protocol;
  uint64_t      value;
  uint16_t      bits;
};

static IRSignal irSignals[MAX_IR_SIGNALS];
static int      irSignalCount = 0;

static void onIRPickerChange();
static void rebuildAllItems();
static void onToggleReaderSlot();

static void _blastSignal(decode_type_t proto, uint64_t val, uint16_t bits) {
#if IR_ENABLED
  irrecv.pause();
  irsend.send(proto, val, bits);
  irrecv.resume();  // resume not enableIRIn — avoids gptimer leak
#endif
}

#define MAKE_DEL(N) static void delSlot##N() { \
  if (N < 0 || N >= irSignalCount) return; \
  for (int _i = N; _i < irSignalCount - 1; _i++) irSignals[_i] = irSignals[_i + 1]; \
  irSignalCount--; \
  rebuildAllItems(); \
  if (Navigator::current()) Navigator::current()->draw(); \
}
MAKE_DEL(0) MAKE_DEL(1) MAKE_DEL(2) MAKE_DEL(3) MAKE_DEL(4)
MAKE_DEL(5) MAKE_DEL(6) MAKE_DEL(7) MAKE_DEL(8) MAKE_DEL(9)
static void (*slotDelFns[MAX_IR_SIGNALS])() = {
  delSlot0, delSlot1, delSlot2, delSlot3, delSlot4,
  delSlot5, delSlot6, delSlot7, delSlot8, delSlot9
};

#define MAKE_BLAST(N) static void blastSlot##N() { \
  if (N >= irSignalCount || !irSignals[N].active) return; \
  _blastSignal(irSignals[N].protocol, irSignals[N].value, irSignals[N].bits); \
}
MAKE_BLAST(0) MAKE_BLAST(1) MAKE_BLAST(2) MAKE_BLAST(3) MAKE_BLAST(4)
MAKE_BLAST(5) MAKE_BLAST(6) MAKE_BLAST(7) MAKE_BLAST(8) MAKE_BLAST(9)
static void (*slotBlastFns[MAX_IR_SIGNALS])() = {
  blastSlot0, blastSlot1, blastSlot2, blastSlot3, blastSlot4,
  blastSlot5, blastSlot6, blastSlot7, blastSlot8, blastSlot9
};

// ── LG (NEC, 32-bit) ──────────────────────────────────────────────────────────
static void blastLGPower()    { _blastSignal(NEC, 0x20DF10EF, 32); }
static void blastLGVolUp()    { _blastSignal(NEC, 0x20DF40BF, 32); }
static void blastLGVolDn()    { _blastSignal(NEC, 0x20DFC03F, 32); }
static void blastLGMute()     { _blastSignal(NEC, 0x20DF906F, 32); }
static void blastLGChUp()     { _blastSignal(NEC, 0x20DF00FF, 32); }
static void blastLGChDn()     { _blastSignal(NEC, 0x20DF807F, 32); }
static void blastLGUp()       { _blastSignal(NEC, 0x20DF02FD, 32); }
static void blastLGDn()       { _blastSignal(NEC, 0x20DF827D, 32); }
static void blastLGLeft()     { _blastSignal(NEC, 0x20DFE01F, 32); }
static void blastLGRight()    { _blastSignal(NEC, 0x20DF609F, 32); }
static void blastLGOK()       { _blastSignal(NEC, 0x20DF22DD, 32); }
static void blastLGBack()     { _blastSignal(NEC, 0x20DF14EB, 32); }
static void blastLGInput()    { _blastSignal(NEC, 0x20DFD02F, 32); }
static void blastLGHome()     { _blastSignal(NEC, 0x20DF3EC1, 32); }
static void blastLGSettings() { _blastSignal(NEC, 0x20DFC23D, 32); }

// ── Samsung (SAMSUNG protocol, 32-bit) ────────────────────────────────────────
static void blastSAPower()    { _blastSignal(SAMSUNG, 0xE0E040BF, 32); }
static void blastSAVolUp()    { _blastSignal(SAMSUNG, 0xE0E0E01F, 32); }
static void blastSAVolDn()    { _blastSignal(SAMSUNG, 0xE0E0D02F, 32); }
static void blastSAMute()     { _blastSignal(SAMSUNG, 0xE0E0F00F, 32); }
static void blastSAChUp()     { _blastSignal(SAMSUNG, 0xE0E048B7, 32); }
static void blastSAChDn()     { _blastSignal(SAMSUNG, 0xE0E008F7, 32); }
static void blastSAUp()       { _blastSignal(SAMSUNG, 0xE0E006F9, 32); }
static void blastSADn()       { _blastSignal(SAMSUNG, 0xE0E08679, 32); }
static void blastSALeft()     { _blastSignal(SAMSUNG, 0xE0E0A659, 32); }
static void blastSARight()    { _blastSignal(SAMSUNG, 0xE0E046B9, 32); }
static void blastSAOK()       { _blastSignal(SAMSUNG, 0xE0E016E9, 32); }
static void blastSABack()     { _blastSignal(SAMSUNG, 0xE0E01AE5, 32); }
static void blastSAInput()    { _blastSignal(SAMSUNG, 0xE0E0807F, 32); }
static void blastSAHome()     { _blastSignal(SAMSUNG, 0xE0E09E61, 32); }
static void blastSASettings() { _blastSignal(SAMSUNG, 0xE0E058A7, 32); }

// ── Sony Bravia (SONY protocol; power/vol/ch 12-bit, nav 15-bit — may vary by model year) ──
static void blastSONYPower()    { _blastSignal(SONY, 0xA90, 12); }
static void blastSONYVolUp()    { _blastSignal(SONY, 0x490, 12); }
static void blastSONYVolDn()    { _blastSignal(SONY, 0xC90, 12); }
static void blastSONYMute()     { _blastSignal(SONY, 0x290, 12); }
static void blastSONYChUp()     { _blastSignal(SONY, 0x090, 12); }
static void blastSONYChDn()     { _blastSignal(SONY, 0x890, 12); }
static void blastSONYUp()       { _blastSignal(SONY, 0x9EB, 15); }
static void blastSONYDn()       { _blastSignal(SONY, 0x5EB, 15); }
static void blastSONYLeft()     { _blastSignal(SONY, 0x1EB, 15); }
static void blastSONYRight()    { _blastSignal(SONY, 0xBEB, 15); }
static void blastSONYOK()       { _blastSignal(SONY, 0x65B, 15); }
static void blastSONYBack()     { _blastSignal(SONY, 0x14B, 15); }
static void blastSONYInput()    { _blastSignal(SONY, 0xA50, 12); }
static void blastSONYHome()     { _blastSignal(SONY, 0x070, 15); }
static void blastSONYSettings() { _blastSignal(SONY, 0x5C6, 15); }

// ── Vizio (NEC, 32-bit, addr=0x04FB — may vary by model year; recapture with IR Reader if needed) ──
static void blastVZPower()    { _blastSignal(NEC, 0x04FB48B7, 32); }
static void blastVZVolUp()    { _blastSignal(NEC, 0x04FB58A7, 32); }
static void blastVZVolDn()    { _blastSignal(NEC, 0x04FBD827, 32); }
static void blastVZMute()     { _blastSignal(NEC, 0x04FB08F7, 32); }
static void blastVZChUp()     { _blastSignal(NEC, 0x04FB9867, 32); }
static void blastVZChDn()     { _blastSignal(NEC, 0x04FBE817, 32); }
static void blastVZUp()       { _blastSignal(NEC, 0x04FB02FD, 32); }
static void blastVZDn()       { _blastSignal(NEC, 0x04FB827D, 32); }
static void blastVZLeft()     { _blastSignal(NEC, 0x04FBE01F, 32); }
static void blastVZRight()    { _blastSignal(NEC, 0x04FB609F, 32); }
static void blastVZOK()       { _blastSignal(NEC, 0x04FB22DD, 32); }
static void blastVZBack()     { _blastSignal(NEC, 0x04FB14EB, 32); }
static void blastVZInput()    { _blastSignal(NEC, 0x04FB0EF1, 32); }
static void blastVZHome()     { _blastSignal(NEC, 0x04FB3EC1, 32); }
static void blastVZSettings() { _blastSignal(NEC, 0x04FBC23D, 32); }

// ── TCL (NEC, 32-bit, addr=0xF807 — varies by model; recapture with IR Reader if needed) ──
static void blastTCLPower()    { _blastSignal(NEC, 0xF807C03F, 32); }
static void blastTCLVolUp()    { _blastSignal(NEC, 0xF807A05F, 32); }
static void blastTCLVolDn()    { _blastSignal(NEC, 0xF807609F, 32); }
static void blastTCLMute()     { _blastSignal(NEC, 0xF807E01F, 32); }
static void blastTCLChUp()     { _blastSignal(NEC, 0xF807807F, 32); }
static void blastTCLChDn()     { _blastSignal(NEC, 0xF807D02F, 32); }
static void blastTCLUp()       { _blastSignal(NEC, 0xF80740BF, 32); }
static void blastTCLDn()       { _blastSignal(NEC, 0xF8070AF5, 32); }
static void blastTCLLeft()     { _blastSignal(NEC, 0xF80710EF, 32); }
static void blastTCLRight()    { _blastSignal(NEC, 0xF807906F, 32); }
static void blastTCLOK()       { _blastSignal(NEC, 0xF807708F, 32); }
static void blastTCLBack()     { _blastSignal(NEC, 0xF807B04F, 32); }
static void blastTCLInput()    { _blastSignal(NEC, 0xF807F00F, 32); }
static void blastTCLHome()     { _blastSignal(NEC, 0xF8072AD5, 32); }
static void blastTCLSettings() { _blastSignal(NEC, 0xF8074AB5, 32); }

static const char* IR_OPTIONS[] = { "Reader", "Blaster", "LG", "Samsung", "Sony", "Vizio", "TCL" };
static const int   IR_OPT_COUNT = 7;

static const int READER_BASE  = 2;
static const int BLASTER_BASE = 2 + MAX_IR_SIGNALS;       // 12
static const int LG_BASE      = 2 + 2 * MAX_IR_SIGNALS;   // 22
static const int SAMSUNG_BASE = LG_BASE + 8;               // 30
static const int SONY_BASE    = SAMSUNG_BASE + 8;          // 38
static const int VIZIO_BASE   = SONY_BASE + 8;             // 46
static const int TCL_BASE     = VIZIO_BASE + 8;            // 54
static const int IR_ITEMS_TOTAL = 2 + 2 * MAX_IR_SIGNALS + 5 * 8;  // 62

static MenuItem IR_ITEMS[IR_ITEMS_TOTAL];
static int      irItemCount = IR_ITEMS_TOTAL;
static bool     s_false     = false;

// Emit 8 standard TV items at base with showWhen pointer and brand-specific blast fns.
#define FILL_TV_ITEMS(base, show, pfx) \
  IR_ITEMS[(base)+0] = { "Power",      nullptr,nullptr,0,0,nullptr,nullptr,nullptr,(show),nullptr,blast##pfx##Power }; \
  IR_ITEMS[(base)+1] = { "Volume",     nullptr,nullptr,0,0,nullptr,nullptr,nullptr,(show),nullptr,nullptr,nullptr,nullptr,nullptr,blast##pfx##VolUp,blast##pfx##VolDn,blast##pfx##Mute }; \
  IR_ITEMS[(base)+2] = { "Channel",    nullptr,nullptr,0,0,nullptr,nullptr,nullptr,(show),nullptr,nullptr,nullptr,nullptr,nullptr,blast##pfx##ChUp, blast##pfx##ChDn }; \
  IR_ITEMS[(base)+3] = { "Up/Down",    nullptr,nullptr,0,0,nullptr,nullptr,nullptr,(show),nullptr,nullptr,nullptr,nullptr,nullptr,blast##pfx##Up,   blast##pfx##Dn,   blast##pfx##OK,blast##pfx##Back }; \
  IR_ITEMS[(base)+4] = { "Left/Right", nullptr,nullptr,0,0,nullptr,nullptr,nullptr,(show),nullptr,nullptr,nullptr,nullptr,nullptr,blast##pfx##Left, blast##pfx##Right,blast##pfx##OK,blast##pfx##Back }; \
  IR_ITEMS[(base)+5] = { "Input",      nullptr,nullptr,0,0,nullptr,nullptr,nullptr,(show),nullptr,blast##pfx##Input }; \
  IR_ITEMS[(base)+6] = { "Home",       nullptr,nullptr,0,0,nullptr,nullptr,nullptr,(show),nullptr,blast##pfx##Home }; \
  IR_ITEMS[(base)+7] = { "Settings",   nullptr,nullptr,0,0,nullptr,nullptr,nullptr,(show),nullptr,blast##pfx##Settings };

static void rebuildAllItems() {
  IR_ITEMS[0] = { nullptr, nullptr, IR_OPTIONS, IR_OPT_COUNT, IR_ITEMS[0].pickerIndex, onIRPickerChange };
  IR_ITEMS[1] = { "Active", nullptr, nullptr, 0, 0, nullptr, &irActive, nullptr, &irReaderMode };

  for (int i = 0; i < MAX_IR_SIGNALS; i++) {
    if (i < irSignalCount) {
      IR_ITEMS[READER_BASE + i] = {
        irSignals[i].label, nullptr,
        nullptr, 0, 0, nullptr,
        nullptr,
        &irSignals[i].active,
        &irReaderMode,
        slotDelFns[i],
        nullptr,
        onToggleReaderSlot
      };
    } else {
      IR_ITEMS[READER_BASE + i] = {
        nullptr, nullptr,
        nullptr, 0, 0, nullptr,
        nullptr, nullptr,
        &s_false
      };
    }
  }

  for (int i = 0; i < MAX_IR_SIGNALS; i++) {
    bool active = (i < irSignalCount) && irSignals[i].active;
    if (active) {
      IR_ITEMS[BLASTER_BASE + i] = {
        irSignals[i].label, nullptr,
        nullptr, 0, 0, nullptr,
        nullptr, nullptr,
        &irBlasterMode,
        nullptr,
        slotBlastFns[i]
      };
    } else {
      IR_ITEMS[BLASTER_BASE + i] = {
        nullptr, nullptr,
        nullptr, 0, 0, nullptr,
        nullptr, nullptr,
        &s_false
      };
    }
  }

  FILL_TV_ITEMS(LG_BASE,      &tvLG,      LG)
  FILL_TV_ITEMS(SAMSUNG_BASE, &tvSamsung, SA)
  FILL_TV_ITEMS(SONY_BASE,    &tvSony,    SONY)
  FILL_TV_ITEMS(VIZIO_BASE,   &tvVizio,   VZ)
  FILL_TV_ITEMS(TCL_BASE,     &tvTCL,     TCL)

  irItemCount = IR_ITEMS_TOTAL;
}

static void onToggleReaderSlot() {
  rebuildAllItems();
}

static void onIRPickerChange() {
  int idx       = IR_ITEMS[0].pickerIndex;
  irReaderMode  = (idx == 0);
  irBlasterMode = (idx == 1);
  tvLG          = (idx == 2);
  tvSamsung     = (idx == 3);
  tvSony        = (idx == 4);
  tvVizio       = (idx == 5);
  tvTCL         = (idx == 6);
  irActive      = false;
}

static void onIRLeave() { irActive = false; }

MenuTab irMenuTab = { "IR", IR_ITEMS, IR_ITEMS_TOTAL, &irItemCount, onIRLeave, 0xFF0000 };

static bool isDuplicate(const char* label) {
  for (int i = 0; i < irSignalCount; i++) {
    if (strncmp(irSignals[i].label, label, 31) == 0) return true;
  }
  return false;
}

static void addIRSignal(decode_type_t proto, uint64_t val, uint16_t bits) {
  if (proto == UNKNOWN || bits < 16) return;  // filter noise
  if (irSignalCount >= MAX_IR_SIGNALS) return;
  char label[32];
  snprintf(label, sizeof(label), "%s:0x%llX", typeToString(proto).c_str(), (unsigned long long)val);
  if (isDuplicate(label)) return;
  IRSignal& s = irSignals[irSignalCount];
  strncpy(s.label, label, 31);
  s.label[31] = '\0';
  s.active    = false;
  s.protocol  = proto;
  s.value     = val;
  s.bits      = bits;
  irSignalCount++;
  rebuildAllItems();
}

void irTabSetup() {
  rebuildAllItems();
#if IR_ENABLED
  irrecv.enableIRIn();
  irsend.begin();
#endif
}

void irTabUpdate() {
#if IR_ENABLED
  if (!irActive || !irReaderMode) return;
  if (irrecv.decode(&irResults)) {
    irrecv.resume();
    addIRSignal(irResults.decode_type, irResults.value, irResults.bits);
    Navigator::current()->draw();
  }
#endif
}
