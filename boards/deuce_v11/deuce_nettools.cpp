// THE DEUCE v1.1 -- online WiFi tools. See .h.

#include "deuce_nettools.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <globals.h>

#include "core/display.h"
#include "core/wifi/wifi_common.h"

// ── HTTPS JSON GET ────────────────────────────────────────────────────────────
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
    if (code == 200) {
        https.useHTTP10(true);
        ok = (deserializeJson(doc, https.getStream()) == DeserializationError::Ok);
    }
    https.end();
    return ok;
}

static void netLoading(const char *msg) {
    int W = tft.width(), H = tft.height();
    tft.fillScreen(bruceConfig.bgColor);
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
    tft.drawCentreString(msg, W / 2, H / 2 - 4, 1);
}
static void waitKey() {
    while (true) { if (check(EscPress) || check(SelPress) || check(NextPress)) break; vTaskDelay(20 / portTICK_PERIOD_MS); }
}
static bool needWifi() {
    if (WiFi.status() == WL_CONNECTED) return true;
    return wifiConnectMenu();
}

// ── Weather Forecast ──────────────────────────────────────────────────────────
static const char *wmo(int c) {
    if (c == 0) return "Clear";
    if (c <= 2) return "Partly cloudy";
    if (c == 3) return "Overcast";
    if (c <= 48) return "Fog";
    if (c <= 57) return "Drizzle";
    if (c <= 67) return "Rain";
    if (c <= 77) return "Snow";
    if (c <= 82) return "Showers";
    if (c <= 86) return "Snow showers";
    if (c <= 99) return "Thunderstorm";
    return "?";
}

void deuceWeatherForecast() {
    if (!needWifi()) { displayError("WiFi needed", true); return; }
    int W = tft.width(), H = tft.height();
    uint16_t bg = bruceConfig.bgColor, pri = bruceConfig.priColor, sec = bruceConfig.secColor;

    netLoading("Locating (IP)...");
    JsonDocument g;
    if (!httpGetJson("https://ipwho.is/", g) || !(g["success"] | false)) { displayError("geoloc failed", true); return; }
    double lat = g["latitude"] | 0.0, lon = g["longitude"] | 0.0;
    String city = String((const char *)(g["city"] | "?"));

    netLoading("Fetching forecast...");
    String url = "https://api.open-meteo.com/v1/forecast?latitude=" + String(lat, 4) +
                 "&longitude=" + String(lon, 4) +
                 "&current=temperature_2m,relative_humidity_2m,weather_code,wind_speed_10m,apparent_temperature"
                 "&daily=weather_code,temperature_2m_max,temperature_2m_min"
                 "&temperature_unit=fahrenheit&wind_speed_unit=mph&forecast_days=3&timezone=auto";
    JsonDocument w;
    if (!httpGetJson(url, w)) { displayError("forecast failed", true); return; }

    float t = w["current"]["temperature_2m"] | 0.0f;
    float feels = w["current"]["apparent_temperature"] | t;
    int rh = w["current"]["relative_humidity_2m"] | 0;
    int code = w["current"]["weather_code"] | 0;
    float wind = w["current"]["wind_speed_10m"] | 0.0f;

    tft.fillScreen(bg);
    tft.setTextColor(pri, bg); tft.setTextSize(1);
    tft.drawCentreString("WEATHER FORECAST", W / 2, 4, 1);
    tft.drawFastHLine(0, 16, W, sec);
    tft.setTextColor(sec, bg);
    tft.drawString(city.c_str(), 6, 20);
    // current
    tft.setTextColor(TFT_ORANGE, bg); tft.setTextSize(3);
    char s[12]; snprintf(s, sizeof(s), "%.0f", t);
    tft.drawString(s, 6, 32); tft.setTextSize(1);
    tft.drawString("F", 6 + tft.textWidth(s) * 3 + 2, 32);
    tft.setTextColor(pri, bg);
    tft.drawString(wmo(code), 6, 62);
    tft.setTextColor(sec, bg);
    char l[48];
    snprintf(l, sizeof(l), "feels %.0fF  hum %d%%  wind %.0f mph", feels, rh, wind);
    tft.drawString(l, 6, 74);
    // 3-day
    JsonArray mx = w["daily"]["temperature_2m_max"].as<JsonArray>();
    JsonArray mn = w["daily"]["temperature_2m_min"].as<JsonArray>();
    JsonArray dc = w["daily"]["weather_code"].as<JsonArray>();
    const char *days[3] = { "Today", "Tomorrow", "Day 3" };
    int y = 92;
    for (int i = 0; i < 3 && i < (int)mx.size(); i++) {
        tft.setTextColor(sec, bg);
        snprintf(l, sizeof(l), "%-9s %2.0f/%2.0fF  %s", days[i], (float)mx[i], (float)mn[i], wmo((int)dc[i]));
        tft.drawString(l, 6, y); y += 12;
    }
    tft.setTextColor(sec, bg);
    tft.drawString("[any key] exit", 4, H - 11);
    waitKey();
}

