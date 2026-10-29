// THE DEUCE v1.1 -- live Crypto + Money trackers: 2x2 swipe grid with sparklines.

#include "deuce_markets.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <globals.h>

#include "core/display.h"
#include "core/wifi/wifi_common.h" // wifiConnectMenu
#include "deuce_seg14.h"           // deuceLiveTouch (swipe)

#define MKT_REFRESH_MS 60000
#define MKT_SPK 32

struct Tile {
    String label;
    String value;
    bool haveChg;
    double chg;      // percent
    float spark[MKT_SPK];
    int nspark;
    bool fixedWindow; // true: +/-20% of cur; false: autoscale
    double cur;       // current value (window center for fixedWindow)
    bool valid;
};

static Tile g_t[8];
static int g_n = 0;

// ---------------------------------------------------------------------------
// HTTPS GET -> JSON, parsed straight from the stream (bounded RAM), optional filter
// ---------------------------------------------------------------------------
static bool httpGetJson(const String &url, JsonDocument &doc, JsonDocument *filter) {
    for (int attempt = 1; attempt <= 2; attempt++) {
        WiFiClientSecure client;
        client.setInsecure();
        HTTPClient https;
        https.setConnectTimeout(15000);
        https.setTimeout(30000);
        if (!https.begin(client, url)) {
            delay(400);
            continue;
        }
        https.useHTTP10(true);
        https.addHeader("User-Agent", "TheDeuce/1.1");
        https.addHeader("Accept", "application/json");
        int code = https.GET();
        if (code == 200) {
            DeserializationError err =
                filter ? deserializeJson(doc, https.getStream(), DeserializationOption::Filter(*filter))
                       : deserializeJson(doc, https.getStream());
            https.end();
            if (!err) return true;
        } else {
            https.end();
        }
        delay(500);
    }
    return false;
}

static bool ensureWifi() {
    if (WiFi.isConnected()) return true;
    return wifiConnectMenu();
}

// ---- formatting ----
static String withCommas(const String &num) {
    int dot = num.indexOf('.');
    String ip = (dot < 0) ? num : num.substring(0, dot);
    String fp = (dot < 0) ? "" : num.substring(dot);
    bool neg = ip.startsWith("-");
    if (neg) ip = ip.substring(1);
    String g;
    int c = 0;
    for (int i = ip.length() - 1; i >= 0; i--) {
        g = ip[i] + g;
        if (++c % 3 == 0 && i > 0) g = "," + g;
    }
    return (neg ? "-" : "") + g + fp;
}
static String fmtPrice(double v) {
    int dec = (v >= 1) ? 2 : (v >= 0.01) ? 4 : 6;
    char b[32];
    dtostrf(v, 0, dec, b);
    return "$" + withCommas(String(b));
}
static String fmtRate(double v) {
    char b[24];
    dtostrf(v, 0, 4, b);
    return String(b);
}

// ---- civil date math (for the forex time-series range) ----
static long civilToDays(int y, unsigned m, unsigned d) {
    y -= m <= 2;
    long era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097L + (long)doe - 719468;
}
static void daysToCivil(long z, int &y, int &m, int &d) {
    z += 719468;
    long era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = (unsigned)(z - era * 146097);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    y = (int)(yoe) + (int)(era)*400;
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned mp = (5 * doy + 2) / 153;
    d = (int)(doy - (153 * mp + 2) / 5 + 1);
    m = (int)(mp + (mp < 10 ? 3 : -9));
    y += (m <= 2);
}

// ===========================================================================
// Crypto fetch
// ===========================================================================
static bool isStable(const String &s) {
    static const char *st[] = {"usdt", "usdc", "dai",  "busd",  "tusd", "fdusd",
                               "usde", "usdd", "pyusd", "gusd", "usds", "usdp"};
    for (auto x : st)
        if (s == x) return true;
    return false;
}

