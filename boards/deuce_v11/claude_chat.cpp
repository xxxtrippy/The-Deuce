// THE DEUCE v1.1 -- text-only Claude chat over the Anthropic Messages API.
//
// Flow: WiFi -> HTTPS POST https://api.anthropic.com/v1/messages -> JSON reply text.
// Multi-turn: the last few turns are kept in RAM and resent each call so follow-ups
// have context. Strictly text; no images are ever sent or requested.
//
// Storage (LittleFS, separate from bruce.conf so no long rebuild and so the WebUI can
// write the key independently):
//   /deuce_claude.key  -> the sk-ant-... API key, one line
//   /deuce_claude.cfg  -> line 1 = model id, line 2 = max_tokens
//
// The key can be set on-device (on-screen keyboard) OR from the WebUI at
// http://deuce.local/claude  (see the /claude route in webInterface.cpp).

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
// state
struct ClaudeMsg {
    bool user;    // true = user turn, false = assistant turn
    String text;
};
static std::vector<ClaudeMsg> g_hist;                  // conversation (trimmed)
static String g_model = "claude-haiku-5-5";            // default; change in Settings
static int g_maxTokens = 512;                          // keep modest for RAM + screen
static String g_apiKey = "";                           // loaded from /deuce_claude.key
static bool g_cfgLoaded = false;

static const size_t HISTORY_MAX = 8; // last N turns resent for context (RAM bound)

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
// helpers
static bool claudeWifi() {
    if (WiFi.status() == WL_CONNECTED) return true;
    return wifiConnectMenu(); // Bruce's connect UI; returns true once associated
}

// Keep history bounded and valid: at most HISTORY_MAX turns, and it must start with a
// user turn (the API requires the first message to be from the user).
static void claudeTrim() {
    while (g_hist.size() > HISTORY_MAX) g_hist.erase(g_hist.begin());
    while (!g_hist.empty() && !g_hist.front().user) g_hist.erase(g_hist.begin());
}

// POST the conversation (with the new user message appended) and return the reply text,
// or a human-readable error string. On any failure the just-added user turn is rolled
// back so history never ends on a dangling user message.
static String claudeSend(const String &userMsg) {
    g_hist.push_back({true, userMsg});
    claudeTrim();

    String body;
    {
        JsonDocument doc;
        doc["model"] = g_model;
        doc["max_tokens"] = g_maxTokens;
        JsonArray arr = doc["messages"].to<JsonArray>();
        for (auto &m : g_hist) {
            JsonObject o = arr.add<JsonObject>();
            o["role"] = m.user ? "user" : "assistant";
            o["content"] = m.text;
        }
        serializeJson(doc, body);
    }

    WiFiClientSecure client;
    client.setInsecure(); // same as Bruce's other HTTPS features (no cert pinning)
    HTTPClient https;
    https.setConnectTimeout(15000);
    https.setTimeout(30000);
    if (!https.begin(client, "https://api.anthropic.com/v1/messages")) {
        g_hist.pop_back();
        return "Could not start HTTPS connection.";
    }
    https.addHeader("content-type", "application/json");
    https.addHeader("x-api-key", g_apiKey);
    https.addHeader("anthropic-version", "2023-06-01");

    int code = https.POST(body);
    String resp = https.getString();
    https.end();

    if (code <= 0) {
        g_hist.pop_back();
        return String("Network error: ") + HTTPClient::errorToString(code);
    }

    JsonDocument rdoc;
    DeserializationError err = deserializeJson(rdoc, resp);
    if (err) {
        g_hist.pop_back();
        return String("Bad response (HTTP ") + code + "): " + err.c_str();
    }
    if (!rdoc["error"].isNull()) {
        g_hist.pop_back();
        const char *em = rdoc["error"]["message"] | "unknown error";
        return String("API error (HTTP ") + code + "): " + em;
    }

    String text = "";
    JsonArray content = rdoc["content"].as<JsonArray>();
    for (JsonObject blk : content) {
        const char *t = blk["type"] | "";
        if (strcmp(t, "text") == 0) text += (const char *)(blk["text"] | "");
    }
    if (text.length() == 0) {
        g_hist.pop_back();
        return String("Empty reply (HTTP ") + code + ").";
    }

    g_hist.push_back({false, text});
    claudeTrim();
    return text;
}

static void claudeShowText(const String &title, const String &text) {
    ScrollableTextArea area(title);
    area.fromString(text);
    area.show(); // blocks with scroll until the user backs out
}

// Compose a message, send it, show the reply. History is preserved across calls.
static void claudeCompose() {
    claudeLoadKey(); // refresh in case the key was just set via the WebUI
    if (g_apiKey.length() == 0) {
        displayError("Set your API key first (Settings).", true);
        return;
    }
    String msg = keyboard("", 240, "Message to Claude:");
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

static void claudeReadLast() {
    for (int i = (int)g_hist.size() - 1; i >= 0; i--) {
        if (!g_hist[i].user) {
            claudeShowText("Claude", g_hist[i].text);
            return;
        }
    }
    displayInfo("No replies yet.", true);
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
        o.push_back({"Clear Chat", []() {
                         g_hist.clear();
                         displayInfo("Chat cleared.", true);
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
        o.push_back({"Send Message", []() { claudeCompose(); }});
        o.push_back({"New Chat", []() {
                         g_hist.clear();
                         claudeCompose();
                     }});
        if (!g_hist.empty()) o.push_back({"Read Last Reply", []() { claudeReadLast(); }});
        o.push_back({"Settings", []() { deuceClaudeSettings(); }});
        o.push_back({"Back", []() {}});
        int sel = loopOptions(o, MENU_TYPE_SUBMENU, "Claude");
        if (sel == -1 || sel == (int)o.size() - 1) return; // Esc or Back
    }
}
