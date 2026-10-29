// THE DEUCE v1.1 -- IR appliance library (SD code DB + browser).
//
// A universal-remote style library: an IR code database stored on SD, browsed by
// Category -> Brand -> Function, each entry transmitted via Bruce's existing IR
// senders (so every protocol sendIRCommand() understands is supported).
//
// DB file: /ir_library.txt, one entry per line, pipe-delimited:
//     Category|Brand|Function|Protocol|Address|Command
//   e.g.  TV|Samsung|Power|Samsung32|0x07|0x02
// For raw entries, use RAW with a frequency and a raw timing string:
//     Category|Brand|Function|RAW|<freqHz>|<space-separated raw durations>
//     e.g.  TV|Sony|Power|RAW|40000|2400 600 1200 600 600 ...
// '#' at the start of a line is a comment. Lines with the wrong field count are skipped.
//
// If the file does not exist on first run, a small starter DB is written so the
// feature is immediately usable and documents the format.

#include "deuce_ir_library.h"

#include <Arduino.h>
#include "core/display.h"
#include "core/mykeyboard.h"
#include "core/sd_functions.h"
#include "core/utils.h"
#include "modules/ir/custom_ir.h"
#include <globals.h>
#include <vector>
#include <SD.h>

static const char *IRLIB_FILE = "/ir_library.txt";

struct IrEntry {
    String category, brand, func, protocol, address, command;
};

// ---- starter DB (common, widely-published universal codes) ----
static const char *STARTER_DB =
    "# THE DEUCE IR appliance library\n"
    "# Format: Category|Brand|Function|Protocol|Address|Command\n"
    "# Raw:    Category|Brand|Function|RAW|<freqHz>|<raw durations>\n"
    "TV|Samsung|Power|Samsung32|0x07|0x02\n"
    "TV|Samsung|Vol+|Samsung32|0x07|0x07\n"
    "TV|Samsung|Vol-|Samsung32|0x07|0x0B\n"
    "TV|Samsung|Mute|Samsung32|0x07|0x0F\n"
    "TV|Samsung|Ch+|Samsung32|0x07|0x12\n"
    "TV|Samsung|Ch-|Samsung32|0x07|0x10\n"
    "TV|LG|Power|NEC|0x04|0x08\n"
    "TV|LG|Vol+|NEC|0x04|0x02\n"
    "TV|LG|Vol-|NEC|0x04|0x03\n"
    "TV|LG|Mute|NEC|0x04|0x09\n"
    "TV|Sony|Power|SIRC|0x01|0x15\n"
    "TV|Sony|Vol+|SIRC|0x01|0x12\n"
    "TV|Sony|Vol-|SIRC|0x01|0x13\n"
    "TV|Sony|Mute|SIRC|0x01|0x14\n"
    "TV|Panasonic|Power|NECext|0x4004|0x0100\n"
    "Projector|Epson|Power|NEC|0x55|0x90\n"
    "SoundBar|Vizio|Power|NEC|0x00|0x08\n";

static bool ensureDb() {
    if (!sdcardMounted && !setupSdCard()) return false;
    if (SD.exists(IRLIB_FILE)) return true;
    File f = SD.open(IRLIB_FILE, FILE_WRITE);
    if (!f) return false;
    f.print(STARTER_DB);
    f.close();
    return true;
}

static std::vector<IrEntry> loadDb() {
    std::vector<IrEntry> list;
    if (!ensureDb()) return list;
    File f = SD.open(IRLIB_FILE, FILE_READ);
    if (!f) return list;
    while (f.available()) {
        String line = readLineFromFile(f);
        line.trim();
        if (line.length() == 0 || line[0] == '#') continue;
        // split into exactly 6 fields on '|'
        int p[5];
        int found = 0;
        int from = 0;
        for (int i = 0; i < 5; i++) {
            int idx = line.indexOf('|', from);
            if (idx < 0) break;
            p[i] = idx;
            from = idx + 1;
            found++;
        }
        if (found != 5) continue; // malformed
        IrEntry e;
        e.category = line.substring(0, p[0]);
        e.brand = line.substring(p[0] + 1, p[1]);
        e.func = line.substring(p[1] + 1, p[2]);
        e.protocol = line.substring(p[2] + 1, p[3]);
        e.address = line.substring(p[3] + 1, p[4]);
        e.command = line.substring(p[4] + 1);
        list.push_back(e);
    }
    f.close();
    return list;
}

static bool containsStr(const std::vector<String> &v, const String &s) {
    for (const auto &x : v)
        if (x == s) return true;
    return false;
}

static void sendEntry(const IrEntry &e) {
    IRCode code;
    if (e.protocol.equalsIgnoreCase("RAW")) {
        code.type = "raw";
        code.frequency = (uint16_t)e.address.toInt(); // for RAW, address field holds freq
        code.data = e.command;                        // command field holds raw durations
    } else {
        code.protocol = e.protocol;
        code.address = e.address;
        code.command = e.command;
    }
    displayTextLine(e.brand + " " + e.func);
    sendIRCommand(&code, true);
}

static void functionMenu(const std::vector<IrEntry> &db, const String &cat, const String &brand) {
    options.clear();
    for (const auto &e : db) {
        if (e.category != cat || e.brand != brand) continue;
        IrEntry item = e;
        options.push_back({e.func.c_str(), [item]() { sendEntry(item); }});
    }
    options.push_back({"Back", []() {}});
    loopOptions(options, MENU_TYPE_SUBMENU, (cat + " / " + brand).c_str());
    options.clear();
}

static void brandMenu(const std::vector<IrEntry> &db, const String &cat) {
    std::vector<String> brands;
    for (const auto &e : db)
        if (e.category == cat && !containsStr(brands, e.brand)) brands.push_back(e.brand);

    options.clear();
    for (const auto &b : brands) {
        String brand = b;
        options.push_back({b.c_str(), [&db, cat, brand]() { functionMenu(db, cat, brand); }});
    }
    options.push_back({"Back", []() {}});
    loopOptions(options, MENU_TYPE_SUBMENU, cat.c_str());
    options.clear();
}

void deuceIrLibrary() {
    std::vector<IrEntry> db = loadDb();
    if (db.empty()) {
        displayError("No IR DB on SD", true);
        return;
    }

    std::vector<String> cats;
    for (const auto &e : db)
        if (!containsStr(cats, e.category)) cats.push_back(e.category);

    options.clear();
    for (const auto &c : cats) {
        String cat = c;
        options.push_back({c.c_str(), [&db, cat]() { brandMenu(db, cat); }});
    }
    addOptionToMainMenu();
    loopOptions(options, MENU_TYPE_SUBMENU, "IR Library");
    options.clear();
}
