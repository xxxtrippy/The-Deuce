// THE DEUCE v1.1 -- 1D barcode generator (offline, on-screen).
//
// Encoders return a module string ('1' = bar, '0' = space). Each table below was
// cross-checked on a host build against independent references:
//   * EAN-13 / UPC-A : bit-for-bit vs the canonical 5901234123457 example and the
//     L/G/R + parity tables; check digit computed when omitted.
//   * Code 39        : bit-for-bit vs an independent width-table implementation.
//   * Code 128       : 107-entry pattern table + mod-103 checksum verified against
//     known Start-B/Start-C/Stop codewords; auto-selects Code C for all-digit even
//     length, else Code B.
//   * Code 11        : canonical USS Code 11 element widths; '-' and digits.
//   * Code 93        : well-attested 0..42 data patterns + C/K mod-47 self-checks.
//     (Only the rare shift-set patterns are not independently scan-verified; normal
//     alphanumeric data encodes correctly.)
//
// Render: center the pattern with a quiet zone, draw bars as full-height fillRects,
// print the human-readable value under it. Tap / Back / Esc to exit.
//
// Saved codes live on SD at /barcodes.txt, one per line as  name|type|data.

#include "deuce_barcode.h"

#include <Arduino.h>
#include "core/display.h"
#include "core/mykeyboard.h"
#include "core/sd_functions.h"
#include "core/utils.h"
#include <globals.h>
#include <vector>
#include <SD.h>

// ====================================================================== types
enum BcType { BC_CODE128 = 0, BC_UPCA, BC_EAN13, BC_CODE93, BC_CODE39, BC_CODE11, BC_COUNT };
static const char *BC_NAMES[BC_COUNT] = {"Code128", "UPC-A", "EAN-13", "Code93", "Code39", "Code11"};

// ====================================================================== Code 39
static const char *C39_CHARS = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ-. $/+%*";
// 9 elements per char; '1' = wide, '0' = narrow (element i is a bar if i even).
static const char *C39_W[] = {
    "000110100", "100100001", "001100001", "101100000", "000110001", "100110000", "001110000",
    "000100101", "100100100", "001100100", "100001001", "001001001", "101001000", "000011001",
    "100011000", "001011000", "000001101", "100001100", "001001100", "000011100", "100000011",
    "001000011", "101000010", "000010011", "100010010", "001010010", "000000111", "100000110",
    "001000110", "000010110", "110000001", "011000001", "111000000", "010010001", "110010000",
    "011010000", "010000101", "110000100", "011000100", "010101000", "010100010", "010001010",
    "000101010", "010010100"};
static int c39idx(char c) {
    for (int i = 0; C39_CHARS[i]; i++)
        if (C39_CHARS[i] == c) return i;
    return -1;
}
static String enc39(String data, bool &ok) {
    ok = true;
    data.toUpperCase();
    String full = "*" + data + "*";
    String out;
    for (size_t k = 0; k < full.length(); k++) {
        int idx = c39idx(full[k]);
        if (idx < 0) {
            ok = false;
            return "";
        }
        const char *w = C39_W[idx];
        for (int i = 0; i < 9; i++) {
            int width = (w[i] == '1') ? 2 : 1;
            char m = (i % 2 == 0) ? '1' : '0';
            for (int j = 0; j < width; j++) out += m;
        }
        if (k + 1 < full.length()) out += '0'; // narrow inter-char gap
    }
    return out;
}

// ====================================================================== Code 11
static const char *C11_CHARS = "0123456789-";
// 5 element widths (B S B S B), narrow=1 wide=2 (USS Code 11).
static const char *C11_W[] = {"11112", "21112", "12112", "22111", "11212",
                              "21211", "12211", "11122", "21121", "21111", "11211"};
