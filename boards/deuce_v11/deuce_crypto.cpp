// THE DEUCE v1.1 -- crypto / encoding toolkit. See .h.

#include "deuce_crypto.h"

#include <Arduino.h>
#include <globals.h>
#include <mbedtls/base64.h>
#include <mbedtls/md.h>

#include "core/display.h"
#include "core/mykeyboard.h"

static String toHex(const uint8_t *d, size_t n) {
    static const char *h = "0123456789abcdef";
    String s; s.reserve(n * 2);
    for (size_t i = 0; i < n; i++) { s += h[d[i] >> 4]; s += h[d[i] & 0x0F]; }
    return s;
}

static String hashHex(mbedtls_md_type_t type, const String &in) {
    const mbedtls_md_info_t *info = mbedtls_md_info_from_type(type);
    uint8_t out[32];
    mbedtls_md(info, (const uint8_t *)in.c_str(), in.length(), out);
    return toHex(out, mbedtls_md_get_size(info));
}

static String b64enc(const String &in) {
    size_t olen = 0;
    size_t cap = ((in.length() + 2) / 3) * 4 + 4;
    std::vector<uint8_t> buf(cap);
    if (mbedtls_base64_encode(buf.data(), cap, &olen, (const uint8_t *)in.c_str(), in.length()) != 0)
        return "(encode error)";
    return String((char *)buf.data());
}
static String b64dec(const String &in) {
    size_t olen = 0;
    std::vector<uint8_t> buf(in.length() + 1);
    if (mbedtls_base64_decode(buf.data(), buf.size(), &olen, (const uint8_t *)in.c_str(), in.length()) != 0)
        return "(invalid base64)";
    buf[olen] = 0;
    return String((char *)buf.data());
}

static String hexenc(const String &in) { return toHex((const uint8_t *)in.c_str(), in.length()); }
static String hexdec(const String &in) {
    String s; String t = in; t.replace(" ", "");
    if (t.length() % 2) return "(odd hex length)";
    for (size_t i = 0; i + 1 < t.length(); i += 2) {
        auto nib = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };
        int hi = nib(t[i]), lo = nib(t[i + 1]);
        if (hi < 0 || lo < 0) return "(invalid hex)";
        s += (char)((hi << 4) | lo);
    }
    return s;
}

static String rot13(const String &in) {
    String s = in;
    for (size_t i = 0; i < s.length(); i++) {
        char c = s[i];
        if (c >= 'a' && c <= 'z') s[i] = 'a' + (c - 'a' + 13) % 26;
        else if (c >= 'A' && c <= 'Z') s[i] = 'A' + (c - 'A' + 13) % 26;
    }
    return s;
}
static String urlenc(const String &in) {
    static const char *h = "0123456789ABCDEF";
    String s;
    for (size_t i = 0; i < in.length(); i++) {
        char c = in[i];
        if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~') s += c;
        else { s += '%'; s += h[(c >> 4) & 0xF]; s += h[c & 0xF]; }
    }
    return s;
}
static String urldec(const String &in) {
    String s;
    for (size_t i = 0; i < in.length(); i++) {
        if (in[i] == '%' && i + 2 < in.length()) {
            auto nib = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return 0;
            };
            s += (char)((nib(in[i + 1]) << 4) | nib(in[i + 2])); i += 2;
        } else if (in[i] == '+') s += ' ';
        else s += in[i];
    }
    return s;
}

// scrollable result viewer
static void showResult(const char *title, const String &in, const String &out) {
    int W = tft.width(), H = tft.height();
    uint16_t bg = bruceConfig.bgColor, pri = bruceConfig.priColor, sec = bruceConfig.secColor;
    int scroll = 0;
    // pre-wrap output into lines of ~ (W-12)/6 chars
    int cpl = (W - 12) / 6; if (cpl < 10) cpl = 10;
    std::vector<String> lines;
    for (size_t i = 0; i < out.length(); i += cpl) lines.push_back(out.substring(i, i + cpl));
    if (lines.empty()) lines.push_back("(empty)");
    int rows = (H - 60) / 10;
    while (true) {
        if (check(EscPress) || check(SelPress)) break;
        if (check(DownPress) && scroll < (int)lines.size() - rows) scroll++;
        if (check(UpPress) && scroll > 0) scroll--;
        tft.fillScreen(bg);
        tft.setTextColor(pri, bg); tft.drawString(title, 6, 4);
        tft.drawFastHLine(0, 16, W, sec);
        tft.setTextColor(sec, bg);
        String inShort = in.length() > (size_t)cpl ? in.substring(0, cpl - 1) + "~" : in;
        tft.drawString("in: " + inShort, 6, 20);
        tft.setTextColor(pri, bg);
        int y = 34;
        for (int i = scroll; i < (int)lines.size() && y < H - 14; i++) { tft.drawString(lines[i], 6, y); y += 10; }
        tft.setTextColor(sec, bg);
        tft.drawString(lines.size() > (size_t)rows ? "[UP/DN] scroll [SEL/ESC] back" : "[SEL/ESC] back", 4, H - 11);
        vTaskDelay(40 / portTICK_PERIOD_MS);
    }
}

void deuceCrypto2() {
    while (true) {
        std::vector<Option> ops = {
            {"SHA-256",      []() {}},
            {"SHA-1",        []() {}},
            {"MD5",          []() {}},
            {"Base64 enc",   []() {}},
            {"Base64 dec",   []() {}},
            {"Hex enc",      []() {}},
            {"Hex dec",      []() {}},
            {"ROT13",        []() {}},
            {"URL enc",      []() {}},
            {"URL dec",      []() {}},
        };
        int sel = loopOptions(ops, MENU_TYPE_SUBMENU, "Crypto Tools");
        if (sel < 0) return;

        String in = keyboard("", 128, "Input text");
        if (in.length() == 0) continue;
        String out, title;
        switch (sel) {
            case 0: out = hashHex(MBEDTLS_MD_SHA256, in); title = "SHA-256"; break;
            case 1: out = hashHex(MBEDTLS_MD_SHA1, in);   title = "SHA-1";   break;
            case 2: out = hashHex(MBEDTLS_MD_MD5, in);    title = "MD5";     break;
            case 3: out = b64enc(in); title = "Base64 encode"; break;
            case 4: out = b64dec(in); title = "Base64 decode"; break;
            case 5: out = hexenc(in); title = "Hex encode"; break;
            case 6: out = hexdec(in); title = "Hex decode"; break;
            case 7: out = rot13(in);  title = "ROT13"; break;
            case 8: out = urlenc(in); title = "URL encode"; break;
            case 9: out = urldec(in); title = "URL decode"; break;
            default: continue;
        }
        showResult(title.c_str(), in, out);
    }
}
