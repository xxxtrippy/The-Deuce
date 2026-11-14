// THE DEUCE v1.1 -- BLE HID host. See .h.
//
// Scope note: HID report descriptors vary between devices. This parses the
// report map to locate the keyboard keys, the D-pad (hat) and the buttons
// generically, which works for standard BLE keyboards and the Xbox Series/One S
// BLE controller. Odd gamepads may need the button mapping tuned in the
// Controller menu. Bluetooth-Classic devices are not supported.

#include "deuce_blehid.h"

#include <NimBLEDevice.h>
#include <Preferences.h>
#include <globals.h>

#include "core/display.h"
#include "core/main_menu.h"
#include "core/mykeyboard.h"

volatile int g_ctrlJump = -1;

// Open a top-level menu tab in response to a controller shortcut. idx 0 is the
// Guide default (Settings); the rest are the main tabs. Runs in the main loop
// (never from the BLE callback -- that only sets g_ctrlJump + returnToMenu).
void deuceHandleCtrlJump(int idx) {
    switch (idx) {
        case 0:  mainMenu.configMenu.optionsMenu(); break;  // Settings (Guide)
        case 1:  mainMenu.wifiMenu.optionsMenu(); break;
        case 2:  mainMenu.bleMenu.optionsMenu(); break;
        case 3:  mainMenu.rfMenu.optionsMenu(); break;
        case 4:  mainMenu.nrf24Menu.optionsMenu(); break;
        case 5:  mainMenu.irMenu.optionsMenu(); break;
        case 6:  mainMenu.gpsMenu.optionsMenu(); break;
        case 7:  mainMenu.i2cMenu.optionsMenu(); break;
        case 8:  mainMenu.othersMenu.optionsMenu(); break;
        case 9:  mainMenu.clockMenu.optionsMenu(); break;
        case 10: mainMenu.fileMenu.optionsMenu(); break;
        default: break;
    }
}

// ── parsed per-report layout ──────────────────────────────────────────────────
struct RptLayout {
    uint8_t reportId = 0;
    bool keyboard = false;   // report carries keyboard usage-page (0x07) keys
    int hatBit = -1;         // bit offset of the D-pad hat field (GD usage 0x39)
    int hatSize = 0;
    int btnBit = -1;         // bit offset of button 1 (Button page 0x09)
    int btnCount = 0;
    bool guide = false;      // single-button report we treat as Guide/Home
};
#define MAX_RPT 8
static RptLayout g_rpt[MAX_RPT];
static int g_nRpt = 0;

static RptLayout *layoutFor(uint8_t id) {
    for (int i = 0; i < g_nRpt; i++) if (g_rpt[i].reportId == id) return &g_rpt[i];
    if (g_nRpt < MAX_RPT) { g_rpt[g_nRpt].reportId = id; return &g_rpt[g_nRpt++]; }
    return nullptr;
}

