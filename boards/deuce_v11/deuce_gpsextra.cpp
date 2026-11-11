// THE DEUCE v1.1 -- extra GPS tools. See .h.

#include "deuce_gpsextra.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <TinyGPS++.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <globals.h>
#include <math.h>

#include "core/display.h"
#include "core/wifi/wifi_common.h"

// ── shared GPS reader (mirrors the GPS tracker init) ──────────────────────────
static HardwareSerial GPSser(2);
static TinyGPSPlus gps;
static bool g_rxReleased = false;

static void gReleasePins() {
    g_rxReleased = false;
    if (bruceConfigPins.CC1101_bus.checkConflict(bruceConfigPins.gps_bus.rx) ||
        bruceConfigPins.NRF24_bus.checkConflict(bruceConfigPins.gps_bus.rx) ||
        bruceConfigPins.SDCARD_bus.checkConflict(bruceConfigPins.gps_bus.rx)) {
        pinMode(bruceConfigPins.gps_bus.rx, INPUT);
        g_rxReleased = true;
    }
}
static void gRestorePins() {
    if (!g_rxReleased) return;
    pinMode(bruceConfigPins.gps_bus.rx, OUTPUT);
    if (bruceConfigPins.gps_bus.rx == bruceConfigPins.CC1101_bus.cs ||
        bruceConfigPins.gps_bus.rx == bruceConfigPins.NRF24_bus.cs ||
        bruceConfigPins.gps_bus.rx == bruceConfigPins.SDCARD_bus.cs)
        digitalWrite(bruceConfigPins.gps_bus.rx, HIGH);
    else
        digitalWrite(bruceConfigPins.gps_bus.rx, LOW);
    g_rxReleased = false;
}
static void gGpsBegin() {
#ifdef IO_EXP_GPS
    ioExpander.turnPinOnOff(IO_EXP_GPS, HIGH);
#endif
    gReleasePins();
    pinMode(bruceConfigPins.gps_bus.rx, INPUT);
    GPSser.begin(bruceConfigPins.gpsBaudrate, SERIAL_8N1, bruceConfigPins.gps_bus.rx,
                 bruceConfigPins.gps_bus.tx);
}
static void gGpsEnd() {
    GPSser.end();
    gRestorePins();
#ifdef IO_EXP_GPS
    ioExpander.turnPinOnOff(IO_EXP_GPS, LOW);
#endif
}
static void gGpsFeed() { while (GPSser.available() > 0) gps.encode(GPSser.read()); }

static double haversine(double la1, double lo1, double la2, double lo2) {
    double R = 6371000.0, p = M_PI / 180.0;
    double dla = (la2 - la1) * p, dlo = (lo2 - lo1) * p;
    double a = sin(dla / 2) * sin(dla / 2) +
               cos(la1 * p) * cos(la2 * p) * sin(dlo / 2) * sin(dlo / 2);
    return 2 * R * atan2(sqrt(a), sqrt(1 - a));
}

static void gWaitFix(const char *what) {
    int W = tft.width(), H = tft.height();
    tft.fillScreen(bruceConfig.bgColor);
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
    while (!(gps.location.isValid() && gps.satellites.value() >= 3)) {
        if (check(EscPress)) return;
        gGpsFeed();
        tft.fillRect(0, H / 2 - 20, W, 40, bruceConfig.bgColor);
        tft.drawCentreString("Waiting for GPS fix...", W / 2, H / 2 - 14, 1);
        char s[24]; snprintf(s, sizeof(s), "sats: %lu", gps.satellites.value());
        tft.drawCentreString(s, W / 2, H / 2 + 2, 1);
        tft.drawCentreString(what, W / 2, H / 2 + 18, 1);
        vTaskDelay(150 / portTICK_PERIOD_MS);
    }
}

// ── Lap Timer ─────────────────────────────────────────────────────────────────
static void fmtTime(uint32_t ms, char *out, size_t n) {
    uint32_t t = ms / 10; // centiseconds
    uint32_t cs = t % 100; uint32_t s = (t / 100) % 60; uint32_t m = t / 6000;
    snprintf(out, n, "%lu:%02lu.%02lu", (unsigned long)m, (unsigned long)s, (unsigned long)cs);
}