static bool fetchCrypto() {
    g_n = 0;
    String url = "https://api.coingecko.com/api/v3/coins/markets?vs_currency=usd"
                 "&order=market_cap_desc&per_page=12&page=1&sparkline=true"
                 "&price_change_percentage=24h";
    JsonDocument filter;
    JsonObject el = filter.add<JsonObject>();
    el["symbol"] = true;
    el["current_price"] = true;
    el["price_change_percentage_24h"] = true;
    el["sparkline_in_7d"]["price"] = true;

    JsonDocument doc;
    if (!httpGetJson(url, doc, &filter)) return false;

    for (JsonObject c : doc.as<JsonArray>()) {
        if (g_n >= 8) break;
        String sym = String((const char *)(c["symbol"] | ""));
        String low = sym;
        low.toLowerCase();
        if (sym.length() == 0 || isStable(low)) continue;
        Tile &t = g_t[g_n];
        t.label = sym;
        t.label.toUpperCase();
        t.cur = c["current_price"] | 0.0;
        t.value = fmtPrice(t.cur);
        t.chg = c["price_change_percentage_24h"] | 0.0;
        t.haveChg = true;
        t.fixedWindow = true;
        t.valid = true;
        // last 24 hourly points of the 7d sparkline = ~24h
        JsonArray sp = c["sparkline_in_7d"]["price"].as<JsonArray>();
        int total = sp.size();
        int take = total > 24 ? 24 : total;
        int start = total - take;
        t.nspark = 0;
        for (int i = 0; i < take && t.nspark < MKT_SPK; i++)
            t.spark[t.nspark++] = (float)(sp[start + i] | 0.0);
        g_n++;
    }
    return g_n > 0;
}

// ===========================================================================
// Money fetch (USD base)
// ===========================================================================
static const char *MONEY[8] = {"GBP", "EUR", "CAD", "CNY", "JPY", "AUD", "MXN", "INR"};

static bool fetchMoney() {
    g_n = 8;
    for (int i = 0; i < 8; i++) {
        g_t[i] = Tile();
        g_t[i].label = MONEY[i];
        g_t[i].fixedWindow = false;
        g_t[i].nspark = 0;
    }
    // 1) latest values + date
    JsonDocument d1;
    if (!httpGetJson("https://api.frankfurter.app/latest?from=USD&to=GBP,EUR,CAD,CNY,JPY,AUD,MXN,INR",
                     d1, nullptr))
        return false;
    JsonObject rates = d1["rates"].as<JsonObject>();
    for (int i = 0; i < 8; i++) {
        if (!rates[MONEY[i]].isNull()) {
            g_t[i].cur = rates[MONEY[i]] | 0.0;
            g_t[i].value = fmtRate(g_t[i].cur);
            g_t[i].valid = true;
        } else g_t[i].value = "n/a";
    }
    // 2) ~2-week daily series for the sparkline
    String date = String((const char *)(d1["date"] | ""));
    if (date.length() == 10) {
        int y = date.substring(0, 4).toInt();
        int m = date.substring(5, 7).toInt();
        int d = date.substring(8, 10).toInt();
        long end = civilToDays(y, m, d);
        int sy, sm, sd;
        daysToCivil(end - 16, sy, sm, sd);
        char range[48];
        snprintf(range, sizeof(range), "%04d-%02d-%02d..%s", sy, sm, sd, date.c_str());
        String url = "https://api.frankfurter.app/" + String(range) +
                     "?from=USD&to=GBP,EUR,CAD,CNY,JPY,AUD,MXN,INR";
        JsonDocument d2;
        if (httpGetJson(url, d2, nullptr)) {
            JsonObject days = d2["rates"].as<JsonObject>();
            for (JsonPair kv : days) {
                JsonObject day = kv.value().as<JsonObject>();
                for (int i = 0; i < 8; i++) {
                    if (!day[MONEY[i]].isNull() && g_t[i].nspark < MKT_SPK)
                        g_t[i].spark[g_t[i].nspark++] = (float)(day[MONEY[i]] | 0.0);
                }
            }
            // trend % over the shown window
            for (int i = 0; i < 8; i++) {
                if (g_t[i].nspark >= 2 && g_t[i].spark[0] != 0) {
                    g_t[i].chg =
                        (g_t[i].spark[g_t[i].nspark - 1] - g_t[i].spark[0]) / g_t[i].spark[0] * 100.0;
                    g_t[i].haveChg = true;
                }
            }
        }
    }
    return true;
}