// ── HID report-descriptor parser (enough to find kbd/hat/buttons) ─────────────
static void parseReportMap(const uint8_t *d, size_t n) {
    g_nRpt = 0;
    uint16_t usagePage = 0;
    uint8_t reportSize = 0, reportCount = 0, reportId = 0;
    int bitOff[256]; for (int i = 0; i < 256; i++) bitOff[i] = 0;
    uint32_t usageMin = 0, usageMax = 0;
    bool sawHat = false, sawGuide = false, sawKbd = false;

    size_t i = 0;
    while (i < n) {
        uint8_t b = d[i++];
        uint8_t type = (b >> 2) & 0x03;   // 1=global,2=local,0=main
        uint8_t tag = (b >> 4) & 0x0F;
        uint8_t size = b & 0x03; if (size == 3) size = 4;
        uint32_t val = 0;
        for (uint8_t k = 0; k < size && i < n; k++) val |= ((uint32_t)d[i++]) << (8 * k);

        if (type == 1) { // global
            switch (tag) {
                case 0x0: usagePage = (uint16_t)val; break;      // Usage Page
                case 0x7: reportSize = (uint8_t)val; break;      // Report Size
                case 0x8: reportId = (uint8_t)val; break;        // Report ID
                case 0x9: reportCount = (uint8_t)val; break;     // Report Count
            }
        } else if (type == 2) { // local
            switch (tag) {
                case 0x0:                                        // Usage
                    if (usagePage == 0x01 && val == 0x39) sawHat = true;
                    if (usagePage == 0x0C && val == 0x0223) sawGuide = true; // AC Home
                    if (usagePage == 0x01 && val == 0x85) sawGuide = true;   // System Main Menu
                    break;
                case 0x1: usageMin = val; break;                 // Usage Minimum
                case 0x2: usageMax = val; break;                 // Usage Maximum
            }
        } else if (type == 0) { // main
            if (tag == 0x8) {                                     // Input
                RptLayout *L = layoutFor(reportId);
                int off = bitOff[reportId];
                if (L) {
                    if (usagePage == 0x07) { L->keyboard = true; sawKbd = true; }
                    else if (usagePage == 0x09) {                 // Buttons
                        if (L->btnBit < 0) { L->btnBit = off; L->btnCount = reportCount; }
                        if (reportSize == 1 && reportCount <= 2 && L->hatBit < 0) L->guide = true;
                    } else if (usagePage == 0x01 && sawHat && L->hatBit < 0) {
                        L->hatBit = off; L->hatSize = reportSize;
                    }
                    if (sawGuide) L->guide = true;
                }
                bitOff[reportId] += (int)reportSize * (int)reportCount;
            }
            // reset locals after any main item
            sawHat = false; sawGuide = false; usageMin = usageMax = 0;
        }
    }
    (void)usageMin; (void)usageMax; (void)sawKbd;
}

// ── HID usage -> ASCII (boot keyboard) ────────────────────────────────────────
static char hidToAscii(uint8_t u, bool shift) {
    if (u >= 0x04 && u <= 0x1d) { char c = 'a' + (u - 0x04); return shift ? (c - 32) : c; }
    if (u >= 0x1e && u <= 0x26) { const char *n = "123456789"; const char *s = "!@#$%^&*("; return shift ? s[u - 0x1e] : n[u - 0x1e]; }
    if (u == 0x27) return shift ? ')' : '0';
    if (u == 0x2c) return ' ';
    if (u == 0x2d) return shift ? '_' : '-';
    if (u == 0x2e) return shift ? '+' : '=';
    if (u == 0x2f) return shift ? '{' : '[';
    if (u == 0x30) return shift ? '}' : ']';
    if (u == 0x31) return shift ? '|' : '\\';
    if (u == 0x33) return shift ? ':' : ';';
    if (u == 0x34) return shift ? '"' : '\'';
    if (u == 0x36) return shift ? '<' : ',';
    if (u == 0x37) return shift ? '>' : '.';
    if (u == 0x38) return shift ? '?' : '/';
    return 0;
}

// ── connection state ──────────────────────────────────────────────────────────
static NimBLEClient *g_client = nullptr;
static volatile bool g_connected = false;
static NimBLEAddress g_target;
static bool g_haveTarget = false;
static Preferences g_prefs;

static uint8_t bit_at(const uint8_t *d, size_t len, int bit, int size) {
    uint32_t v = 0;
    for (int k = 0; k < size; k++) {
        int b = bit + k; int byte = b >> 3, off = b & 7;
        if (byte < (int)len && (d[byte] >> off) & 1) v |= (1u << k);
    }
    return (uint8_t)v;
}

