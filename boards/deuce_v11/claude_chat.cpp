// THE DEUCE v1.1 -- text-only Claude chat over the Anthropic Messages API.
//
// Deliberately STATELESS: one question in, one answer out. No conversation history is
// kept, so every request is tiny -- which keeps RAM minimal and the TLS connection
// reliable on the no-PSRAM WROOM (long histories were exhausting the heap). Each "Chat"
// is a fresh single turn.
//
// Storage (LittleFS, separate from bruce.conf):
//   /deuce_claude.key  -> the sk-ant-... API key, one line
//   /deuce_claude.cfg  -> line 1 = model id, line 2 = max_tokens
// The key can be set on-screen (Settings) or from the WebUI at  http://<device-ip>/claude

#include "claude_chat.h"

#include <Arduino.h>
#include "core/display.h"
#include "core/mykeyboard.h"
#include "core/scrollableTextArea.h"
#include "core/wifi/wifi_common.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <globals.h>
#include <vector>

// ---------------------------------------------------------------------------
// settings (persisted)
static String g_model = "claude-haiku-5-5"; // default; change in Settings
static int g_maxTokens = 512;               // reply length cap (64..4096)
static String g_apiKey = "";                // loaded from /deuce_claude.key
static bool g_cfgLoaded = false;

// ---------------------------------------------------------------------------
// persistence
static void claudeLoadKey() {
    g_apiKey = "";
    File k = LittleFS.open("/deuce_claude.key", "r");
    if (k) {
        String s = k.readString();
        s.trim();
        g_apiKey = s;
        k.close();
    }
}
static void claudeLoadCfg() {
    File f = LittleFS.open("/deuce_claude.cfg", "r");
    if (f) {
        String m = f.readStringUntil('\n');
        m.trim();
        if (m.length()) g_model = m;
        int v = f.readStringUntil('\n').toInt();
        if (v >= 64 && v <= 4096) g_maxTokens = v;
        f.close();
    }
}
static void claudeLoad() {
    if (g_cfgLoaded) return;
    claudeLoadCfg();
    claudeLoadKey();
    g_cfgLoaded = true;
}
static void claudeSaveCfg() {
    File f = LittleFS.open("/deuce_claude.cfg", "w");
    if (!f) return;
    f.println(g_model);
    f.println(g_maxTokens);
    f.close();
}
static void claudeSaveKey() {
    File f = LittleFS.open("/deuce_claude.key", "w");
    if (!f) return;
    f.print(g_apiKey);
    f.close();
}

// ---------------------------------------------------------------------------
static bool claudeWifi() {
    if (WiFi.status() == WL_CONNECTED) return true;
    return wifiConnectMenu(); // Bruce's connect UI; returns true once associated
}

// Send ONE user message, return the reply text (or a human-readable error). Stateless:
// no history, so the request is always small.
static String claudeSend(const String &userMsg) {
    String body;
    {
        JsonDocument doc;
        doc["model"] = g_model;
        doc["max_tokens"] = g_maxTokens;
        JsonArray arr = doc["messages"].to<JsonArray>();
        JsonObject o = arr.add<JsonObject>();
        o["role"] = "user";
        o["content"] = userMsg;
        serializeJson(doc, body);
    }

    // The ESP32 TLS+HTTP stack intermittently returns an empty body on an HTTP 200, so we
    // (a) force HTTP/1.0 for a plain Content-Length body getString() reads reliably, and
    // (b) retry once if the body still comes back empty.
    int code = 0;
    String resp;
    for (int attempt = 1; attempt <= 2; attempt++) {
        WiFiClientSecure client;
        client.setInsecure(); // same as Bruce's other HTTPS features (no cert pinning)
        HTTPClient https;
        https.setConnectTimeout(15000);
        https.setTimeout(30000);
        if (!https.begin(client, "https://api.anthropic.com/v1/messages")) {
            delay(400);
            continue;
        }
        https.useHTTP10(true);
        https.addHeader("content-type", "application/json");
        https.addHeader("x-api-key", g_apiKey);
        https.addHeader("anthropic-version", "2023-06-01");
        code = https.POST(body);
        resp = https.getString();
        https.end();
        if (code > 0 && resp.length() > 0) break; // got a body -> stop retrying
        delay(400);                               // transient empty -> pause, try once more
    }

    if (code <= 0)
        return String("Network error ") + code + " (" + HTTPClient::errorToString(code) +
               "), free heap " + (unsigned)ESP.getFreeHeap();

    JsonDocument rdoc;
    DeserializationError err = deserializeJson(rdoc, resp);
    if (err) return String("Bad response (HTTP ") + code + "): " + err.c_str();
    if (!rdoc["error"].isNull()) {
        const char *em = rdoc["error"]["message"] | "unknown error";
        return String("API error (HTTP ") + code + "): " + em;
    }

    String text = "";
    JsonArray content = rdoc["content"].as<JsonArray>();
    for (JsonObject blk : content) {
        const char *t = blk["type"] | "";
        if (strcmp(t, "text") == 0) text += (const char *)(blk["text"] | "");
    }
    if (text.length() == 0) return String("Empty reply (HTTP ") + code + ").";
    return text;
}