void deuceLapTimer() {
    int W = tft.width(), H = tft.height();
    uint16_t bg = bruceConfig.bgColor, pri = bruceConfig.priColor, sec = bruceConfig.secColor;
    gGpsBegin();
    gWaitFix("[SEL] sets start/finish");

    double gateLat = 0, gateLon = 0; bool gateSet = false, armed = false, running = false;
    uint32_t lapStart = 0, lastLap = 0, bestLap = 0; int lapNum = 0;
    const double GATE_R = 15.0, LEAVE_R = 30.0;
    int lastWhole = -1;

    while (true) {
        if (check(EscPress)) break;
        gGpsFeed();

        if (check(SelPress)) {
            if (gps.location.isValid()) {
                gateLat = gps.location.lat(); gateLon = gps.location.lng();
                gateSet = true; armed = false; running = false;
                lapNum = 0; lastLap = 0; bestLap = 0;
                tft.fillScreen(bg);
            }
        }
        if (check(NextPress)) { gateSet = false; running = false; armed = false; lapNum = 0; lastLap = bestLap = 0; tft.fillScreen(bg); }

        double d = 99999;
        if (gateSet && gps.location.isValid())
            d = haversine(gateLat, gateLon, gps.location.lat(), gps.location.lng());

        // gate crossing logic
        if (gateSet && gps.location.isValid()) {
            if (!running) {
                if (d < GATE_R && !armed) {         // first cross = start
                    running = true; lapStart = millis(); armed = true; lapNum = 0;
                }
            } else {
                if (d > LEAVE_R) armed = false;      // left the gate, re-arm
                if (d < GATE_R && !armed) {           // crossed again = lap
                    uint32_t now = millis();
                    lastLap = now - lapStart; lapStart = now; lapNum++;
                    if (bestLap == 0 || lastLap < bestLap) bestLap = lastLap;
                    armed = true;
                }
            }
        }

        // draw ~5 fps
        int whole = running ? (int)((millis() - lapStart) / 100) : -1;
        if (whole != lastWhole || check(SelPress)) {
            lastWhole = whole;
            tft.setTextColor(pri, bg); tft.setTextSize(1);
            tft.drawCentreString("GPS LAP TIMER", W / 2, 3, 1);
            tft.drawFastHLine(0, 15, W, sec);
            char buf[20];
            // current lap big
            tft.fillRect(0, 22, W, 40, bg);
            tft.setTextSize(3); tft.setTextColor(running ? TFT_GREEN : sec, bg);
            if (running) fmtTime(millis() - lapStart, buf, sizeof(buf)); else strcpy(buf, gateSet ? "ready" : "no gate");
            tft.drawCentreString(buf, W / 2, 26, 1);
            tft.setTextSize(1);
            tft.fillRect(0, 66, W, H - 78, bg);
            tft.setTextColor(sec, bg);
            char l[28];
            snprintf(l, sizeof(l), "Lap %d", lapNum); tft.drawString(l, 8, 70);
            if (lastLap) { fmtTime(lastLap, buf, sizeof(buf)); snprintf(l, sizeof(l), "Last %s", buf); tft.drawString(l, 8, 84); }
            if (bestLap) { fmtTime(bestLap, buf, sizeof(buf)); tft.setTextColor(TFT_YELLOW, bg); snprintf(l, sizeof(l), "Best %s", buf); tft.drawString(l, 8, 98); tft.setTextColor(sec, bg); }
            double spd = gps.speed.isValid() ? gps.speed.mph() : 0;
            snprintf(l, sizeof(l), "%.0f mph  gate %.0fm  s%lu", spd, d > 9999 ? 0 : d, gps.satellites.value());
            tft.drawString(l, 8, 112);
            tft.drawString("[SEL] set gate [NEXT] reset [ESC]", 4, H - 11);
        }
        vTaskDelay(60 / portTICK_PERIOD_MS);
    }
    gGpsEnd();
}