// map parsed input to Bruce's global flags / KeyStroke
static void injectReport(uint8_t reportId, const uint8_t *data, size_t len) {
    RptLayout *L = layoutFor(reportId);
    if (!L) return;

    if (L->keyboard) {
        // boot-style: [modifiers][reserved][k0..k5]
        if (len < 3) return;
        uint8_t mod = data[0];
        bool shift = mod & 0x22;
        KeyStroke.Clear();
        for (size_t k = 2; k < len && k < 8; k++) {
            uint8_t u = data[k];
            if (!u) continue;
            if (u == 0x28) { KeyStroke.enter = true; KeyStroke.pressed = true; }
            else if (u == 0x2a) { KeyStroke.del = true; KeyStroke.pressed = true; }
            else if (u == 0x29) { KeyStroke.exit_key = true; EscPress = true; }
            else { char c = hidToAscii(u, shift); if (c) { KeyStroke.word.push_back(c); KeyStroke.pressed = true; } }
        }
        // also drive nav with arrow keys
        for (size_t k = 2; k < len && k < 8; k++) {
            switch (data[k]) {
                case 0x52: UpPress = true; PrevPress = true; break;    // up arrow
                case 0x51: DownPress = true; NextPress = true; break;  // down arrow
                case 0x50: PrevPress = true; break;                    // left
                case 0x4f: NextPress = true; break;                    // right
                case 0x28: SelPress = true; break;                     // enter = select
                case 0x29: EscPress = true; break;                     // esc = back
            }
        }
        AnyKeyPress = true;
        return;
    }

    // controller
    if (L->guide && L->btnBit >= 0) {
        if (bit_at(data, len, L->btnBit, 1)) { g_ctrlJump = 0; returnToMenu = true; } // 0 = Settings
    }
    if (L->hatBit >= 0) {
        uint8_t hat = bit_at(data, len, L->hatBit, L->hatSize ? L->hatSize : 4);
        // hat: 1=N,2=NE,3=E,4=SE,5=S,6=SW,7=W,8=NW, 0/15=neutral
        if (hat == 8 || hat == 1 || hat == 2) { UpPress = true; PrevPress = true; AnyKeyPress = true; }
        if (hat == 4 || hat == 5 || hat == 6) { DownPress = true; NextPress = true; AnyKeyPress = true; }
        if (hat == 2 || hat == 3 || hat == 4) { NextPress = true; AnyKeyPress = true; } // right
        if (hat == 6 || hat == 7 || hat == 8) { PrevPress = true; AnyKeyPress = true; } // left
    }
    if (L->btnBit >= 0 && !L->guide) {
        // Xbox BLE button order (page 0x09): 1=A,2=B,3=X,4=Y,5=LB,6=RB,7=View,8=Menu
        bool A = bit_at(data, len, L->btnBit + 0, 1);
        bool B = bit_at(data, len, L->btnBit + 1, 1);
        if (A) { SelPress = true; AnyKeyPress = true; }
        if (B) { EscPress = true; AnyKeyPress = true; }
    }
}

// characteristic -> report-id cache, built once at subscribe time so the notify
// callback never does a (blocking) BLE descriptor read.
#define SYNTH_BOOT_KBD 0xFE
static NimBLERemoteCharacteristic *g_rc[MAX_RPT * 2];
static uint8_t g_rcId[MAX_RPT * 2];
static int g_nRc = 0;

static void notifyCB(NimBLERemoteCharacteristic *c, uint8_t *data, size_t len, bool isNotify) {
    uint8_t id = 0;
    for (int i = 0; i < g_nRc; i++) if (g_rc[i] == c) { id = g_rcId[i]; break; }
    injectReport(id, data, len);
}

class HidClientCB : public NimBLEClientCallbacks {
    void onConnect(NimBLEClient *c) override { g_connected = true; }
    void onDisconnect(NimBLEClient *c, int reason) override { g_connected = false; }
};
static HidClientCB g_ccb;

// subscribe to the HID service's input reports + load the report map
static bool hidSetup(NimBLEClient *c) {
    NimBLERemoteService *hid = c->getService(NimBLEUUID((uint16_t)0x1812));
    if (!hid) return false;
    NimBLERemoteCharacteristic *rmap = hid->getCharacteristic(NimBLEUUID((uint16_t)0x2A4B));
    if (rmap) { std::string m = rmap->readValue(); if (m.size()) parseReportMap((const uint8_t *)m.data(), m.size()); }
    bool any = false;
    g_nRc = 0;
    for (auto *ch : hid->getCharacteristics(true)) {
        if (ch->getUUID() == NimBLEUUID((uint16_t)0x2A4D) && ch->canNotify()) {
            // resolve this report char's report-id once, now
            uint8_t id = 0;
            NimBLERemoteDescriptor *rr = ch->getDescriptor(NimBLEUUID((uint16_t)0x2908));
            if (rr) { std::string v = rr->readValue(); if (v.size() >= 1) id = (uint8_t)v[0]; }
            if (ch->subscribe(true, notifyCB)) {
                any = true;
                if (g_nRc < (int)(sizeof(g_rc) / sizeof(g_rc[0]))) { g_rc[g_nRc] = ch; g_rcId[g_nRc] = id; g_nRc++; }
            }
        }
        // boot keyboard input report (no report map / report id)
        if (ch->getUUID() == NimBLEUUID((uint16_t)0x2A22) && ch->canNotify()) {
            RptLayout *L = layoutFor(SYNTH_BOOT_KBD);
            if (L) L->keyboard = true;
            if (ch->subscribe(true, notifyCB)) {
                any = true;
                if (g_nRc < (int)(sizeof(g_rc) / sizeof(g_rc[0]))) { g_rc[g_nRc] = ch; g_rcId[g_nRc] = SYNTH_BOOT_KBD; g_nRc++; }
            }
        }
    }
    return any;
}