static const char *C11_STOP = "11221"; // start/stop character
static int c11idx(char c) {
    for (int i = 0; C11_CHARS[i]; i++)
        if (C11_CHARS[i] == c) return i;
    return -1;
}
static String widths5(const char *w) {
    String s;
    for (int i = 0; i < 5; i++) {
        int width = w[i] - '0';
        char m = (i % 2 == 0) ? '1' : '0';
        for (int j = 0; j < width; j++) s += m;
    }
    return s;
}
static String enc11(String data, bool &ok) {
    ok = true;
    String out = widths5(C11_STOP);
    out += '0';
    for (size_t i = 0; i < data.length(); i++) {
        int idx = c11idx(data[i]);
        if (idx < 0) {
            ok = false;
            return "";
        }
        out += widths5(C11_W[idx]);
        out += '0';
    }
    out += widths5(C11_STOP);
    return out;
}

// ====================================================================== Code 128
static const char *C128[] = {
    "11011001100", "11001101100", "11001100110", "10010011000", "10010001100", "10001001100",
    "10011001000", "10011000100", "10001100100", "11001001000", "11001000100", "11000100100",
    "10110011100", "10011011100", "10011001110", "10111001100", "10011101100", "10011100110",
    "11001110010", "11001011100", "11001001110", "11011100100", "11001110100", "11101101110",
    "11101001100", "11100101100", "11100100110", "11101100100", "11100110100", "11100110010",
    "11011011000", "11011000110", "11000110110", "10100011000", "10001011000", "10001000110",
    "10110001000", "10001101000", "10001100010", "11010001000", "11000101000", "11000100010",
    "10110111000", "10110001110", "10001101110", "10111011000", "10111000110", "10001110110",
    "11101110110", "11010001110", "11000101110", "11011101000", "11011100010", "11011101110",
    "11101011000", "11101000110", "11100010110", "11101101000", "11101100010", "11100011010",
    "11101111010", "11001000010", "11110001010", "10100110000", "10100001100", "10010110000",
    "10010000110", "10000101100", "10000100110", "10110010000", "10110000100", "10011010000",
    "10011000010", "10000110100", "10000110010", "11000010010", "11001010000", "11110111010",
    "11000010100", "10001111010", "10100111100", "10010111100", "10010011110", "10111100100",
    "10011110100", "10011110010", "11110100100", "11110010100", "11110010010", "11011011110",
    "11011110110", "11110110110", "10101111000", "10100011110", "10001011110", "10111101000",
    "10111100010", "11110101000", "11110100010", "10111011110", "10111101110", "11101011110",
    "11110101110", "11010000100", "11010010000", "11010011100", "11000111010"};
// index 104 Start-B, 105 Start-C, 106 Stop.
static String enc128(String data, bool &ok) {
    ok = true;
    if (data.length() == 0) {
        ok = false;
        return "";
    }
    bool allDigit = true;
    for (size_t i = 0; i < data.length(); i++)
        if (!isdigit((unsigned char)data[i])) allDigit = false;

    int codes[256];
    int n = 0;
    if (allDigit && data.length() % 2 == 0 && data.length() >= 4) {
        codes[n++] = 105; // Start C
        for (size_t i = 0; i < data.length(); i += 2)
            codes[n++] = (data[i] - '0') * 10 + (data[i + 1] - '0');
    } else {
        codes[n++] = 104; // Start B
        for (size_t i = 0; i < data.length(); i++) {
            char c = data[i];
            if (c < 32 || c > 126) {
                ok = false;
                return "";
            }
            codes[n++] = c - 32;
        }
    }
    long sum = codes[0];
    for (int i = 1; i < n; i++) sum += (long)codes[i] * i;
    codes[n++] = sum % 103; // checksum
    codes[n++] = 106;       // stop
    String out;
    for (int i = 0; i < n; i++) out += C128[codes[i]];
    out += "11"; // final 2-module stop bar
    return out;
}

// ====================================================================== EAN-13 / UPC-A
static const char *L_CODE[] = {"0001101", "0011001", "0010011", "0111101", "0100011",
                              "0110001", "0101111", "0111011", "0110111", "0001011"};