// ===========================================================================
// Rendering
// ===========================================================================
static void drawSpark(int gx, int gy, int gw, int gh, const Tile &t, uint16_t col) {
    uint16_t bg = bruceConfig.bgColor, sec = bruceConfig.secColor;
    if (t.nspark < 2) return;
    double lo, hi;
    if (t.fixedWindow) {
        lo = t.cur * 0.8;
        hi = t.cur * 1.2;
    } else {
        lo = hi = t.spark[0];
        for (int i = 1; i < t.nspark; i++) {
            if (t.spark[i] < lo) lo = t.spark[i];
            if (t.spark[i] > hi) hi = t.spark[i];
        }
        double pad = (hi - lo) * 0.15;
        if (pad < 1e-9) pad = (hi == 0 ? 1e-6 : hi * 0.001);
        lo -= pad;
        hi += pad;
    }
    if (hi - lo < 1e-12) hi = lo + 1e-6;

    // mid baseline (current, for fixed window)
    if (t.fixedWindow) {
        double tc = (t.cur - lo) / (hi - lo);
        int my = gy + gh - (int)(tc * gh);
        tft.drawFastHLine(gx, my, gw, sec);
    }
    int px = -1, py = -1;
    for (int i = 0; i < t.nspark; i++) {
        double nv = (t.spark[i] - lo) / (hi - lo);
        if (nv < 0) nv = 0;
        if (nv > 1) nv = 1;
        int x = gx + (t.nspark == 1 ? 0 : gw * i / (t.nspark - 1));
        int y = gy + gh - (int)(nv * gh);
        if (px >= 0) tft.drawLine(px, py, x, y, col);
        px = x;
        py = y;
    }
    // axis labels
    tft.setTextSize(1);
    tft.setTextColor(sec, bg);
    if (t.fixedWindow) {
        tft.drawString("+20%", gx + gw + 2, gy - 2);
        tft.drawString("-20%", gx + gw + 2, gy + gh - 7);
    } else {
        char hb[12], lb[12];
        dtostrf(hi, 0, (hi < 10 ? 3 : 1), hb);
        dtostrf(lo, 0, (lo < 10 ? 3 : 1), lb);
        tft.drawString(hb, gx + gw + 2, gy - 2);
        tft.drawString(lb, gx + gw + 2, gy + gh - 7);
    }
}

static void drawTile(int slot, const Tile &t) {
    int W = tft.width(), H = tft.height();
    int M = 5, top = 20;
    int w = (W - 3 * M) / 2, h = (H - top - 3 * M) / 2;
    int col = slot % 2, row = slot / 2;
    int x = M + col * (w + M), y = top + M + row * (h + M);
    uint16_t pri = bruceConfig.priColor, bg = bruceConfig.bgColor, sec = bruceConfig.secColor;

    tft.fillRoundRect(x, y, w, h, 6, bg);
    tft.drawRoundRect(x, y, w, h, 6, sec);
    if (!t.valid) {
        tft.setTextSize(1);
        tft.setTextColor(sec, bg);
        tft.drawString("n/a", x + 8, y + 8);
        return;
    }

    tft.setTextSize(2);
    tft.setTextColor(pri, bg);
    tft.drawString(t.label, x + 8, y + 6);

    // value (right aligned, size 1 for long prices)
    tft.setTextSize(1);
    tft.setTextColor(pri, bg);
    tft.drawString(t.value, x + w - 6 - tft.textWidth(t.value), y + 8);

    if (t.haveChg) {
        uint16_t cc = t.chg >= 0 ? TFT_GREEN : TFT_RED;
        char ch[16];
        snprintf(ch, sizeof(ch), "%s%.2f%%", t.chg >= 0 ? "+" : "", t.chg);
        tft.setTextColor(cc, bg);
        tft.drawString(ch, x + 8, y + 24);
    }

    uint16_t sc = pri;
    if (t.nspark >= 2) sc = (t.spark[t.nspark - 1] >= t.spark[0]) ? TFT_GREEN : TFT_RED;
    int gx = x + 8, gy = y + 40, gw = w - 16 - 30, gh = h - 40 - 8;
    drawSpark(gx, gy, gw, gh, t, sc);
}

