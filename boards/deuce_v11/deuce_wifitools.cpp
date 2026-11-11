// THE DEUCE v1.1 -- WiFi utility tools. See .h.

#include "deuce_wifitools.h"

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <globals.h>
#include <string.h>

#include "core/display.h"
#include "core/mykeyboard.h"
#include "core/wifi/wifi_common.h"

static void waitKey() {
    while (true) {
        if (check(EscPress) || check(SelPress) || check(NextPress)) break;
        vTaskDelay(20 / portTICK_PERIOD_MS);
    }
}

// ── Wake-on-LAN ───────────────────────────────────────────────────────────────
void deuceWakeOnLan() {
    if (WiFi.status() != WL_CONNECTED && !wifiConnectMenu()) { displayError("WiFi needed", true); return; }

    static String mac = "AA:BB:CC:DD:EE:FF";
    String in = keyboard(mac, 17, "Target MAC");
    if (in.length() < 11) return;
    mac = in;
    unsigned int t[6];
    if (sscanf(mac.c_str(), "%x:%x:%x:%x:%x:%x", &t[0], &t[1], &t[2], &t[3], &t[4], &t[5]) != 6) {
        displayError("Bad MAC format", true); return;
    }
    uint8_t m[6]; for (int i = 0; i < 6; i++) m[i] = (uint8_t)t[i];
    uint8_t pkt[102];
    memset(pkt, 0xFF, 6);
    for (int i = 0; i < 16; i++) memcpy(pkt + 6 + i * 6, m, 6);

    WiFiUDP udp; udp.begin(9);
    bool ok = false;
    IPAddress bc(255, 255, 255, 255);
    for (int k = 0; k < 3; k++) { udp.beginPacket(bc, 9); udp.write(pkt, 102); if (udp.endPacket()) ok = true; delay(40); }
    // also directed subnet broadcast
    IPAddress ip = WiFi.localIP(), sub = WiFi.subnetMask();
    IPAddress dbc(ip[0] | ~sub[0], ip[1] | ~sub[1], ip[2] | ~sub[2], ip[3] | ~sub[3]);
    udp.beginPacket(dbc, 9); udp.write(pkt, 102); udp.endPacket();
    udp.stop();

    int W = tft.width(), H = tft.height();
    uint16_t bg = bruceConfig.bgColor;
    tft.fillScreen(bg);
    tft.setTextColor(ok ? TFT_GREEN : TFT_RED, bg);
    tft.setTextSize(2);
    tft.drawCentreString(ok ? "WOL sent" : "send failed", W / 2, H / 2 - 20, 1);
    tft.setTextSize(1);
    tft.setTextColor(bruceConfig.secColor, bg);
    tft.drawCentreString(mac.c_str(), W / 2, H / 2 + 6, 1);
    tft.drawCentreString("port 9 broadcast  [any key]", W / 2, H - 14, 1);
    waitKey();
}

// ── Device Finder ─────────────────────────────────────────────────────────────
void deuceDeviceFinder() {
    WiFi.mode(WIFI_STA); WiFi.disconnect();
    int n = WiFi.scanNetworks();
    if (n <= 0) { displayError("No networks", true); WiFi.scanDelete(); return; }

    uint8_t target[6] = {0}; String tname = ""; bool picked = false;
    options.clear();
    for (int i = 0; i < n && i < 24; i++) {
        String lbl = WiFi.SSID(i); if (lbl.length() == 0) lbl = "(hidden)";
        lbl += "  " + String(WiFi.RSSI(i));
        options.push_back({lbl, [&, i]() { memcpy(target, WiFi.BSSID(i), 6); tname = WiFi.SSID(i); picked = true; }});
    }
    options.push_back({"Cancel", [&]() { picked = false; }});
    loopOptions(options, MENU_TYPE_SUBMENU, "Pick device");
    if (!picked) { WiFi.scanDelete(); return; }

    int W = tft.width(), H = tft.height();
    uint16_t bg = bruceConfig.bgColor, pri = bruceConfig.priColor, sec = bruceConfig.secColor;
    tft.fillScreen(bg);
    int prev = -100, best = -200;
    while (true) {
        if (check(EscPress)) break;
        int rssi = -100;
        int n2 = WiFi.scanNetworks(false, true, false, 120);
        for (int i = 0; i < n2; i++) { if (memcmp(WiFi.BSSID(i), target, 6) == 0) { rssi = WiFi.RSSI(i); break; } }
        WiFi.scanDelete();
        if (rssi > best) best = rssi;

        tft.fillRect(0, 0, W, 17, bg);
        tft.setTextColor(pri, bg); tft.setTextSize(1);
        tft.drawCentreString("DEVICE FINDER", W / 2, 4, 1);
        tft.drawFastHLine(0, 16, W, sec);
        tft.fillRect(0, 18, W, H - 30, bg);
        tft.setTextColor(sec, bg);
        tft.drawString(tname.length() ? tname.c_str() : "(hidden)", 6, 20);
        // big rssi
        tft.setTextColor(pri, bg); tft.setTextSize(3);
        char s[12]; snprintf(s, sizeof(s), "%d", rssi);
        tft.drawString(s, 8, 34); tft.setTextSize(1); tft.drawString("dBm", 8 + tft.textWidth(s) * 3 + 2, 34);
        // warmer/colder
        if (rssi > prev + 1) { tft.setTextColor(TFT_GREEN, bg); tft.drawString("WARMER", 8, 68); }
        else if (rssi < prev - 1) { tft.setTextColor(TFT_RED, bg); tft.drawString("colder", 8, 68); }
        prev = rssi;
        // hot/cold bar
        float frac = (rssi + 100) / 70.0f; frac = frac < 0 ? 0 : frac > 1 ? 1 : frac;
        tft.drawRect(8, 84, W - 16, 14, sec);
        uint16_t c = rssi > -55 ? TFT_GREEN : rssi > -70 ? TFT_YELLOW : rssi > -85 ? TFT_ORANGE : TFT_RED;
        tft.fillRect(9, 85, (int)(frac * (W - 18)), 12, c);
        tft.setTextColor(sec, bg);
        char l[32]; snprintf(l, sizeof(l), "best %d dBm", best); tft.drawString(l, 8, 104);
        tft.drawString("[ESC]", 4, H - 11);
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
    WiFi.scanDelete();
}
