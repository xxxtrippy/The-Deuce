// THE DEUCE v1.1 -- Home Network "Health Checks" suite (Others -> Health Checks).
// Defensive / audit tooling for your OWN network. Built incrementally: each module
// is flashed and verified before the next is fleshed out. Implemented so far:
//   - WiFi Hygiene scorecard
//   - Internet / Latency check
// The rest are wired into the menu and show a placeholder until their pass lands.
#include <Arduino.h>
#include <WiFi.h>
#include <globals.h>
#include "core/display.h"

// ---- wait for a tap (or the GPIO36 Back) to leave a results screen ----
// check() on this board calls InputHandler() internally, which refreshes touchPoint,
// so we can poll the tap without touching the input chip directly.
static void hcWaitTap() {
    delay(250); // swallow the press that opened this screen
    touchPoint.Clear();
    while (true) {
        check(SelPress); // drives InputHandler() -> updates touchPoint / EscPress
        if (touchPoint.pressed) {
            touchPoint.Clear();
            return;
        }
        if (EscPress) {
            EscPress = false;
            return;
        }
        delay(15);
    }
}

static void hcHeader(const char *title) {
    tft.fillScreen(bruceConfig.bgColor);
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
    tft.setTextSize(2);
    tft.drawCentreString(title, tftWidth / 2, 6, 1);
    tft.drawFastHLine(0, 26, tftWidth, bruceConfig.priColor);
    tft.setTextSize(1);
    tft.setCursor(6, 34);
}

// ============================= WiFi Hygiene =============================
static const char *hcAuthName(wifi_auth_mode_t a) {
    switch (a) {
        case WIFI_AUTH_OPEN: return "OPEN";
        case WIFI_AUTH_WEP: return "WEP";
        case WIFI_AUTH_WPA_PSK: return "WPA";
        case WIFI_AUTH_WPA2_PSK: return "WPA2";
        case WIFI_AUTH_WPA_WPA2_PSK: return "WPA/2";
        case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2-E";
        case WIFI_AUTH_WPA3_PSK: return "WPA3";
        case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA2/3";
        default: return "?";
    }
}
static void hcWifiHygiene() {
    hcHeader("WiFi Hygiene");
    tft.println("Scanning APs...");
    int n = WiFi.scanNetworks();

    hcHeader("WiFi Hygiene");
    if (n <= 0) {
        tft.println("No networks found.");
        tft.println("");
        tft.println("Tap to exit.");
        hcWaitTap();
        return;
    }
    int shown = n > 12 ? 12 : n;
    for (int i = 0; i < shown; i++) {
        wifi_auth_mode_t a = WiFi.encryptionType(i);
        uint16_t col = bruceConfig.priColor;
        if (a == WIFI_AUTH_OPEN || a == WIFI_AUTH_WEP) col = TFT_RED; // weak
        else if (a == WIFI_AUTH_WPA3_PSK || a == WIFI_AUTH_WPA2_WPA3_PSK) col = TFT_GREEN; // strong
        tft.setTextColor(col, bruceConfig.bgColor);
        String ssid = WiFi.SSID(i);
        if (ssid.length() > 13) ssid = ssid.substring(0, 13);
        tft.println(
            String(hcAuthName(a)) + " c" + String(WiFi.channel(i)) + " " + String((int)WiFi.RSSI(i)) +
            "d " + ssid
        );
    }
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
    tft.println("");
    tft.println("RED=open/WEP GRN=WPA3");
    tft.println("Tap to exit.");
    WiFi.scanDelete();
    hcWaitTap();
}

// ============================ Internet / Latency ============================
static void hcInternet() {
    hcHeader("Internet Check");
    if (!wifiConnected) {
        tft.println("Not joined to WiFi.");
        tft.println("");
        tft.println("Connect to your network");
        tft.println("in the WiFi menu first,");
        tft.println("then run this again.");
        tft.println("");
        tft.println("Tap to exit.");
        hcWaitTap();
        return;
    }
    tft.println("Probing 1.1.1.1:53 ...");
    const int TRIES = 5;
    int ok = 0;
    long tmin = 999999, tmax = 0, tsum = 0;
    for (int i = 0; i < TRIES; i++) {
        WiFiClient c;
        unsigned long t0 = millis();
        bool r = c.connect("1.1.1.1", 53, 1500);
        long dt = (long)(millis() - t0);
        if (r) {
            ok++;
            tsum += dt;
            if (dt < tmin) tmin = dt;
            if (dt > tmax) tmax = dt;
            c.stop();
        }
        delay(120);
    }

    hcHeader("Internet Check");
    tft.println("Your IP : " + WiFi.localIP().toString());
    tft.println("Gateway : " + WiFi.gatewayIP().toString());
    tft.println("DNS srv : " + WiFi.dnsIP().toString());
    tft.println("");
    if (ok == 0) {
        tft.setTextColor(TFT_RED, bruceConfig.bgColor);
        tft.println("INTERNET: DOWN (0/5)");
    } else {
        tft.setTextColor(ok == TRIES ? TFT_GREEN : bruceConfig.priColor, bruceConfig.bgColor);
        tft.printf("Reachable: %d/%d\n", ok, TRIES);
        tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
        tft.printf("Lat min/avg/max ms:\n  %ld / %ld / %ld\n", tmin, tsum / ok, tmax);
        tft.printf("Packet loss: %d%%\n", (TRIES - ok) * 100 / TRIES);
    }
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
    tft.println("");
    tft.println("Tap to exit.");
    hcWaitTap();
}

// ============================ placeholder ============================
static void hcSoon() {
    hcHeader("Coming Soon");
    tft.println("");
    tft.println("This check is next on");
    tft.println("the build list -- we add");
    tft.println("them one at a time so");
    tft.println("each is tested.");
    tft.println("");
    tft.println("Tap to exit.");
    hcWaitTap();
}

// ============================ the submenu ============================
void healthChecksMenu() {
    while (true) {
        options = {
            {"WiFi Hygiene", hcWifiHygiene},
            {"Internet/Latency", hcInternet},
            {"Device Inventory", hcSoon},
            {"Service/Port Scan", hcSoon},
            {"Attack Watchdog", hcSoon},
            {"Rogue DHCP", hcSoon},
            {"UPnP/mDNS Exposure", hcSoon},
            {"Health Report", hcSoon},
            {"Back", []() {}},
        };
        int sel = loopOptions(options, MENU_TYPE_SUBMENU, "Health Checks");
        if (sel == -1 || sel == (int)options.size() - 1) return;
    }
}
