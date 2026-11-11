// THE DEUCE v1.1 -- clock synchronisation. See .h.

#include "deuce_timesync.h"

#include <Arduino.h>
#include <ESP32Time.h>
#include <TinyGPS++.h>
#include <WiFi.h>
#include <globals.h>
#include <time.h>

#include "core/bus_HAL.h"
#include "core/display.h"
#include "core/wifi/wifi_common.h"

// ── DS3231 (battery-backed RTC) over the shared I2C bus ───────────────────────
#define DS3231_ADDR 0x68
static uint8_t bcd2dec(uint8_t v) { return (v >> 4) * 10 + (v & 0x0F); }
static uint8_t dec2bcd(uint8_t v) { return ((v / 10) << 4) | (v % 10); }

// write a UTC calendar time to the DS3231
static bool ds3231Write(int yr, int mon, int day, int hr, int mn, int sc, int dow) {
    TwoWire *w = acquireI2CBus();
    bool ok = false;
    if (w) {
        w->beginTransmission(DS3231_ADDR);
        w->write(0x00);
        w->write(dec2bcd(sc));
        w->write(dec2bcd(mn));
        w->write(dec2bcd(hr));           // bit6=0 -> 24h mode
        w->write(dec2bcd(dow < 1 ? 1 : dow));
        w->write(dec2bcd(day));
        w->write(dec2bcd(mon));
        w->write(dec2bcd(yr % 100));
        ok = (w->endTransmission() == 0);
    }
    releaseI2CBus();
    return ok;
}

// read UTC calendar time from the DS3231; returns false if not present
static bool ds3231Read(int &yr, int &mon, int &day, int &hr, int &mn, int &sc) {
    TwoWire *w = acquireI2CBus();
    bool ok = false;
    if (w) {
        w->beginTransmission(DS3231_ADDR);
        w->write(0x00);
        if (w->endTransmission() == 0) {
            uint8_t n = w->requestFrom((uint8_t)DS3231_ADDR, (uint8_t)7, (uint8_t) true);
            if (n == 7) {
                sc = bcd2dec(w->read() & 0x7F);
                mn = bcd2dec(w->read() & 0x7F);
                hr = bcd2dec(w->read() & 0x3F);
                w->read();                       // day-of-week
                day = bcd2dec(w->read() & 0x3F);
                mon = bcd2dec(w->read() & 0x1F);
                yr = 2000 + bcd2dec(w->read());
                ok = true;
            }
        }
    }
    releaseI2CBus();
    return ok;
}

static int dowFromDate(int y, int m, int d) { // Sakamoto, 1=Sun..7=Sat
    static int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (m < 3) y -= 1;
    return ((y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7) + 1;
}

// apply a UTC time to the ESP32 system clock + DS3231, then show it
static void applyTime(int yr, int mon, int day, int hr, int mn, int sc, const char *src, bool haveDs) {
    rtc.setTime(sc, mn, hr, day, mon, yr); // offset 0 -> this is UTC epoch
    bool ds = ds3231Write(yr, mon, day, hr, mn, sc, dowFromDate(yr, mon, day));

    int W = tft.width(), H = tft.height();
    tft.fillScreen(bruceConfig.bgColor);
    tft.setTextColor(TFT_GREEN, bruceConfig.bgColor);
    tft.drawCentreString("CLOCK SET", W / 2, 24, 1);
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor); tft.setTextSize(2);
    char d[24]; snprintf(d, sizeof(d), "%04d-%02d-%02d", yr, mon, day);
    tft.drawCentreString(d, W / 2, 52, 1);
    snprintf(d, sizeof(d), "%02d:%02d:%02d UTC", hr, mn, sc);
    tft.drawCentreString(d, W / 2, 78, 1);
    tft.setTextSize(1); tft.setTextColor(bruceConfig.secColor, bruceConfig.bgColor);
    char s[40]; snprintf(s, sizeof(s), "source: %s", src); tft.drawCentreString(s, W / 2, 104, 1);
    tft.drawCentreString(ds ? "DS3231 updated (persists)" : "DS3231 not found (session only)",
                         W / 2, 118, 1);
    (void)haveDs;
    tft.drawCentreString("press any key", W / 2, H - 16, 1);
    while (!check(EscPress) && !check(SelPress) && !check(NextPress)) vTaskDelay(20 / portTICK_PERIOD_MS);
}

