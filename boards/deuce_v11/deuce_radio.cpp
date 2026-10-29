// THE DEUCE v1.1 -- WiFi Radio station (see .h). SD /music -> HTTP; browser plays.

#include "deuce_radio.h"

#include <Arduino.h>
#include <SD.h>
#include <globals.h>
#include <vector>

#include "core/display.h"
#include "core/sd_functions.h"       // setupSdCard, sdcardMounted
#include "core/wifi/webInterface.h"  // startWebUi, server

static const char *RADIO_DIR = "/music";

struct RTrack {
    String file;   // full SD path
    String title;  // ID3 or filename
    String artist; // ID3 or ""
    uint32_t durMs;
};

static std::vector<RTrack> g_tracks;
static uint32_t g_totalMs = 0;
static uint32_t g_startMs = 0;
static bool g_scanned = false;

// ---------------------------------------------------------------------------
// MP3 metadata: ID3 title/artist + playback duration
// ---------------------------------------------------------------------------
static uint32_t syncsafe(const uint8_t *p) {
    return ((uint32_t)(p[0] & 0x7f) << 21) | ((uint32_t)(p[1] & 0x7f) << 14) |
           ((uint32_t)(p[2] & 0x7f) << 7) | (uint32_t)(p[3] & 0x7f);
}

static String textFromFrame(const uint8_t *d, uint32_t n) {
    if (n < 1) return "";
    uint8_t enc = d[0];
    String out;
    if (enc == 1 || enc == 2) { // UTF-16: crude ASCII fold (every other byte)
        for (uint32_t i = 1; i + 1 < n; i += 2) {
            uint8_t c = d[i] ? d[i] : d[i + 1];
            if (c >= 32 && c < 127) out += (char)c;
        }
    } else { // latin1 / utf8
        for (uint32_t i = 1; i < n; i++) {
            uint8_t c = d[i];
            if (c >= 32 && c < 127) out += (char)c;
        }
    }
    out.trim();
    return out;
}

// Parse ID3v2 (front) for TIT2/TPE1; returns bytes of the ID3v2 tag (audio start).
static uint32_t parseId3(File &f, RTrack &t) {
    uint8_t h[10];
    f.seek(0);
    if (f.read(h, 10) != 10) return 0;
    if (!(h[0] == 'I' && h[1] == 'D' && h[2] == '3')) return 0;
    uint8_t major = h[3];
    uint32_t tagSize = syncsafe(&h[6]);
    uint32_t readLen = tagSize;
    if (readLen > 4096) readLen = 4096; // cap: tags live near the front
    uint8_t *buf = (uint8_t *)malloc(readLen);
    if (!buf) return tagSize + 10;
    uint32_t got = f.read(buf, readLen);
    uint32_t i = 0;
    while (i + 10 <= got) {
        char id[5] = {(char)buf[i], (char)buf[i + 1], (char)buf[i + 2], (char)buf[i + 3], 0};
        uint32_t fsz;
        if (major == 4) fsz = syncsafe(&buf[i + 4]);
        else
            fsz = ((uint32_t)buf[i + 4] << 24) | ((uint32_t)buf[i + 5] << 16) |
                  ((uint32_t)buf[i + 6] << 8) | buf[i + 7];
        if (id[0] == 0 || fsz == 0 || i + 10 + fsz > got) break;
        const uint8_t *d = &buf[i + 10];
        if (!strcmp(id, "TIT2")) t.title = textFromFrame(d, fsz);
        else if (!strcmp(id, "TPE1")) t.artist = textFromFrame(d, fsz);
        i += 10 + fsz;
    }
    free(buf);
    return tagSize + 10;
}

static const int BR_V1L3[16] = {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0};
static const int BR_V2L3[16] = {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0};
static const int SR_TAB[4][3] = {
    {11025, 12000, 8000}, // MPEG2.5
    {0, 0, 0},
    {22050, 24000, 16000}, // MPEG2
    {44100, 48000, 32000}, // MPEG1
};

