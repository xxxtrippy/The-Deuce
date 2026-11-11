// THE DEUCE v1.1 -- WiFi-join QR generator. See .h.

#include "deuce_wifiqr.h"

#include <Arduino.h>
#include <globals.h>

#include "core/display.h"
#include "core/mykeyboard.h"
#include "modules/others/qrcode_menu.h"

// escape \ ; , : " per the WIFI: QR convention
static String esc(const String &in) {
    String o;
    for (size_t i = 0; i < in.length(); i++) {
        char c = in[i];
        if (c == '\\' || c == ';' || c == ',' || c == ':' || c == '"') o += '\\';
        o += c;
    }
    return o;
}

void deuceWifiQr() {
    String ssid = keyboard("", 32, "WiFi SSID");
    if (ssid.length() == 0) return;

    // security picker
    std::vector<Option> sec = {
        {"WPA/WPA2", []() {}},
        {"WEP",      []() {}},
        {"Open (no password)", []() {}},
    };
    int s = loopOptions(sec, MENU_TYPE_SUBMENU, "Security");
    if (s < 0) return;

    String pass = "";
    const char *T = "WPA";
    if (s == 0) { T = "WPA"; pass = keyboard("", 63, "WiFi password"); }
    else if (s == 1) { T = "WEP"; pass = keyboard("", 63, "WiFi password"); }
    else { T = "nopass"; }

    String payload = "WIFI:T:" + String(T) + ";S:" + esc(ssid) + ";";
    if (s != 2) payload += "P:" + esc(pass) + ";";
    payload += ";";

    qrcode_display(payload);
}
