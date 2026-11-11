// THE DEUCE v1.1 -- online network recon tools. See .h.

#include "deuce_nettools2.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <globals.h>
#include <mbedtls/x509_crt.h>

#include "core/display.h"
#include "core/mykeyboard.h"
#include "core/wifi/wifi_common.h"

static bool needWifi() {
    if (WiFi.status() == WL_CONNECTED) return true;
    return wifiConnectMenu();
}

// generic scrollable text viewer (vector of lines)
static void pager(const char *title, const std::vector<String> &lines) {
    int W = tft.width(), H = tft.height();
    uint16_t bg = bruceConfig.bgColor, pri = bruceConfig.priColor, sec = bruceConfig.secColor;
    int scroll = 0, rows = (H - 34) / 11;
    while (true) {
        if (check(EscPress) || check(SelPress)) break;
        if (check(DownPress) && scroll < (int)lines.size() - rows) scroll++;
        if (check(UpPress) && scroll > 0) scroll--;
        tft.fillScreen(bg);
        tft.setTextColor(pri, bg); tft.drawString(title, 6, 4);
        tft.drawFastHLine(0, 16, W, sec);
        int y = 20;
        for (int i = scroll; i < (int)lines.size() && y < H - 12; i++) {
            tft.setTextColor(lines[i].startsWith("!") ? TFT_ORANGE : pri, bg);
            tft.drawString(lines[i].startsWith("!") ? lines[i].substring(1) : lines[i], 6, y);
            y += 11;
        }
        tft.setTextColor(sec, bg);
        tft.drawString((int)lines.size() > rows ? "[UP/DN] scroll [SEL/ESC] back" : "[SEL/ESC] back", 4, H - 11);
        vTaskDelay(40 / portTICK_PERIOD_MS);
    }
}

static void loading(const char *m) {
    int W = tft.width(), H = tft.height();
    tft.fillScreen(bruceConfig.bgColor);
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
    tft.drawCentreString(m, W / 2, H / 2 - 4, 1);
}

// ── TLS / certificate inspector ───────────────────────────────────────────────
static String fmtX509Time(const mbedtls_x509_time &t) {
    char b[24];
    snprintf(b, sizeof(b), "%04d-%02d-%02d %02d:%02d", t.year, t.mon, t.day, t.hour, t.min);
    return String(b);
}

void deuceTlsInspect() {
    String host = keyboard("example.com", 64, "TLS host (no https://)");
    if (host.length() == 0) return;
    int port = 443;
    int colon = host.indexOf(':');
    if (colon > 0) { port = host.substring(colon + 1).toInt(); host = host.substring(0, colon); }
    if (!needWifi()) return;
    loading("Connecting (TLS)...");

    WiFiClientSecure client;
    client.setInsecure();
    client.setHandshakeTimeout(12);
    std::vector<String> out;
    if (!client.connect(host.c_str(), port)) {
        out.push_back("!TLS connect failed");
        out.push_back("host: " + host + ":" + String(port));
        pager("TLS Inspector", out);
        return;
    }
    const mbedtls_x509_crt *crt = client.getPeerCertificate();
    if (!crt) {
        out.push_back("!no peer certificate");
    } else {
        char buf[256];
        out.push_back(host + ":" + String(port));
        if (mbedtls_x509_dn_gets(buf, sizeof(buf), &crt->subject) > 0) {
            out.push_back("Subject:"); out.push_back(" " + String(buf));
        }
        if (mbedtls_x509_dn_gets(buf, sizeof(buf), &crt->issuer) > 0) {
            out.push_back("Issuer:"); out.push_back(" " + String(buf));
        }
        out.push_back("Valid from: " + fmtX509Time(crt->valid_from));
        out.push_back("Valid to:   " + fmtX509Time(crt->valid_to));
        // expiry check vs system clock (if set)
        time_t nowt = time(nullptr);
        if (nowt > 1600000000) {
            struct tm *g = gmtime(&nowt);
            int cy = g->tm_year + 1900, cm = g->tm_mon + 1, cd = g->tm_mday;
            bool expired = (cy > crt->valid_to.year) ||
                           (cy == crt->valid_to.year && cm > crt->valid_to.mon) ||
                           (cy == crt->valid_to.year && cm == crt->valid_to.mon && cd > crt->valid_to.day);
            out.push_back(expired ? "!STATUS: EXPIRED" : "STATUS: valid (date ok)");
        }
        uint8_t fp[32];
        if (client.getFingerprintSHA256(fp)) {
            String f = "SHA256 ";
            for (int i = 0; i < 8; i++) { char h[4]; snprintf(h, sizeof(h), "%02X", fp[i]); f += h; }
            f += "...";
            out.push_back(f);
        }
    }
    client.stop();
    pager("TLS Inspector", out);
}