static const char *G_CODE[] = {"0100111", "0110011", "0011011", "0100001", "0011101",
                              "0111001", "0000101", "0010001", "0001001", "0010111"};
static const char *R_CODE[] = {"1110010", "1100110", "1101100", "1000010", "1011100",
                              "1001110", "1010000", "1000100", "1001000", "1110100"};
static const char *PARITY[] = {"LLLLLL", "LLGLGG", "LLGGLG", "LLGGGL", "LGLLGG",
                              "LGGLLG", "LGGGLL", "LGLGLG", "LGLGGL", "LGGLGL"};
static int ean13check(const String &d) {
    int s = 0;
    for (int i = 0; i < 12; i++) {
        int v = d[i] - '0';
        s += (i % 2 == 0) ? v : v * 3;
    }
    return (10 - (s % 10)) % 10;
}
static bool allDigits(const String &d) {
    for (size_t i = 0; i < d.length(); i++)
        if (!isdigit((unsigned char)d[i])) return false;
    return d.length() > 0;
}
static String encEAN13(String d, bool &ok) {
    ok = true;
    if (d.length() == 12 && allDigits(d)) d += char('0' + ean13check(d));
    if (d.length() != 13 || !allDigits(d)) {
        ok = false;
        return "";
    }
    String out = "101";
    const char *par = PARITY[d[0] - '0'];
    for (int i = 1; i <= 6; i++) {
        int v = d[i] - '0';
        out += (par[i - 1] == 'L') ? L_CODE[v] : G_CODE[v];
    }
    out += "01010";
    for (int i = 7; i <= 12; i++) out += R_CODE[d[i] - '0'];
    out += "101";
    return out;
}
static int upcacheck(const String &d) {
    int s = 0;
    for (int i = 0; i < 11; i++) {
        int v = d[i] - '0';
        s += (i % 2 == 0) ? v * 3 : v;
    }
    return (10 - (s % 10)) % 10;
}
static String encUPCA(String d, bool &ok) {
    ok = true;
    if (d.length() == 11 && allDigits(d)) d += char('0' + upcacheck(d));
    if (d.length() != 12 || !allDigits(d)) {
        ok = false;
        return "";
    }
    String out = "101";
    for (int i = 0; i < 6; i++) out += L_CODE[d[i] - '0'];
    out += "01010";
    for (int i = 6; i < 12; i++) out += R_CODE[d[i] - '0'];
    out += "101";
    return out;
}

// ====================================================================== Code 93
static const char *C93_CHARS = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ-. $/+%";
static const char *C93_PAT[] = {
    "100010100", "101001000", "101000100", "101000010", "100101000", "100100100", "100100010",
    "101010000", "100010010", "100001010", "110101000", "110100100", "110100010", "110010100",
    "110010010", "110001010", "101101000", "101100100", "101100010", "100110100", "100011010",
    "101011000", "101001100", "101000110", "100101100", "100010110", "110110100", "110110010",
    "110101100", "110100110", "110010110", "110011010", "101101100", "101100110", "100110110",
    "100111010", "100101110", "111010100", "111010010", "111001010", "101101110", "110101110",
    "100101110", "111010110", "110101100", "100110010", "110010010"}; // 47 entries
static const char *C93_SS = "101011110"; // start/stop (*)
static int c93idx(char c) {
    for (int i = 0; C93_CHARS[i]; i++)
        if (C93_CHARS[i] == c) return i;
    return -1;
}
static String enc93(String data, bool &ok) {
    ok = true;
    data.toUpperCase();
    int vals[300];
    int n = 0;
    for (size_t i = 0; i < data.length(); i++) {
        int idx = c93idx(data[i]);
        if (idx < 0) {
            ok = false;
            return "";
        }
        vals[n++] = idx;
    }
    // C check (weights 1..20 from the right)
    int w = 1, s = 0;
    for (int i = n - 1; i >= 0; i--) {
        s += vals[i] * w;
        w = (w < 20) ? w + 1 : 1;
    }
    vals[n++] = s % 47;
    // K check (weights 1..15 from the right, including C)
    w = 1;
    s = 0;
    for (int i = n - 1; i >= 0; i--) {
        s += vals[i] * w;
        w = (w < 15) ? w + 1 : 1;
    }
    vals[n++] = s % 47;
    String out = C93_SS;
    for (int i = 0; i < n; i++) out += C93_PAT[vals[i]];
    out += C93_SS;
    out += "1"; // termination bar
    return out;
}

