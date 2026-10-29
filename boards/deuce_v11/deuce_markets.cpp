// THE DEUCE v1.1 -- live crypto + currency trackers (see .h).

#include "deuce_markets.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <globals.h>

#include "core/display.h"
#include "core/wifi/wifi_common.h" // wifiConnectMenu

#define MKT_REFRESH_MS 60000

// ---------------------------------------------------------------------------
// HTTPS GET (mirrors the TLS pattern used by the Claude chat module)
// ---------------------------------------------------------------------------
static bool httpGet(const String &url, String &out) {
    for (int attempt = 1; attempt <= 2; attempt++) {
        WiFiClientSecure client;
        client.setInsecure(); // no cert pinning, same as Bruce's other HTTPS features
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
        out = https.getString();
        https.end();
        if (code == 200 && out.length() > 0) return true;
        delay(500);
    }
    return false;
}

static bool ensureWifi() {
    if (WiFi.isConnected()) return true;
    return wifiConnectMenu();
}

// group the integer part with commas: 60123.45 -> "60,123.45"
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
    int dec = (v >= 1000) ? 2 : (v >= 1) ? 2 : (v >= 0.01) ? 4 : 6;
    char b[32];
    dtostrf(v, 0, dec, b);
    return "$" + withCommas(String(b));
}

// ===========================================================================
// Crypto
// ===========================================================================
struct Coin {
    String sym;
    double price;
    double chg;
};

static bool isStable(const String &symLower) {
    static const char *stables[] = {"usdt", "usdc", "dai",  "busd",  "tusd",
                                    "fdusd", "usde", "usdd", "pyusd", "gusd", "usds"};
    for (auto s : stables)
        if (symLower == s) return true;
    return false;
}

static bool fetchCrypto(Coin *coins, int &n) {
    n = 0;
    String resp;
    String url = "https://api.coingecko.com/api/v3/coins/markets?vs_currency=usd"
                 "&order=market_cap_desc&per_page=12&page=1&price_change_percentage=24h";
    if (!httpGet(url, resp)) return false;

    // filter: keep only the fields we need, per array element
    JsonDocument filter;
    JsonObject el = filter.add<JsonObject>();
    el["symbol"] = true;
    el["current_price"] = true;
    el["price_change_percentage_24h"] = true;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, resp, DeserializationOption::Filter(filter));
    if (err) return false;

    for (JsonObject c : doc.as<JsonArray>()) {
        String sym = String((const char *)(c["symbol"] | ""));
        String low = sym;
        low.toLowerCase();
        if (sym.length() == 0 || isStable(low)) continue;
        coins[n].sym = sym;
        coins[n].sym.toUpperCase();
        coins[n].price = c["current_price"] | 0.0;
        coins[n].chg = c["price_change_percentage_24h"] | 0.0;
        if (++n >= 4) break;
    }
    return n > 0;
}

static void drawCrypto(Coin *coins, int n, const String &status) {
    uint16_t pri = bruceConfig.priColor, bg = bruceConfig.bgColor, sec = bruceConfig.secColor;
    int W = tft.width();
    tft.fillScreen(bg);
    tft.setTextSize(1);
    tft.setTextColor(pri, bg);
    tft.drawString("CRYPTO  (USD)", 6, 6);
    tft.setTextColor(sec, bg);
    tft.drawString(status, W - 6 - tft.textWidth(status), 6);
    tft.drawFastHLine(0, 22, W, sec);

    int y = 34;
    for (int i = 0; i < n; i++) {
        tft.setTextSize(2);
        tft.setTextColor(pri, bg);
        tft.drawString(coins[i].sym, 8, y);
        String ps = fmtPrice(coins[i].price);
        tft.drawString(ps, W - 8 - tft.textWidth(ps), y);
        // 24h change
        tft.setTextSize(1);
        uint16_t cc = coins[i].chg >= 0 ? TFT_GREEN : TFT_RED;
        char ch[16];
        snprintf(ch, sizeof(ch), "%s%.2f%%", coins[i].chg >= 0 ? "+" : "", coins[i].chg);
        tft.setTextColor(cc, bg);
        tft.drawString(ch, 8, y + 18);
        y += 44;
    }
    tft.setTextSize(1);
    tft.setTextColor(sec, bg);
    tft.drawString("[SEL] refresh   [ESC] exit", 6, tft.height() - 12);
}

