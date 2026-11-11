// THE DEUCE v1.1 -- BLE recon tools. See .h.

#include "deuce_bletools.h"

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <globals.h>

#include "core/display.h"
#include "core/mykeyboard.h"

// ── tracker classification ────────────────────────────────────────────────────
static const char *classifyTracker(const NimBLEAdvertisedDevice *d) {
    // service UUIDs (Tile / Samsung SmartTag)
    if (d->haveServiceUUID()) {
        std::string u = d->getServiceUUID().toString();
        if (u.find("feed") != std::string::npos || u.find("feec") != std::string::npos) return "Tile";
        if (u.find("fd5a") != std::string::npos) return "Samsung SmartTag";
        if (u.find("fd44") != std::string::npos) return "AirTag (FindMy)";
    }
    // service data UUIDs
    for (int i = 0; i < d->getServiceDataCount(); i++) {
        std::string u = d->getServiceDataUUID(i).toString();
        if (u.find("fd5a") != std::string::npos) return "Samsung SmartTag";
        if (u.find("feed") != std::string::npos) return "Tile";
    }
    // Apple manufacturer data -> FindMy / AirTag
    if (d->haveManufacturerData()) {
        std::string m = d->getManufacturerData();
        if (m.length() >= 3) {
            const uint8_t *b = (const uint8_t *)m.data();
            uint16_t company = b[0] | (b[1] << 8);
            if (company == 0x004C) {
                if (b[2] == 0x12) return "AirTag/FindMy";   // FindMy network
                if (b[2] == 0x07) return "Apple (pairing)"; // nearby/pairing
            }
            if (company == 0x0075 && m.length() >= 3) return "Samsung dev";
        }
    }
    return nullptr;
}

struct TrackSeen { String addr; String type; int rssi; int rounds; };

void deuceBleTracker() {
    int W = tft.width(), H = tft.height();
    uint16_t bg = bruceConfig.bgColor, pri = bruceConfig.priColor, sec = bruceConfig.secColor;

    NimBLEDevice::init("Deuce");
    NimBLEScan *scan = NimBLEDevice::getScan();
    scan->setActiveScan(true);
    scan->setInterval(97);
    scan->setWindow(67);

    std::vector<TrackSeen> seen;
    int round = 0;

    while (true) {
        if (check(EscPress)) break;

        tft.fillScreen(bg);
        tft.setTextColor(pri, bg); tft.setTextSize(1);
        tft.drawCentreString("BLE TRACKER SCAN", W / 2, 3, 1);
        tft.drawFastHLine(0, 15, W, sec);
        tft.setTextColor(sec, bg); tft.drawString("scanning...", 6, 18);

        NimBLEScanResults res = scan->getResults(4000, false);
        round++;
        scan->clearResults();

        // mark all not-seen-this-round
        for (auto &s : seen) s.rssi = s.rssi; // keep
        for (int i = 0; i < res.getCount(); i++) {
            const NimBLEAdvertisedDevice *d = res.getDevice(i);
            const char *cls = classifyTracker(d);
            if (!cls) continue;
            String addr = d->getAddress().toString().c_str();
            bool found = false;
            for (auto &s : seen) {
                if (s.addr == addr) { s.rssi = d->getRSSI(); s.rounds++; s.type = cls; found = true; break; }
            }
            if (!found && seen.size() < 60) seen.push_back({addr, cls, d->getRSSI(), 1});
        }

        // draw
        tft.fillScreen(bg);
        tft.setTextColor(pri, bg);
        tft.drawCentreString("BLE TRACKER SCAN", W / 2, 3, 1);
        tft.drawFastHLine(0, 15, W, sec);
        char hdr[40]; snprintf(hdr, sizeof(hdr), "round %d   trackers: %d", round, (int)seen.size());
        tft.setTextColor(sec, bg); tft.drawString(hdr, 6, 18);

        int y = 32;
        for (int i = 0; i < (int)seen.size() && y < H - 22; i++) {
            TrackSeen &s = seen[i];
            bool following = s.rounds >= 3;
            tft.setTextColor(following ? TFT_RED : pri, bg);
            tft.drawString(s.type, 6, y);
            tft.setTextColor(sec, bg);
            char r[20]; snprintf(r, sizeof(r), "%d x%d", s.rssi, s.rounds);
            tft.drawRightString(r, W - 6, y, 1);
            if (following) { tft.setTextColor(TFT_RED, bg); tft.drawString("FOLLOWING?", 150, y); }
            y += 11;
        }
        if (seen.empty()) { tft.setTextColor(sec, bg); tft.drawCentreString("no trackers seen yet", W / 2, 60, 1); }
        tft.setTextColor(sec, bg); tft.drawString("persist>=3 rounds = red  [ESC]", 4, H - 11);
    }
    NimBLEDevice::deinit(true);
}