// ── GPS reader (self-contained; mirrors the GPS tracker init) ─────────────────
static HardwareSerial GPSser(2);
static TinyGPSPlus gps;
static bool g_rxReleased = false;
static void tsReleasePins() {
    g_rxReleased = false;
    if (bruceConfigPins.CC1101_bus.checkConflict(bruceConfigPins.gps_bus.rx) ||
        bruceConfigPins.NRF24_bus.checkConflict(bruceConfigPins.gps_bus.rx) ||
        bruceConfigPins.SDCARD_bus.checkConflict(bruceConfigPins.gps_bus.rx)) {
        pinMode(bruceConfigPins.gps_bus.rx, INPUT);
        g_rxReleased = true;
    }
}
static void tsRestorePins() {
    if (!g_rxReleased) return;
    pinMode(bruceConfigPins.gps_bus.rx, OUTPUT);
    if (bruceConfigPins.gps_bus.rx == bruceConfigPins.CC1101_bus.cs ||
        bruceConfigPins.gps_bus.rx == bruceConfigPins.NRF24_bus.cs ||
        bruceConfigPins.gps_bus.rx == bruceConfigPins.SDCARD_bus.cs)
        digitalWrite(bruceConfigPins.gps_bus.rx, HIGH);
    else digitalWrite(bruceConfigPins.gps_bus.rx, LOW);
    g_rxReleased = false;
}
static void tsGpsBegin() {
#ifdef IO_EXP_GPS
    ioExpander.turnPinOnOff(IO_EXP_GPS, HIGH);
#endif
    tsReleasePins();
    pinMode(bruceConfigPins.gps_bus.rx, INPUT);
    GPSser.begin(bruceConfigPins.gpsBaudrate, SERIAL_8N1, bruceConfigPins.gps_bus.rx, bruceConfigPins.gps_bus.tx);
}
static void tsGpsEnd() {
    GPSser.end();
    tsRestorePins();
#ifdef IO_EXP_GPS
    ioExpander.turnPinOnOff(IO_EXP_GPS, LOW);
#endif
}

void deuceGpsTimeSync() {
    int W = tft.width(), H = tft.height();
    uint16_t bg = bruceConfig.bgColor, pri = bruceConfig.priColor, sec = bruceConfig.secColor;
    tsGpsBegin();

    while (true) {
        if (check(EscPress)) break;
        while (GPSser.available() > 0) gps.encode(GPSser.read());

        bool ready = gps.time.isValid() && gps.date.isValid() && gps.date.year() >= 2024 &&
                     gps.satellites.value() >= 3;

        if (check(SelPress) && ready) {
            applyTime(gps.date.year(), gps.date.month(), gps.date.day(),
                      gps.time.hour(), gps.time.minute(), gps.time.second(), "GPS satellites", false);
            break;
        }

        tft.fillScreen(bg);
        tft.setTextColor(pri, bg); tft.drawCentreString("GPS TIME SYNC", W / 2, 6, 1);
        tft.drawFastHLine(0, 18, W, sec);
        tft.setTextColor(sec, bg);
        char l[40];
        snprintf(l, sizeof(l), "sats: %lu   fix: %s", gps.satellites.value(), ready ? "YES" : "no");
        tft.drawCentreString(l, W / 2, 40, 1);
        if (gps.time.isValid() && gps.date.isValid()) {
            snprintf(l, sizeof(l), "%04d-%02d-%02d %02d:%02d:%02d UTC", gps.date.year(), gps.date.month(),
                     gps.date.day(), gps.time.hour(), gps.time.minute(), gps.time.second());
            tft.setTextColor(pri, bg); tft.drawCentreString(l, W / 2, 62, 1);
        }
        tft.setTextColor(sec, bg);
        tft.drawCentreString(ready ? "[SEL] set clock" : "waiting for satellites...", W / 2, 90, 1);
        tft.drawCentreString("[ESC] exit", W / 2, H - 16, 1);
        vTaskDelay(200 / portTICK_PERIOD_MS);
    }
    tsGpsEnd();
}

void deuceNtpTimeSync() {
    int W = tft.width(), H = tft.height();
    uint16_t bg = bruceConfig.bgColor, pri = bruceConfig.priColor, sec = bruceConfig.secColor;

    tft.fillScreen(bg);
    tft.setTextColor(pri, bg); tft.drawCentreString("NTP TIME SYNC", W / 2, 6, 1);
    tft.drawFastHLine(0, 18, W, sec);
    tft.setTextColor(sec, bg);

    if (WiFi.status() != WL_CONNECTED && !wifiConnectMenu()) return;

    tft.fillScreen(bg);
    tft.setTextColor(pri, bg); tft.drawCentreString("NTP TIME SYNC", W / 2, 6, 1);
    tft.drawFastHLine(0, 18, W, sec);
    tft.setTextColor(sec, bg);
    tft.drawCentreString("Contacting time server...", W / 2, 50, 1);

    configTime(0, 0, "pool.ntp.org", "time.nist.gov", "time.google.com"); // UTC
    struct tm t;
    bool ok = false;
    for (int i = 0; i < 40; i++) {               // up to ~8s
        if (getLocalTime(&t, 200) && t.tm_year + 1900 >= 2024) { ok = true; break; }
        if (check(EscPress)) return;
        vTaskDelay(200 / portTICK_PERIOD_MS);
    }

    if (!ok) {
        // offer DS3231 fallback if the module has a good time
        int yr, mon, day, hr, mn, sc;
        if (ds3231Read(yr, mon, day, hr, mn, sc) && yr >= 2024) {
            applyTime(yr, mon, day, hr, mn, sc, "DS3231 (NTP failed)", true);
        } else {
            displayError("NTP failed, no DS3231", true);
        }
        return;
    }
    applyTime(t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec, "NTP (pool.ntp.org)", false);
}