// Compute duration from the first audio frame (CBR estimate, Xing/Info if present).
static uint32_t mp3Duration(File &f, uint32_t audioStart) {
    uint32_t fsize = f.size();
    // detect trailing ID3v1
    uint32_t audioBytes = fsize - audioStart;
    if (fsize >= 128) {
        uint8_t tag[3];
        f.seek(fsize - 128);
        if (f.read(tag, 3) == 3 && tag[0] == 'T' && tag[1] == 'A' && tag[2] == 'G') audioBytes -= 128;
    }

    // find a frame sync at/after audioStart
    f.seek(audioStart);
    uint8_t chunk[512];
    uint32_t got = f.read(chunk, sizeof(chunk));
    int off = -1;
    for (uint32_t i = 0; i + 4 < got; i++) {
        if (chunk[i] == 0xFF && (chunk[i + 1] & 0xE0) == 0xE0) {
            off = i;
            break;
        }
    }
    if (off < 0) return 180000; // fallback 3 min
    uint8_t b1 = chunk[off + 1], b2 = chunk[off + 2], b3 = chunk[off + 3];
    int verBits = (b1 >> 3) & 0x03; // 0=2.5,2=2,3=1
    int layer = (b1 >> 1) & 0x03;   // 1=III
    int brIdx = (b2 >> 4) & 0x0F;
    int srIdx = (b2 >> 2) & 0x03;
    int pad = (b2 >> 1) & 0x01;
    int chMode = (b3 >> 6) & 0x03;
    if (layer != 1 || srIdx == 3 || brIdx == 0 || brIdx == 15) return 180000;
    bool mpeg1 = (verBits == 3);
    int bitrate = (mpeg1 ? BR_V1L3[brIdx] : BR_V2L3[brIdx]) * 1000;
    int srate = SR_TAB[verBits][srIdx];
    if (bitrate <= 0 || srate <= 0) return 180000;
    int samplesPerFrame = mpeg1 ? 1152 : 576;

    // Xing/Info (VBR) frame count for accuracy
    int sideInfo = mpeg1 ? (chMode == 3 ? 17 : 32) : (chMode == 3 ? 9 : 17);
    uint32_t xoff = audioStart + off + 4 + sideInfo;
    f.seek(xoff);
    uint8_t xb[12];
    if (f.read(xb, 12) == 12) {
        bool xing = (xb[0] == 'X' && xb[1] == 'i' && xb[2] == 'n' && xb[3] == 'g') ||
                    (xb[0] == 'I' && xb[1] == 'n' && xb[2] == 'f' && xb[3] == 'o');
        if (xing) {
            uint32_t flags = ((uint32_t)xb[4] << 24) | ((uint32_t)xb[5] << 16) | ((uint32_t)xb[6] << 8) |
                             xb[7];
            if (flags & 0x01) {
                uint32_t frames = ((uint32_t)xb[8] << 24) | ((uint32_t)xb[9] << 16) |
                                  ((uint32_t)xb[10] << 8) | xb[11];
                double sec = (double)frames * samplesPerFrame / srate;
                return (uint32_t)(sec * 1000.0);
            }
        }
    }
    // CBR estimate
    double sec = (double)audioBytes * 8.0 / bitrate;
    return (uint32_t)(sec * 1000.0);
}

static String baseName(const String &path) {
    int s = path.lastIndexOf('/');
    String n = (s >= 0) ? path.substring(s + 1) : path;
    int d = n.lastIndexOf('.');
    if (d > 0) n = n.substring(0, d);
    return n;
}

static void radioScan() {
    g_tracks.clear();
    g_totalMs = 0;
    g_scanned = true;
    if (!sdcardMounted) return;
    File dir = SD.open(RADIO_DIR);
    if (!dir || !dir.isDirectory()) {
        if (dir) dir.close();
        return;
    }
    File e;
    while ((e = dir.openNextFile())) {
        if (!e.isDirectory()) {
            String name = e.name();
            String low = name;
            low.toLowerCase();
            if (low.endsWith(".mp3")) {
                RTrack t;
                t.file = name.startsWith("/") ? name : (String(RADIO_DIR) + "/" + name);
                t.durMs = 0;
                File mf = SD.open(t.file, FILE_READ);
                if (mf) {
                    uint32_t audioStart = parseId3(mf, t);
                    t.durMs = mp3Duration(mf, audioStart);
                    mf.close();
                }
                if (t.title.length() == 0) t.title = baseName(t.file);
                if (t.durMs < 1000) t.durMs = 180000;
                g_tracks.push_back(t);
                g_totalMs += t.durMs;
            }
        }
        e.close();
        if (g_tracks.size() >= 250) break; // sanity cap
    }
    dir.close();
    if (g_totalMs == 0) g_totalMs = 1;
}