void deuceCrypto() {
    if (!ensureWifi()) return;
    Coin coins[4];
    int n = 0;
    drawCrypto(coins, 0, "updating...");
    bool ok = fetchCrypto(coins, n);
    drawCrypto(coins, n, ok ? "live" : "fetch failed");
    uint32_t last = millis();

    while (true) {
        if (check(EscPress)) break;
        bool refresh = check(SelPress);
        if (millis() - last > MKT_REFRESH_MS) refresh = true;
        if (refresh) {
            drawCrypto(coins, n, "updating...");
            ok = fetchCrypto(coins, n);
            drawCrypto(coins, n, ok ? "live" : "fetch failed");
            last = millis();
        }
        vTaskDelay(50 / portTICK_PERIOD_MS);
    }
}

// ===========================================================================
// Money (currency)
// ===========================================================================
struct Rate {
    const char *code;
    double value;
    bool have;
};

static bool fetchMoney(Rate *r, int n) {
    String resp;
    if (!httpGet("https://api.frankfurter.app/latest?from=USD&to=GBP,CAD,CNY", resp)) return false;
    JsonDocument doc;
    if (deserializeJson(doc, resp)) return false;
    JsonObject rates = doc["rates"].as<JsonObject>();
    bool any = false;
    for (int i = 0; i < n; i++) {
        if (!rates[r[i].code].isNull()) {
            r[i].value = rates[r[i].code] | 0.0;
            r[i].have = true;
            any = true;
        } else r[i].have = false;
    }
    return any;
}

static void drawMoney(Rate *r, int n, const String &status) {
    uint16_t pri = bruceConfig.priColor, bg = bruceConfig.bgColor, sec = bruceConfig.secColor;
    int W = tft.width();
    tft.fillScreen(bg);
    tft.setTextSize(1);
    tft.setTextColor(pri, bg);
    tft.drawString("MONEY  (1 USD =)", 6, 6);
    tft.setTextColor(sec, bg);
    tft.drawString(status, W - 6 - tft.textWidth(status), 6);
    tft.drawFastHLine(0, 22, W, sec);

    int y = 40;
    for (int i = 0; i < n; i++) {
        tft.setTextSize(2);
        tft.setTextColor(pri, bg);
        tft.drawString(r[i].code, 8, y);
        String v;
        if (r[i].have) {
            char b[24];
            dtostrf(r[i].value, 0, 4, b);
            v = String(b);
        } else v = "n/a";
        tft.drawString(v, W - 8 - tft.textWidth(v), y);
        y += 42;
    }
    tft.setTextSize(1);
    tft.setTextColor(sec, bg);
    tft.drawString("[SEL] refresh   [ESC] exit", 6, tft.height() - 12);
}

void deuceMoney() {
    if (!ensureWifi()) return;
    Rate r[3] = {{"GBP", 0, false}, {"CAD", 0, false}, {"CNY", 0, false}};
    drawMoney(r, 3, "updating...");
    bool ok = fetchMoney(r, 3);
    drawMoney(r, 3, ok ? "live" : "fetch failed");
    uint32_t last = millis();

    while (true) {
        if (check(EscPress)) break;
        bool refresh = check(SelPress);
        if (millis() - last > MKT_REFRESH_MS) refresh = true;
        if (refresh) {
            drawMoney(r, 3, "updating...");
            ok = fetchMoney(r, 3);
            drawMoney(r, 3, ok ? "live" : "fetch failed");
            last = millis();
        }
        vTaskDelay(50 / portTICK_PERIOD_MS);
    }
}