static void claudeShowText(const String &title, const String &text) {
    ScrollableTextArea area(title);
    area.fromString(text);
    area.show(); // blocks with scroll until the user backs out
}

// One question, one answer. Returns to the menu afterward (fresh each time).
static void claudeChat() {
    claudeLoadKey(); // refresh in case the key was just set via the WebUI
    if (g_apiKey.length() == 0) {
        displayError("Set your API key first (Settings).", true);
        return;
    }
    String msg = keyboard("", 240, "Ask Claude:");
    msg.trim();
    if (msg.length() == 0) return;

    if (!claudeWifi()) {
        displayError("WiFi connection needed.", true);
        return;
    }

    drawMainBorderWithTitle("Claude");
    padprintln("");
    padprintln("  Thinking...");
    String reply = claudeSend(msg);
    claudeShowText("Claude", reply);
}

static void claudePickModel() {
    static const char *MODELS[] = {
        "claude-haiku-5-5", "claude-sonnet-5-5", "claude-opus-5-5", "claude-3-5-haiku-latest"};
    std::vector<Option> o;
    for (auto m : MODELS) {
        String mm = m;
        o.push_back({mm.c_str(), [mm]() {
                         g_model = mm;
                         claudeSaveCfg();
                     }});
    }
    o.push_back({"Custom...", []() {
                     String m = keyboard(g_model, 64, "Model id:");
                     m.trim();
                     if (m.length()) {
                         g_model = m;
                         claudeSaveCfg();
                     }
                 }});
    o.push_back({"Back", []() {}});
    loopOptions(o, MENU_TYPE_SUBMENU, "Model");
}

// ---------------------------------------------------------------------------
// public entry points
void deuceClaudeSettings() {
    claudeLoad();
    while (true) {
        std::vector<Option> o;
        o.push_back({"Set API Key", []() {
                         String k = keyboard("", 128, "API key (sk-ant-...):", true);
                         k.trim();
                         if (k.length()) {
                             g_apiKey = k;
                             claudeSaveKey();
                             displayInfo("Key saved.", true);
                         }
                     }});
        o.push_back({(String("Model: ") + g_model).c_str(), []() { claudePickModel(); }});
        o.push_back({(String("Max tokens: ") + g_maxTokens).c_str(), []() {
                         String s = num_keyboard(String(g_maxTokens), 5, "Max tokens (64-4096):");
                         int v = s.toInt();
                         if (v >= 64 && v <= 4096) {
                             g_maxTokens = v;
                             claudeSaveCfg();
                         }
                     }});
        o.push_back({"Back", []() {}});
        int sel = loopOptions(o, MENU_TYPE_SUBMENU, "Claude Setup");
        if (sel == -1 || sel == (int)o.size() - 1) return; // Esc or Back
    }
}

void deuceClaudeLaunch() {
    claudeLoad();
    while (true) {
        std::vector<Option> o;
        o.push_back({"Chat", []() { claudeChat(); }});
        o.push_back({"Settings", []() { deuceClaudeSettings(); }});
        int sel = loopOptions(o, MENU_TYPE_SUBMENU, "Claude");
        if (sel == -1) return; // Esc / Back button -> back to the grid
    }
}