static void ensureScanned() {
    if (!g_scanned) radioScan();
    if (g_startMs == 0) g_startMs = millis();
}

// current broadcast position from the single server clock
static void radioPos(int &idx, uint32_t &posMs) {
    idx = 0;
    posMs = 0;
    if (g_tracks.empty() || g_totalMs == 0) return;
    uint32_t el = (millis() - g_startMs) % g_totalMs;
    uint32_t acc = 0;
    for (size_t i = 0; i < g_tracks.size(); i++) {
        if (el < acc + g_tracks[i].durMs) {
            idx = (int)i;
            posMs = el - acc;
            return;
        }
        acc += g_tracks[i].durMs;
    }
    idx = (int)g_tracks.size() - 1;
}

// ---------------------------------------------------------------------------
// listener counter (fresh unique client IPs within 6s of hitting /radio/now)
// ---------------------------------------------------------------------------
struct Lis {
    uint32_t ip;
    uint32_t last;
};
static Lis g_lis[16];
static int radioTouchListener(uint32_t ip) {
    uint32_t now = millis();
    int slot = -1, count = 0;
    for (int i = 0; i < 16; i++) {
        if (g_lis[i].last && now - g_lis[i].last > 6000) g_lis[i].last = 0;
        if (g_lis[i].last && g_lis[i].ip == ip) slot = i;
    }
    if (slot < 0)
        for (int i = 0; i < 16; i++)
            if (!g_lis[i].last) {
                slot = i;
                break;
            }
    if (slot >= 0) {
        g_lis[slot].ip = ip;
        g_lis[slot].last = now;
    }
    for (int i = 0; i < 16; i++)
        if (g_lis[i].last) count++;
    return count;
}

static String jsonEsc(const String &s) {
    String o;
    for (size_t i = 0; i < s.length(); i++) {
        char c = s[i];
        if (c == '"' || c == '\\') {
            o += '\\';
            o += c;
        } else if (c >= 32) o += c;
    }
    return o;
}

static String c565(uint16_t c) {
    uint8_t r = ((c >> 11) & 31) * 255 / 31;
    uint8_t g = ((c >> 5) & 63) * 255 / 63;
    uint8_t b = (c & 31) * 255 / 31;
    char buf[8];
    snprintf(buf, sizeof(buf), "#%02x%02x%02x", r, g, b);
    return String(buf);
}