static void drawPage(const String &title, const String &status, int page, int pages) {
    int W = tft.width();
    uint16_t pri = bruceConfig.priColor, bg = bruceConfig.bgColor, sec = bruceConfig.secColor;
    tft.fillScreen(bg);
    tft.setTextSize(1);
    tft.setTextColor(pri, bg);
    tft.drawString(title, 6, 5);
    tft.setTextColor(sec, bg);
    tft.drawString(status, W - 6 - tft.textWidth(status), 5);
    // page dots
    int dotsX = W / 2 - (pages * 10) / 2;
    for (int p = 0; p < pages; p++) {
        if (p == page) tft.fillCircle(dotsX + p * 10, 9, 3, pri);
        else tft.drawCircle(dotsX + p * 10, 9, 3, sec);
    }

    for (int s = 0; s < 4; s++) {
        int idx = page * 4 + s;
        if (idx >= g_n) continue;
        drawTile(s, g_t[idx]);
    }
}

// Shared paged/swipe loop for both trackers.
static void marketLoop(const String &title, bool (*fetchFn)()) {
    if (!ensureWifi()) return;
    drawPage(title, "updating...", 0, 1);
    bool ok = fetchFn();
    int pages = (g_n + 3) / 4;
    if (pages < 1) pages = 1;
    int page = 0;
    drawPage(title, ok ? "live" : "fetch failed", page, pages);
    uint32_t last = millis();

    bool prevLive = true;
    int sx0 = 0, lastX = 0;
    bool committed = false;

    while (true) {
        if (check(EscPress)) break;
        if (check(SelPress)) {
            drawPage(title, "updating...", page, pages);
            ok = fetchFn();
            pages = (g_n + 3) / 4;
            if (pages < 1) pages = 1;
            if (page >= pages) page = pages - 1;
            drawPage(title, ok ? "live" : "fetch failed", page, pages);
            last = millis();
        }

        bool live;
        int x, y;
        deuceLiveTouch(live, x, y);
        if (live && !prevLive) {
            sx0 = x;
            lastX = x;
            committed = false;
        } else if (live) {
            lastX = x;
            if (abs(lastX - sx0) > 16) committed = true;
        } else if (!live && prevLive && committed) {
            int dx = lastX - sx0;
            if (dx <= -40 && page < pages - 1) {
                page++;
                drawPage(title, ok ? "live" : "fetch failed", page, pages);
            } else if (dx >= 40 && page > 0) {
                page--;
                drawPage(title, ok ? "live" : "fetch failed", page, pages);
            }
        }
        prevLive = live;

        if (millis() - last > MKT_REFRESH_MS) {
            drawPage(title, "updating...", page, pages);
            ok = fetchFn();
            pages = (g_n + 3) / 4;
            if (pages < 1) pages = 1;
            if (page >= pages) page = pages - 1;
            drawPage(title, ok ? "live" : "fetch failed", page, pages);
            last = millis();
        }
        vTaskDelay(15 / portTICK_PERIOD_MS);
    }
}

void deuceCrypto() { marketLoop("CRYPTO (USD)  swipe >", fetchCrypto); }
void deuceMoney() { marketLoop("MONEY  1 USD =  swipe >", fetchMoney); }