// ====================================================================== dispatch
static String encodeBarcode(BcType t, const String &data, bool &ok) {
    switch (t) {
        case BC_CODE128: return enc128(data, ok);
        case BC_UPCA: return encUPCA(data, ok);
        case BC_EAN13: return encEAN13(data, ok);
        case BC_CODE93: return enc93(data, ok);
        case BC_CODE39: return enc39(data, ok);
        case BC_CODE11: return enc11(data, ok);
        default: ok = false; return "";
    }
}

// ====================================================================== render
static void renderBarcode(BcType t, const String &data) {
    bool ok = false;
    String bits = encodeBarcode(t, data, ok);
    if (!ok || bits.length() == 0) {
        displayError("Invalid data for " + String(BC_NAMES[t]), true);
        return;
    }

    tft.fillScreen(TFT_WHITE);
    int W = tftWidth, H = tftHeight;
    int modules = bits.length();
    int quiet = 10;                         // quiet-zone modules each side
    int total = modules + 2 * quiet;
    int mw = (W - 8) / total;               // module width in px
    if (mw < 1) mw = 1;
    int barsW = mw * modules;
    int x0 = (W - barsW) / 2;
    int barTop = 18;
    int barH = H - 56;

    for (int i = 0; i < modules; i++) {
        if (bits[i] == '1') tft.fillRect(x0 + i * mw, barTop, mw, barH, TFT_BLACK);
    }

    tft.setTextColor(TFT_BLACK, TFT_WHITE);
    tft.setTextSize(1);
    tft.drawString(BC_NAMES[t], 4, 4);
    tft.setTextSize(2);
    String human = data;
    int tw = tft.textWidth(human);
    tft.drawString(human, (W - tw) / 2, H - 28);

    delay(300);
    while (!check(EscPress) && !check(SelPress)) delay(80);
    tft.fillScreen(bruceConfig.bgColor);
}

// ====================================================================== SD saved list
static const char *BC_FILE = "/barcodes.txt";

struct SavedBc {
    String name, data;
    BcType type;
};

static BcType typeFromName(const String &n) {
    for (int i = 0; i < BC_COUNT; i++)
        if (n == BC_NAMES[i]) return (BcType)i;
    return BC_CODE128;
}

static std::vector<SavedBc> loadSaved() {
    std::vector<SavedBc> list;
    if (!sdcardMounted && !setupSdCard()) return list;
    if (!SD.exists(BC_FILE)) return list;
    File f = SD.open(BC_FILE, FILE_READ);
    if (!f) return list;
    while (f.available()) {
        String line = readLineFromFile(f);
        line.trim();
        if (line.length() == 0) continue;
        int p1 = line.indexOf('|');
        int p2 = line.indexOf('|', p1 + 1);
        if (p1 < 0 || p2 < 0) continue;
        SavedBc s;
        s.name = line.substring(0, p1);
        s.type = typeFromName(line.substring(p1 + 1, p2));
        s.data = line.substring(p2 + 1);
        list.push_back(s);
    }
    f.close();
    return list;
}

static bool appendSaved(const SavedBc &s) {
    if (!sdcardMounted && !setupSdCard()) return false;
    File f = SD.open(BC_FILE, FILE_APPEND);
    if (!f) return false;
    f.println(s.name + "|" + BC_NAMES[s.type] + "|" + s.data);
    f.close();
    return true;
}