// ── GATT explorer ─────────────────────────────────────────────────────────────
static void gattPager(const std::vector<String> &lines) {
    int W = tft.width(), H = tft.height();
    uint16_t bg = bruceConfig.bgColor, pri = bruceConfig.priColor, sec = bruceConfig.secColor;
    int scroll = 0, rows = (H - 30) / 11;
    while (true) {
        if (check(EscPress) || check(SelPress)) break;
        if (check(DownPress) && scroll < (int)lines.size() - rows) scroll++;
        if (check(UpPress) && scroll > 0) scroll--;
        tft.fillScreen(bg);
        tft.setTextColor(pri, bg); tft.drawCentreString("GATT", W / 2, 3, 1);
        tft.drawFastHLine(0, 15, W, sec);
        int y = 20;
        for (int i = scroll; i < (int)lines.size() && y < H - 12; i++) {
            const String &l = lines[i];
            tft.setTextColor(l.startsWith("svc") ? TFT_CYAN : l.startsWith("!") ? TFT_ORANGE : pri, bg);
            tft.drawString(l.startsWith("!") ? l.substring(1) : l, 6, y);
            y += 11;
        }
        tft.setTextColor(sec, bg);
        tft.drawString((int)lines.size() > rows ? "[UP/DN] scroll [SEL/ESC] back" : "[SEL/ESC] back", 4, H - 11);
        vTaskDelay(40 / portTICK_PERIOD_MS);
    }
}

void deuceBleGatt() {
    int W = tft.width(), H = tft.height();
    uint16_t bg = bruceConfig.bgColor, pri = bruceConfig.priColor, sec = bruceConfig.secColor;

    NimBLEDevice::init("Deuce");
    NimBLEScan *scan = NimBLEDevice::getScan();
    scan->setActiveScan(true);
    scan->setInterval(97); scan->setWindow(67);

    tft.fillScreen(bg); tft.setTextColor(pri, bg);
    tft.drawCentreString("Scanning 5s...", W / 2, H / 2, 1);
    NimBLEScanResults res = scan->getResults(5000, false);

    // build a picker
    std::vector<Option> picks;
    std::vector<NimBLEAddress> addrs;
    for (int i = 0; i < res.getCount() && (int)picks.size() < 30; i++) {
        const NimBLEAdvertisedDevice *d = res.getDevice(i);
        String name = d->getName().c_str();
        String label = (name.length() ? name : String("(no name)")) + " " + String(d->getRSSI());
        picks.push_back({label.c_str(), []() {}});
        addrs.push_back(d->getAddress());
    }
    if (picks.empty()) { scan->clearResults(); NimBLEDevice::deinit(true); displayError("No BLE devices", true); return; }

    int sel = loopOptions(picks, MENU_TYPE_SUBMENU, "Pick device");
    if (sel < 0 || sel >= (int)addrs.size()) { scan->clearResults(); NimBLEDevice::deinit(true); return; }
    NimBLEAddress target = addrs[sel];
    scan->clearResults();

    tft.fillScreen(bg); tft.setTextColor(pri, bg);
    tft.drawCentreString("Connecting...", W / 2, H / 2, 1);

    std::vector<String> out;
    NimBLEClient *c = NimBLEDevice::createClient();
    if (!c->connect(target)) {
        out.push_back("!connect failed");
        out.push_back(target.toString().c_str());
    } else {
        out.push_back(String("dev ") + target.toString().c_str());
        const std::vector<NimBLERemoteService *> &svcs = c->getServices(true);
        for (auto *svc : svcs) {
            out.push_back("svc " + String(svc->getUUID().toString().c_str()));
            const std::vector<NimBLERemoteCharacteristic *> &chs = svc->getCharacteristics(true);
            for (auto *ch : chs) {
                String props = "";
                if (ch->canRead()) props += "R";
                if (ch->canWrite()) props += "W";
                if (ch->canNotify()) props += "N";
                String line = " " + String(ch->getUUID().toString().c_str()) + " [" + props + "]";
                out.push_back(line);
                if (ch->canRead()) {
                    std::string v = ch->readValue();
                    if (v.length()) {
                        String disp = "  = ";
                        bool printable = true;
                        for (size_t k = 0; k < v.length() && k < 24; k++) if (v[k] < 32 || v[k] > 126) { printable = false; break; }
                        if (printable) disp += String(v.c_str()).substring(0, 24);
                        else { for (size_t k = 0; k < v.length() && k < 12; k++) { char hx[4]; snprintf(hx, sizeof(hx), "%02X", (uint8_t)v[k]); disp += hx; } }
                        out.push_back(disp);
                    }
                }
            }
        }
        c->disconnect();
    }
    NimBLEDevice::deleteClient(c);
    NimBLEDevice::deinit(true);
    gattPager(out);
}