static bool connectTarget() {
    if (!g_haveTarget) return false;
    if (!g_client) g_client = NimBLEDevice::createClient();
    g_client->setClientCallbacks(&g_ccb, false);
    if (!g_client->connect(g_target)) return false;
    if (!hidSetup(g_client)) { g_client->disconnect(); return false; }
    return true;
}

bool bleHidBonded() {
    g_prefs.begin("deucehid", true);
    bool has = g_prefs.isKey("addr");
    g_prefs.end();
    return has;
}

void bleHidStart() {
    if (!bleHidBonded()) return;
    g_prefs.begin("deucehid", true);
    String a = g_prefs.getString("addr", "");
    uint8_t atype = g_prefs.getUChar("atype", BLE_ADDR_PUBLIC);
    g_prefs.end();
    if (a.length() < 11) return;
    NimBLEDevice::init("Deuce");
    NimBLEDevice::setSecurityAuth(true, false, false); // bonding
    g_target = NimBLEAddress(std::string(a.c_str()), atype);
    g_haveTarget = true;
    connectTarget();
}

void bleHidStop() {
    if (g_client && g_client->isConnected()) g_client->disconnect();
    g_connected = false;
}

bool bleHidConnected() { return g_connected; }

// ── pairing screen ────────────────────────────────────────────────────────────
void deuceBleInputPair() {
    int W = tft.width(), H = tft.height();
    uint16_t bg = bruceConfig.bgColor, pri = bruceConfig.priColor, sec = bruceConfig.secColor;

    NimBLEDevice::init("Deuce");
    NimBLEDevice::setSecurityAuth(true, false, false);
    NimBLEScan *scan = NimBLEDevice::getScan();
    scan->setActiveScan(true); scan->setInterval(97); scan->setWindow(67);

    tft.fillScreen(bg); tft.setTextColor(pri, bg);
    tft.drawCentreString("Scanning for HID...", W / 2, H / 2, 1);
    NimBLEScanResults res = scan->getResults(6000, false);

    std::vector<Option> picks; std::vector<NimBLEAddress> addrs;
    for (int i = 0; i < res.getCount() && (int)picks.size() < 30; i++) {
        const NimBLEAdvertisedDevice *d = res.getDevice(i);
        bool isHid = d->isAdvertisingService(NimBLEUUID((uint16_t)0x1812)) ||
                     (d->haveAppearance() && ((d->getAppearance() & 0xff00) == 0x0300)); // HID appearance
        if (!isHid) continue;
        String name = d->getName().c_str();
        picks.push_back({(name.length() ? name : String("(hid)")) + " " + String(d->getRSSI()), []() {}});
        addrs.push_back(d->getAddress());
    }
    scan->clearResults();
    if (picks.empty()) { displayError("No BLE HID devices found", true); return; }

    int sel = loopOptions(picks, MENU_TYPE_SUBMENU, "Pair device");
    if (sel < 0 || sel >= (int)addrs.size()) return;

    g_target = addrs[sel]; g_haveTarget = true;
    tft.fillScreen(bg); tft.setTextColor(pri, bg);
    tft.drawCentreString("Connecting / bonding...", W / 2, H / 2, 1);
    if (connectTarget()) {
        g_prefs.begin("deucehid", false);
        g_prefs.putString("addr", g_target.toString().c_str());
        g_prefs.putUChar("atype", g_target.getType());
        g_prefs.end();
        displaySuccess("Paired! Input is live.", true);
    } else {
        displayError("Pairing failed", true);
    }
}