// ── Speed Test ────────────────────────────────────────────────────────────────
void deuceSpeedTest() {
    if (!needWifi()) { displayError("WiFi needed", true); return; }
    int W = tft.width(), H = tft.height();
    uint16_t bg = bruceConfig.bgColor, pri = bruceConfig.priColor, sec = bruceConfig.secColor;

    // latency: time a tiny request
    netLoading("Pinging...");
    long pingMs = -1;
    {
        WiFiClientSecure c; c.setInsecure(); c.setHandshakeTimeout(8);
        HTTPClient h; h.setConnectTimeout(6000); h.setTimeout(8000);
        if (h.begin(c, "https://speed.cloudflare.com/__down?bytes=0")) {
            uint32_t t0 = millis();
            int code = h.GET();
            if (code > 0) pingMs = millis() - t0;
            h.end();
        }
    }

    // download: fetch ~2MB, measure throughput
    netLoading("Download test...");
    float mbps = -1; size_t total = 0; uint32_t dt = 0;
    {
        WiFiClientSecure c; c.setInsecure(); c.setHandshakeTimeout(8);
        HTTPClient h; h.setConnectTimeout(6000); h.setTimeout(20000);
        if (h.begin(c, "https://speed.cloudflare.com/__down?bytes=3000000")) {
            uint32_t t0 = millis();
            int code = h.GET();
            if (code == 200) {
                WiFiClient *st = h.getStreamPtr();
                uint8_t buf[1460];
                while (h.connected() && total < 3000000) {
                    size_t a = st->available();
                    if (a) { int r = st->readBytes(buf, a > sizeof(buf) ? sizeof(buf) : a); total += r; }
                    else vTaskDelay(1 / portTICK_PERIOD_MS);
                    if (millis() - t0 > 18000) break;
                }
                dt = millis() - t0;
                if (dt > 0) mbps = (total * 8.0f) / (dt / 1000.0f) / 1e6f;
            }
            h.end();
        }
    }

    tft.fillScreen(bg);
    tft.setTextColor(pri, bg); tft.setTextSize(1);
    tft.drawCentreString("SPEED TEST", W / 2, 4, 1);
    tft.drawFastHLine(0, 16, W, sec);
    // ping
    tft.setTextColor(sec, bg); tft.drawString("Latency", 10, 30);
    tft.setTextColor(pri, bg); tft.setTextSize(2);
    char s[16]; snprintf(s, sizeof(s), pingMs >= 0 ? "%ld ms" : "n/a", pingMs);
    tft.drawString(s, 10, 42);
    // download
    tft.setTextSize(1); tft.setTextColor(sec, bg); tft.drawString("Download", 10, 72);
    tft.setTextColor(TFT_GREEN, bg); tft.setTextSize(3);
    if (mbps >= 0) snprintf(s, sizeof(s), "%.1f", mbps); else snprintf(s, sizeof(s), "n/a");
    tft.drawString(s, 10, 84); tft.setTextSize(1);
    if (mbps >= 0) tft.drawString("Mbps", 10 + tft.textWidth(s) * 3 + 2, 84);
    tft.setTextColor(sec, bg);
    char l[40]; snprintf(l, sizeof(l), "%u KB in %u ms", (unsigned)(total / 1024), (unsigned)dt);
    tft.drawString(l, 10, 116);
    tft.drawString("[SEL] retest   [ESC]", 4, H - 11);
    // simple wait: ESC exits, SEL reruns
    while (true) {
        if (check(EscPress)) return;
        if (check(SelPress)) { deuceSpeedTest(); return; }
        vTaskDelay(20 / portTICK_PERIOD_MS);
    }
}