// ── HTTP banner / security headers ────────────────────────────────────────────
void deuceHttpBanner() {
    String host = keyboard("example.com", 64, "HTTP host");
    if (host.length() == 0) return;
    int port = 80;
    int colon = host.indexOf(':');
    if (colon > 0) { port = host.substring(colon + 1).toInt(); host = host.substring(0, colon); }
    if (!needWifi()) return;
    loading("Connecting...");

    WiFiClient client;
    std::vector<String> out;
    if (!client.connect(host.c_str(), port, 8000)) {
        out.push_back("!connect failed");
        pager("HTTP Banner", out);
        return;
    }
    client.printf("HEAD / HTTP/1.1\r\nHost: %s\r\nUser-Agent: Deuce\r\nConnection: close\r\n\r\n", host.c_str());
    uint32_t t0 = millis();
    bool sawSec = false;
    while ((client.connected() || client.available()) && millis() - t0 < 8000) {
        if (client.available()) {
            String line = client.readStringUntil('\n');
            line.trim();
            if (line.length() == 0) break; // end of headers
            // highlight key headers
            String low = line; low.toLowerCase();
            bool key = low.startsWith("http/") || low.startsWith("server:") || low.startsWith("x-powered-by:") ||
                       low.startsWith("strict-transport-security:") || low.startsWith("content-security-policy:") ||
                       low.startsWith("x-frame-options:") || low.startsWith("location:");
            if (low.startsWith("strict-transport-security:") || low.startsWith("content-security-policy:")) sawSec = true;
            if (out.size() < 40) out.push_back((key ? "" : "") + line);
        } else vTaskDelay(10 / portTICK_PERIOD_MS);
    }
    client.stop();
    if (out.empty()) out.push_back("!no response");
    out.push_back(sawSec ? "sec-headers: present" : "!sec-headers: HSTS/CSP missing");
    pager("HTTP Banner", out);
}

// ── DNS over HTTPS ────────────────────────────────────────────────────────────
static bool doh(const String &name, const String &type, JsonDocument &doc) {
    WiFiClientSecure client; client.setInsecure(); client.setHandshakeTimeout(10);
    HTTPClient https;
    https.setConnectTimeout(8000); https.setTimeout(12000);
    String url = "https://dns.google/resolve?name=" + name + "&type=" + type;
    if (!https.begin(client, url)) return false;
    int code = https.GET();
    bool ok = false;
    if (code == 200) { https.useHTTP10(true); ok = (deserializeJson(doc, https.getStream()) == DeserializationError::Ok); }
    https.end();
    return ok;
}

void deuceDnsTools() {
    std::vector<Option> types = {
        {"A (IPv4)",   []() {}},
        {"AAAA (IPv6)", []() {}},
        {"MX (mail)",  []() {}},
        {"TXT",        []() {}},
        {"NS",         []() {}},
        {"PTR (reverse IP)", []() {}},
    };
    int t = loopOptions(types, MENU_TYPE_SUBMENU, "Record type");
    if (t < 0) return;
    const char *TYPES[] = {"A", "AAAA", "MX", "TXT", "NS", "PTR"};

    String q;
    if (t == 5) {
        String ip = keyboard("", 40, "IP to reverse");
        if (ip.length() == 0) return;
        // build in-addr.arpa (IPv4 only)
        int p[4]; if (sscanf(ip.c_str(), "%d.%d.%d.%d", &p[0], &p[1], &p[2], &p[3]) != 4) { displayError("Need IPv4", true); return; }
        char arpa[40]; snprintf(arpa, sizeof(arpa), "%d.%d.%d.%d.in-addr.arpa", p[3], p[2], p[1], p[0]);
        q = arpa;
    } else {
        q = keyboard("", 64, "Domain name");
        if (q.length() == 0) return;
    }
    if (!needWifi()) return;
    loading("Querying DoH...");

    JsonDocument doc;
    std::vector<String> out;
    out.push_back(String(TYPES[t]) + "  " + q);
    if (!doh(q, TYPES[t], doc)) {
        out.push_back("!query failed");
    } else {
        int status = doc["Status"] | -1;
        if (status != 0) out.push_back("!DNS status " + String(status) + (status == 3 ? " (NXDOMAIN)" : ""));
        JsonArray ans = doc["Answer"].as<JsonArray>();
        if (ans.isNull() || ans.size() == 0) out.push_back("(no records)");
        else for (JsonObject a : ans) {
            String data = a["data"] | "";
            out.push_back(" " + data);
        }
    }
    pager("DNS Tools", out);
}