// ── Reverse Geocode ───────────────────────────────────────────────────────────
static bool httpGetJson(const String &url, JsonDocument &doc) {
    WiFiClientSecure client;
    client.setInsecure();
    client.setHandshakeTimeout(10);
    HTTPClient https;
    https.setConnectTimeout(8000);
    https.setTimeout(12000);
    https.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    if (!https.begin(client, url)) return false;
    int code = https.GET();
    bool ok = false;
    if (code == 200) { https.useHTTP10(true); ok = (deserializeJson(doc, https.getStream()) == DeserializationError::Ok); }
    https.end();
    return ok;
}

void deuceReverseGeo() {
    int W = tft.width(), H = tft.height();
    uint16_t bg = bruceConfig.bgColor, pri = bruceConfig.priColor, sec = bruceConfig.secColor;
    gGpsBegin();
    gWaitFix("[SEL] look up place");

    bool haveResult = false; String lines[6]; int nLines = 0;
    bool redraw = true;

    while (true) {
        if (check(EscPress)) break;
        gGpsFeed();

        if (check(SelPress) && gps.location.isValid()) {
            if (WiFi.status() != WL_CONNECTED && !wifiConnectMenu()) { redraw = true; continue; }
            tft.fillScreen(bg); tft.setTextColor(pri, bg);
            tft.drawCentreString("Looking up...", W / 2, H / 2 - 4, 1);
            char url[160];
            snprintf(url, sizeof(url),
                     "https://api.bigdatacloud.net/data/reverse-geocode-client?latitude=%.5f&longitude=%.5f&localityLanguage=en",
                     gps.location.lat(), gps.location.lng());
            JsonDocument d;
            nLines = 0;
            if (httpGetJson(String(url), d)) {
                const char *locality = d["locality"] | "";
                const char *city = d["city"] | "";
                const char *sub = d["principalSubdivision"] | "";
                const char *country = d["countryName"] | "";
                const char *postcode = d["postcode"] | "";
                String place = strlen(locality) ? locality : city;
                if (place.length()) lines[nLines++] = place;
                if (strlen(city) && String(city) != place) lines[nLines++] = city;
                String region = String(sub);
                if (strlen(postcode)) region += (region.length() ? " " : "") + String(postcode);
                if (region.length()) lines[nLines++] = region;
                if (strlen(country)) lines[nLines++] = country;
                if (nLines == 0) lines[nLines++] = "(no name found)";
                haveResult = true;
            } else {
                lines[0] = "lookup failed"; nLines = 1; haveResult = true;
            }
            redraw = true;
        }

        if (redraw) {
            tft.fillScreen(bg);
            tft.setTextColor(pri, bg); tft.setTextSize(1);
            tft.drawCentreString("REVERSE GEOCODE", W / 2, 3, 1);
            tft.drawFastHLine(0, 15, W, sec);
            tft.setTextColor(sec, bg);
            char c[36];
            if (gps.location.isValid())
                snprintf(c, sizeof(c), "%.5f, %.5f", gps.location.lat(), gps.location.lng());
            else strcpy(c, "no fix");
            tft.drawString(c, 8, 22);
            if (haveResult) {
                int y = 42;
                // first line large, remaining lines normal
                tft.setTextColor(pri, bg); tft.setTextSize(2);
                tft.drawString(lines[0].c_str(), 8, y); y += 24;
                tft.setTextSize(1); tft.setTextColor(pri, bg);
                for (int i = 1; i < nLines && y < H - 20; i++) {
                    tft.drawString(lines[i].c_str(), 8, y);
                    y += 14;
                }
            } else {
                tft.setTextColor(sec, bg);
                tft.drawString("Press SEL to resolve your", 8, 56);
                tft.drawString("current position to a place.", 8, 68);
            }
            tft.setTextSize(1); tft.setTextColor(sec, bg);
            tft.drawString("[SEL] look up  [ESC] exit", 4, H - 11);
            redraw = false;
        }
        vTaskDelay(80 / portTICK_PERIOD_MS);
    }
    gGpsEnd();
}