// ---------------------------------------------------------------------------
// Player page (matrix-themed; theme colours injected). Kept compact.
// ---------------------------------------------------------------------------
static const char RADIO_HTML[] PROGMEM = R"HTML(<!doctype html><html><head><meta charset=utf-8>
<meta name=viewport content='width=device-width,initial-scale=1'>
<title>The Deuce Radio</title><style>
:root{--pri:{{PRI}};--sec:{{SEC}};--bg:{{BG}}}
*{box-sizing:border-box}html,body{margin:0;height:100%}
body{background:var(--bg);color:var(--pri);font-family:ui-monospace,Menlo,Consolas,monospace;overflow:hidden}
#rain{position:fixed;inset:0;z-index:0;opacity:.25}
#app{position:relative;z-index:1;max-width:560px;margin:0 auto;padding:16px;height:100%;display:flex;flex-direction:column;gap:12px}
.hd{display:flex;justify-content:space-between;align-items:center;border-bottom:1px solid var(--sec);padding-bottom:8px}
.hd b{letter-spacing:2px}
.live{color:var(--sec);font-size:12px}
.np{border:1px solid var(--sec);border-radius:10px;padding:14px;background:rgba(0,0,0,.35)}
.np .t{font-size:20px;font-weight:700;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.np .a{color:var(--sec);margin-top:2px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.bar{height:6px;background:rgba(255,255,255,.12);border-radius:3px;margin-top:12px;overflow:hidden}
.bar>i{display:block;height:100%;width:0;background:var(--pri)}
.tm{display:flex;justify-content:space-between;font-size:11px;color:var(--sec);margin-top:4px}
.vol{display:flex;align-items:center;gap:8px;margin-top:10px;color:var(--sec);font-size:12px}
.vol input{flex:1}
.list{flex:1;overflow:auto;border:1px solid var(--sec);border-radius:10px}
.row{padding:9px 12px;border-bottom:1px solid rgba(255,255,255,.07);display:flex;gap:8px;font-size:13px}
.row.cur{background:rgba(255,255,255,.08);color:var(--pri)}
.row.cur .n::before{content:'> '}
.row .n{flex:1;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;color:var(--sec)}
.row.cur .n{color:var(--pri);font-weight:600}
.row .d{color:var(--sec);font-size:11px}
#tune{position:fixed;inset:0;z-index:5;display:flex;align-items:center;justify-content:center;background:var(--bg)}
#tune button{background:transparent;border:2px solid var(--pri);color:var(--pri);font:inherit;font-size:22px;letter-spacing:3px;padding:18px 34px;border-radius:12px;cursor:pointer}
</style></head><body>
<canvas id=rain></canvas>
<div id=app>
 <div class=hd><b>THE DEUCE // RADIO</b><span class=live>&#9679; <span id=ls>0</span> on air</span></div>
 <div class=np>
  <div class=t id=title>&mdash;</div><div class=a id=artist></div>
  <div class=bar><i id=prog></i></div>
  <div class=tm><span id=cur>0:00</span><span id=dur>0:00</span></div>
  <div class=vol>VOL<input id=vol type=range min=0 max=100 value=80></div>
 </div>
 <div class=list id=list></div>
</div>
<div id=tune><button id=tb>&#9654; TUNE IN</button></div>
<audio id=au></audio>
<script>
var au=document.getElementById('au'),cur=-1,started=false,PL=[];
function fmt(s){s=Math.max(0,s|0);var m=s/60|0;var r=s%60;return m+':'+(r<10?'0':'')+r}
function esc(x){return (x||'').replace(/[&<>]/g,function(c){return{'&':'&amp;','<':'&lt;','>':'&gt;'}[c]})}
async function loadPL(){try{var r=await fetch('/radio/playlist');PL=await r.json();var h='';for(var i=0;i<PL.length;i++){h+='<div class=row id=r'+i+'><span class=n>'+esc(PL[i].title)+(PL[i].artist?(' &middot; '+esc(PL[i].artist)):'')+'</span><span class=d>'+fmt(PL[i].dur)+'</span></div>'}document.getElementById('list').innerHTML=h}catch(e){}}
function mark(i){var el=document.querySelector('.row.cur');if(el)el.className='row';var n=document.getElementById('r'+i);if(n){n.className='row cur';n.scrollIntoView({block:'nearest'})}}
async function tick(){try{var r=await fetch('/radio/now');var n=await r.json();
 document.getElementById('title').textContent=n.title||'—';
 document.getElementById('artist').textContent=n.artist||'';
 document.getElementById('ls').textContent=n.listeners;
 document.getElementById('dur').textContent=fmt(n.dur);
 if(n.idx!==cur){cur=n.idx;mark(cur);if(started){au.src='/radio/stream?i='+n.idx+'&c='+Date.now();au.load();au.addEventListener('loadedmetadata',function h(){try{au.currentTime=n.pos}catch(e){}au.play().catch(function(){});au.removeEventListener('loadedmetadata',h)})}}
 else if(started&&au.readyState>0){if(Math.abs(au.currentTime-n.pos)>1.8){try{au.currentTime=n.pos}catch(e){}}}
 var cu=started?au.currentTime:n.pos;document.getElementById('cur').textContent=fmt(cu);
 document.getElementById('prog').style.width=(n.dur?Math.min(100,cu/n.dur*100):0)+'%';
}catch(e){}}
document.getElementById('vol').oninput=function(){au.volume=this.value/100};au.volume=.8;
document.getElementById('tb').onclick=function(){started=true;cur=-1;document.getElementById('tune').style.display='none';au.play().catch(function(){});tick()};
loadPL();setInterval(tick,1000);tick();
// matrix rain
var cv=document.getElementById('rain'),cx=cv.getContext('2d'),cols,yp;
function rs(){cv.width=innerWidth;cv.height=innerHeight;cols=Math.floor(cv.width/12);yp=[];for(var i=0;i<cols;i++)yp[i]=Math.random()*cv.height}
rs();addEventListener('resize',rs);
var cs=getComputedStyle(document.documentElement),SEC=cs.getPropertyValue('--sec').trim();
setInterval(function(){cx.fillStyle='rgba(0,0,0,.08)';cx.fillRect(0,0,cv.width,cv.height);cx.fillStyle=SEC;cx.font='12px monospace';for(var i=0;i<cols;i++){var ch=String.fromCharCode(0x30A0+Math.random()*96);cx.fillText(ch,i*12,yp[i]);yp[i]=yp[i]>cv.height&&Math.random()>.975?0:yp[i]+12}},70);
</script></body></html>)HTML";

static void sendPlayer(AsyncWebServerRequest *request) {
    ensureScanned();
    String html = String(RADIO_HTML);
    html.replace("{{PRI}}", c565(bruceConfig.priColor));
    html.replace("{{SEC}}", c565(bruceConfig.secColor));
    html.replace("{{BG}}", c565(bruceConfig.bgColor));
    request->send(200, "text/html", html);
}

static void sendPlaylist(AsyncWebServerRequest *request) {
    ensureScanned();
    String j = "[";
    for (size_t i = 0; i < g_tracks.size(); i++) {
        if (i) j += ",";
        j += "{\"title\":\"" + jsonEsc(g_tracks[i].title) + "\",\"artist\":\"" +
             jsonEsc(g_tracks[i].artist) + "\",\"dur\":" + String(g_tracks[i].durMs / 1000) + "}";
    }
    j += "]";
    request->send(200, "application/json", j);
}

static void sendNow(AsyncWebServerRequest *request) {
    ensureScanned();
    if (g_tracks.empty()) {
        request->send(200, "application/json", "{\"idx\":0,\"pos\":0,\"dur\":0,\"title\":\"no music on SD\",\"artist\":\"\",\"listeners\":0,\"count\":0}");
        return;
    }
    int idx;
    uint32_t posMs;
    radioPos(idx, posMs);
    int listeners = radioTouchListener((uint32_t)request->client()->remoteIP());
    String j = "{\"idx\":" + String(idx) + ",\"pos\":" + String(posMs / 1000.0, 2) +
               ",\"dur\":" + String(g_tracks[idx].durMs / 1000.0, 2) + ",\"title\":\"" +
               jsonEsc(g_tracks[idx].title) + "\",\"artist\":\"" + jsonEsc(g_tracks[idx].artist) +
               "\",\"listeners\":" + String(listeners) + ",\"count\":" + String(g_tracks.size()) + "}";
    request->send(200, "application/json", j);
}

static void sendStream(AsyncWebServerRequest *request) {
    ensureScanned();
    if (!request->hasArg("i")) {
        request->send(400, "text/plain", "missing i");
        return;
    }
    int i = request->arg("i").toInt();
    if (i < 0 || i >= (int)g_tracks.size()) {
        request->send(404, "text/plain", "bad index");
        return;
    }
    // AsyncWebServer serves the file with HTTP Range support (seek + progressive play)
    request->send(SD, g_tracks[i].file, "audio/mpeg");
}

void deuceRadioConfigure(AsyncWebServer *s) {
    if (!s) return;
    s->on("/radio", HTTP_GET, sendPlayer);
    s->on("/radio/playlist", HTTP_GET, sendPlaylist);
    s->on("/radio/now", HTTP_GET, sendNow);
    s->on("/radio/stream", HTTP_GET, sendStream);
}

// ---------------------------------------------------------------------------
// WiFi-menu entry
// ---------------------------------------------------------------------------
void deuceWifiRadio() {
    if (!sdcardMounted && !setupSdCard()) {
        displayError("WiFi Radio needs an SD card", true);
        return;
    }
    drawMainBorderWithTitle("WiFi Radio");
    padprintln("");
    padprintln("Scanning " + String(RADIO_DIR) + " ...");
    radioScan();
    g_startMs = millis();
    if (g_tracks.empty()) {
        displayError("No MP3s found in /music", true);
        return;
    }
    drawMainBorderWithTitle("WiFi Radio");
    padprintln("");
    padprintln(String(g_tracks.size()) + " tracks queued.");
    padprintln("");
    padprintln("Starting WiFi + WebUI...");
    padprintln("Open  http://deuce.local/radio");
    delay(1200);
    // Brings up WiFi (STA) + the web server (registers /radio* via configureWebServer),
    // and shows the WebUI screen with the device URL + run-in-background option.
    startWebUi(false);
}