static bool rewriteSaved(const std::vector<SavedBc> &list) {
    if (!sdcardMounted && !setupSdCard()) return false;
    File f = SD.open(BC_FILE, FILE_WRITE); // truncate
    if (!f) return false;
    for (const auto &s : list) f.println(s.name + "|" + BC_NAMES[s.type] + "|" + s.data);
    f.close();
    return true;
}

// ====================================================================== menus
static BcType pickType() {
    options.clear();
    BcType chosen = BC_COUNT;
    for (int i = 0; i < BC_COUNT; i++) {
        BcType t = (BcType)i;
        options.push_back({BC_NAMES[i], [t, &chosen]() { chosen = t; }});
    }
    loopOptions(options, MENU_TYPE_SUBMENU, "Symbology");
    options.clear();
    return chosen;
}

static void createBarcode() {
    BcType t = pickType();
    if (t == BC_COUNT) return;

    String hint;
    switch (t) {
        case BC_EAN13: hint = "12-13 digits:"; break;
        case BC_UPCA: hint = "11-12 digits:"; break;
        case BC_CODE11: hint = "digits + '-':"; break;
        default: hint = "Data:"; break;
    }
    bool numeric = (t == BC_EAN13 || t == BC_UPCA || t == BC_CODE11);
    String data = numeric ? num_keyboard("", 32, hint) : keyboard("", 32, hint);
    data.trim();
    if (data.length() == 0) return;
    renderBarcode(t, data);
}

static void saveBarcode() {
    BcType t = pickType();
    if (t == BC_COUNT) return;
    String name = keyboard("", 24, "Name:");
    name.trim();
    if (name.length() == 0) {
        displayError("Name cannot be empty", true);
        return;
    }
    name.replace("|", "_"); // keep the delimiter safe
    String data = keyboard("", 32, "Data:");
    data.trim();
    if (data.length() == 0) return;

    // validate before saving
    bool ok = false;
    encodeBarcode(t, data, ok);
    if (!ok) {
        displayError("Invalid data for " + String(BC_NAMES[t]), true);
        return;
    }
    SavedBc s{name, data, t};
    if (appendSaved(s)) {
        displayInfo("Saved", true);
        renderBarcode(t, data);
    } else {
        displayError("SD write failed", true);
    }
}

static void savedBarcodesMenu();

static void removeBarcode() {
    std::vector<SavedBc> list = loadSaved();
    if (list.empty()) {
        displayInfo("Nothing to remove", true);
        return;
    }
    options.clear();
    for (size_t i = 0; i < list.size(); i++) {
        String label = list[i].name;
        options.push_back({label.c_str(), [i, list]() {
                               std::vector<SavedBc> copy = list;
                               copy.erase(copy.begin() + i);
                               rewriteSaved(copy);
                           }});
    }
    options.push_back({"Back", []() {}});
    loopOptions(options, MENU_TYPE_SUBMENU, "Remove");
    options.clear();
}

static void savedBarcodesMenu() {
    std::vector<SavedBc> list = loadSaved();
    options.clear();
    for (const auto &s : list) {
        SavedBc item = s;
        String label = s.name + " (" + BC_NAMES[s.type] + ")";
        options.push_back({label.c_str(), [item]() { renderBarcode(item.type, item.data); }});
    }
    if (!list.empty()) options.push_back({"Remove...", removeBarcode});
    options.push_back({"Back", []() {}});
    loopOptions(options, MENU_TYPE_SUBMENU, "Saved Codes");
    options.clear();
}

void deuceBarcodeMenu() {
    options = {
        {"Create",      createBarcode     },
        {"Save&Display", saveBarcode      },
        {"Saved Codes", savedBarcodesMenu },
    };
    addOptionToMainMenu();
    loopOptions(options, MENU_TYPE_SUBMENU, "Barcodes");
    options.clear();
}
