// FieldMesh repeater: WiFi dashboard (P5a: access point, captive portal, live status, clock).
// Only compiled in with -D FIELDMESH_REPEATER=1 on ESP32.
//
// Threading: the web server (ESPAsyncWebServer) answers requests in its own FreeRTOS task.
// Handlers never touch mesh data. loop() builds a JSON snapshot every 2 s under a mutex;
// handlers only copy that snapshot. Input from the page (e.g. the phone's clock) goes into
// a small hand-over variable that loop() picks up.

#include "MyMesh.h"

#if defined(FIELDMESH_REPEATER) && defined(ESP32)

#include <WiFi.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>

#define FM_WEB_SNAPSHOT_INTERVAL  2000
#define FM_WEB_MAX_STATIONS       10              // phones at the same time (ESP32-S3 allows up to 15)
#define FM_WEB_MDNS_FALLBACK      "fieldmesh"     // .local name if the public name has no usable letters
#define FM_WEB_CLOCK_MIN_STEP     30              // s: smaller differences are ignored
#define FM_WEB_CLOCK_MAX_STEP     (365UL * 86400) // s: a phone further ahead is not trusted

static AsyncWebServer* fm_server = NULL;
static DNSServer* fm_dns = NULL;
static SemaphoreHandle_t fm_snap_mutex = NULL;
static char fm_snap[1024] = "{}";
static volatile uint32_t fm_pending_time = 0;   // phone clock, handed over to loop()
static bool fm_web_running = false;
static char fm_web_ssid[33] = "";
static char fm_web_host[64] = "";   // mDNS name without ".local", made from the public name ("BaseCamp" -> "basecamp")
static unsigned long fm_next_snapshot = 0;
static unsigned long fm_next_big_snapshot = 0;

// bigger snapshots (P5c), in PSRAM (malloc > 4 KB goes to PSRAM on the G2)
struct FmSnap { char* buf; size_t cap; size_t len; };
static FmSnap fm_snap_nodes = { NULL, 48000, 0 };
static FmSnap fm_snap_net   = { NULL, 8000, 0 };
static FmSnap fm_snap_board = { NULL, 12000, 0 };
#define FM_WEB_BIG_SNAPSHOT_INTERVAL  5000
#define FM_WEB_NODES_MAX_AGE          86400   // s: nodes not heard for a day are left out
#define FM_WEB_POST_NAME_MAX          24
#define FM_WEB_POST_TEXT_MAX          120

// admin login (setup page): password = the repeater admin password, copied here by loop()
#define FM_WEB_MAX_TOKENS     4
#define FM_WEB_TOKEN_TTL_MS   3600000UL   // 1 h without use
#define FM_WEB_LOGIN_BLOCK_MS 2000        // after a wrong password (open WiFi: no fast guessing)
struct FmToken { char t[33]; unsigned long until; };
static FmToken fm_tokens[FM_WEB_MAX_TOKENS];
static char fm_admin_pw[17] = "";
static unsigned long fm_login_block_until = 0;
// admin command (a normal CLI command) handed over to loop(), result fetched with its id
static char fm_admin_cmd[160];
static bool fm_admin_pending = false;
static uint32_t fm_admin_seq = 0, fm_admin_done_id = 0;
static char fm_admin_reply[200] = "";

// post from the WiFi page, handed over to loop()
static char fm_post_name[FM_WEB_POST_NAME_MAX + 1];
static char fm_post_text[FM_WEB_POST_TEXT_MAX + 1];
static bool fm_post_pending = false;   // guarded by fm_snap_mutex

// small JSON writer (bounded, never overflows)
struct FmJson {
  char* dp; char* end;
  FmJson(char* buf, size_t cap) : dp(buf), end(buf + cap - 1) { }
  void raw(const char* s) { while (*s && dp < end) *dp++ = *s++; }
  void f(const char* fmt, ...) {
    if (dp >= end) return;
    va_list ap; va_start(ap, fmt);
    int n = vsnprintf(dp, end - dp + 1, fmt, ap);
    va_end(ap);
    if (n > 0) dp = (dp + n > end) ? end : dp + n;
  }
  void str(const char* s) {
    if (dp < end) *dp++ = '"';
    for (; *s && dp < end - 7; s++) {
      unsigned char c = (unsigned char)*s;
      if (c == '"' || c == '\\') { *dp++ = '\\'; *dp++ = c; }
      else if (c < 0x20) { dp += snprintf(dp, end - dp, "\\u%04x", c); }
      else *dp++ = c;
    }
    if (dp < end) *dp++ = '"';
  }
  size_t done(char* buf) { *dp = 0; return dp - buf; }
};

static void fmSendSnap(AsyncWebServerRequest* request, FmSnap& snap) {
  String body;
  if (fm_snap_mutex && xSemaphoreTake(fm_snap_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    if (snap.buf && snap.len) body = snap.buf; else body = "{}";
    xSemaphoreGive(fm_snap_mutex);
  } else {
    body = "{}";
  }
  AsyncWebServerResponse* res = request->beginResponse(200, "application/json", body);
  res->addHeader("Cache-Control", "no-store");
  request->send(res);
}

static String fmParam(AsyncWebServerRequest* request, const char* name) {
  if (request->hasParam(name, true)) return request->getParam(name, true)->value();
  if (request->hasParam(name)) return request->getParam(name)->value();
  return String("");
}

// valid admin token (body/query "k")? extends it. Caller holds no lock.
static bool fmTokenOk(AsyncWebServerRequest* request) {
  String k = fmParam(request, "k");
  if (k.length() != 32) return false;
  bool ok = false;
  if (xSemaphoreTake(fm_snap_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    for (int i = 0; i < FM_WEB_MAX_TOKENS; i++) {
      if (fm_tokens[i].t[0] && (long)(fm_tokens[i].until - millis()) > 0 && k.equals(fm_tokens[i].t)) {
        fm_tokens[i].until = millis() + FM_WEB_TOKEN_TTL_MS;
        ok = true;
        break;
      }
    }
    xSemaphoreGive(fm_snap_mutex);
  }
  return ok;
}

static void fmCleanInput(char* dest, size_t sz, const String& src) {
  size_t n = 0;
  for (size_t i = 0; i < src.length() && n + 1 < sz; i++) {
    unsigned char c = (unsigned char)src[i];
    dest[n++] = (c < 0x20) ? ' ' : c;   // no control characters (newlines) on the board
  }
  dest[n] = 0;
  while (n > 0 && dest[n - 1] == ' ') dest[--n] = 0;
}

// ---- the page (P5a: status only; full dashboard in P5e) ----
static const char FM_PAGE[] PROGMEM = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<meta name="color-scheme" content="light dark">
<title>FieldMesh</title>
<style>
:root{--bg:#0F1412;--surface:#171E1A;--border:#243029;--text:#E6EBE6;--muted:#9AA59C;--accent:#F2B544;--on-accent:#1B1405;--live:#6FD3A0;--bad:#FF8A7F}
@media (prefers-color-scheme: light){:root{--bg:#F3F5F2;--surface:#FFFFFF;--border:#D9E0DA;--text:#16201A;--muted:#56625A;--accent:#F2B544;--on-accent:#1B1405;--live:#1F9D60;--bad:#B3261E}}
body{margin:0;background:var(--bg);color:var(--text);font-family:system-ui,-apple-system,"Segoe UI",Roboto,sans-serif}
.wrap{max-width:560px;margin:0 auto;padding:16px}
header{display:flex;justify-content:space-between;align-items:flex-start;gap:12px;padding-bottom:12px;border-bottom:1px solid var(--border)}
.kicker{font-size:12px;letter-spacing:.08em;text-transform:uppercase;color:var(--muted)}
h1{margin:2px 0 0;font-size:22px}
.right{display:flex;flex-direction:column;align-items:flex-end;gap:4px;font-size:12px;color:var(--muted)}
.badge{font-size:12px;font-weight:800;letter-spacing:.06em;padding:4px 8px;border-radius:6px;background:var(--border);color:var(--text)}
.badge.og{background:var(--accent);color:var(--on-accent)}
.live{display:flex;align-items:center;gap:5px}.dot{width:8px;height:8px;border-radius:4px;background:var(--live)}
.live.off{color:var(--bad)}.live.off .dot{background:var(--bad)}
.grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:8px;margin-top:16px}
.card{padding:12px;border-radius:12px;background:var(--surface);border:1px solid var(--border)}
.lbl{font-size:12px;color:var(--muted)}.val{font-size:20px;font-weight:700;overflow-wrap:anywhere}
.note{margin-top:16px;font-size:13px;color:var(--muted)}
</style></head><body><div class="wrap">
<header><div><div class="kicker">FieldMesh</div><h1 id="name">-</h1></div>
<div class="right"><span id="mode" class="badge">-</span><span id="freq">-</span>
<span id="live" class="live"><span class="dot"></span><span id="livet">connecting</span></span></div></header>
<div class="grid">
<div class="card"><div class="lbl">Running for</div><div class="val" id="up">-</div></div>
<div class="card"><div class="lbl">Box clock</div><div class="val" id="clock">-</div></div>
<div class="card"><div class="lbl">Board</div><div class="val" id="board">-</div></div>
<div class="card"><div class="lbl">Phones on WiFi</div><div class="val" id="wifi">-</div></div>
<div class="card"><div class="lbl">Packets in / out</div><div class="val" id="pk">-</div></div>
<div class="card"><div class="lbl">Noise</div><div class="val" id="noise">-</div></div>
</div>
<p class="note">Address: <span id="addr">-</span> · fixed address for QR codes: http://192.168.4.1<br>Dashboard preview (P5a): Board, SOS, Mesh and Setup follow.</p>
</div><script>
var last=0,fails=0;
function set(id,v){var e=document.getElementById(id);if(e.textContent!==v)e.textContent=v;}
function dur(s){var d=Math.floor(s/86400),h=Math.floor(s/3600)%24,m=Math.floor(s/60)%60;return (d?d+' d ':'')+h+' h '+m+' min';}
function hm(t){var d=new Date(t*1000);return ('0'+d.getHours()).slice(-2)+':'+('0'+d.getMinutes()).slice(-2);}
var moved=false;
function toLocal(host){
// The fixed address (QR code: 192.168.4.1) moves on to http://<name>.local, but only when this
// phone can reach that name (iPhone: yes; Android before 12 often not: stay on the IP then).
if(moved||!host||location.hostname===host)return;moved=true;
var probe=fetch('http://'+host+'/api/ping',{mode:'no-cors',cache:'no-store'});
var timeout=new Promise(function(_,rej){setTimeout(rej,2500);});
Promise.race([probe,timeout]).then(function(){location.replace('http://'+host+'/');}).catch(function(){});}
function draw(j){set('name',j.name);set('addr','http://'+j.host);toLocal(j.host);set('mode',j.offgrid?'OFF-GRID':'NORMAL');
document.getElementById('mode').className='badge'+(j.offgrid?' og':'');
set('freq',j.freq.toFixed(3)+' MHz SF'+j.sf);set('up',dur(j.uptime));set('clock',hm(j.time));
set('board',j.room?(j.posts+' posts · '+j.users+' users'):'room off');set('wifi',String(j.stations));
set('pk',j.rx+' / '+j.tx);set('noise',j.noise+' dBm');}
function tick(){fetch('/api/status',{cache:'no-store'}).then(function(r){return r.json();}).then(function(j){
last=Date.now();fails=0;draw(j);}).catch(function(){fails++;});}
function live(){var e=document.getElementById('live');var age=last?Math.round((Date.now()-last)/1000):0;
if(last&&age<8){e.className='live';set('livet','Live · updated '+age+' s ago');}
else{e.className='live off';set('livet',last?'Offline · last data '+Math.round(age/60)+' min ago':'connecting');}}
fetch('/api/time',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'t='+Math.floor(Date.now()/1000)}).catch(function(){});
tick();setInterval(tick,3000);setInterval(live,1000);
</script></body></html>)HTML";

static void fmJsonStr(char*& dp, const char* end, const char* s) {
  if (dp < end) *dp++ = '"';
  for (; *s && dp < end - 7; s++) {
    unsigned char c = (unsigned char)*s;
    if (c == '"' || c == '\\') { *dp++ = '\\'; *dp++ = c; }
    else if (c < 0x20) { dp += snprintf(dp, end - dp, "\\u%04x", c); }
    else *dp++ = c;
  }
  if (dp < end) *dp++ = '"';
}

static void fmWebSetupRoutes() {
  fm_server->on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
    AsyncWebServerResponse* res = request->beginResponse(200, "text/html", (const uint8_t*)FM_PAGE, strlen(FM_PAGE));   // ESP32: flash is memory-mapped
    res->addHeader("Cache-Control", "no-store");
    request->send(res);
  });
  fm_server->on("/api/status", HTTP_GET, [](AsyncWebServerRequest* request) {
    String body;
    if (fm_snap_mutex && xSemaphoreTake(fm_snap_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
      body = fm_snap;
      xSemaphoreGive(fm_snap_mutex);
    } else {
      body = "{}";
    }
    AsyncWebServerResponse* res = request->beginResponse(200, "application/json", body);
    res->addHeader("Cache-Control", "no-store");
    request->send(res);
  });
  fm_server->on("/api/nodes", HTTP_GET, [](AsyncWebServerRequest* request) { fmSendSnap(request, fm_snap_nodes); });
  fm_server->on("/api/network", HTTP_GET, [](AsyncWebServerRequest* request) { fmSendSnap(request, fm_snap_net); });
  fm_server->on("/api/board", HTTP_GET, [](AsyncWebServerRequest* request) { fmSendSnap(request, fm_snap_board); });
  fm_server->on("/api/post", HTTP_POST, [](AsyncWebServerRequest* request) {
    if (!request->hasParam("t", true)) { request->send(400); return; }
    char name[FM_WEB_POST_NAME_MAX + 1], text[FM_WEB_POST_TEXT_MAX + 1];
    fmCleanInput(text, sizeof(text), request->getParam("t", true)->value());
    fmCleanInput(name, sizeof(name), request->hasParam("n", true) ? request->getParam("n", true)->value() : String(""));
    if (text[0] == 0) { request->send(400); return; }
    bool ok = false;
    if (fm_snap_mutex && xSemaphoreTake(fm_snap_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
      if (!fm_post_pending) {
        strcpy(fm_post_name, name);
        strcpy(fm_post_text, text);
        fm_post_pending = true;
        ok = true;
      }
      xSemaphoreGive(fm_snap_mutex);
    }
    request->send(ok ? 202 : 429);   // 429: previous post not taken over yet, try again
  });
  fm_server->on("/api/login", HTTP_POST, [](AsyncWebServerRequest* request) {
    if ((long)(fm_login_block_until - millis()) > 0) { request->send(429); return; }
    String p = fmParam(request, "p");
    bool ok = false;
    char token[33] = "";
    if (xSemaphoreTake(fm_snap_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
      ok = fm_admin_pw[0] != 0 && p.equals(fm_admin_pw);
      if (ok) {
        int slot = 0;
        for (int i = 1; i < FM_WEB_MAX_TOKENS; i++) {   // free or oldest slot
          if ((long)(fm_tokens[i].until - fm_tokens[slot].until) < 0) slot = i;
        }
        for (int i = 0; i < 16; i += 4) {
          uint32_t r = esp_random();
          snprintf(&token[i * 2], 9, "%08lx", (unsigned long)r);
        }
        StrHelper::strncpy(fm_tokens[slot].t, token, sizeof(fm_tokens[slot].t));
        fm_tokens[slot].until = millis() + FM_WEB_TOKEN_TTL_MS;
      } else {
        fm_login_block_until = millis() + FM_WEB_LOGIN_BLOCK_MS;
      }
      xSemaphoreGive(fm_snap_mutex);
    }
    if (!ok) { request->send(403); return; }
    request->send(200, "application/json", String("{\"token\":\"") + token + "\"}");
  });
  fm_server->on("/api/logout", HTTP_POST, [](AsyncWebServerRequest* request) {
    String k = fmParam(request, "k");
    if (xSemaphoreTake(fm_snap_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
      for (int i = 0; i < FM_WEB_MAX_TOKENS; i++) {
        if (fm_tokens[i].t[0] && k.equals(fm_tokens[i].t)) fm_tokens[i].t[0] = 0;
      }
      xSemaphoreGive(fm_snap_mutex);
    }
    request->send(204);
  });
  // setup actions = normal CLI commands, run by loop() like a remote admin CLI
  fm_server->on("/api/admin", HTTP_POST, [](AsyncWebServerRequest* request) {
    if (!fmTokenOk(request)) { request->send(403); return; }
    String c = fmParam(request, "c");
    if (c.length() == 0 || c.length() >= sizeof(fm_admin_cmd)) { request->send(400); return; }
    uint32_t id = 0;
    if (xSemaphoreTake(fm_snap_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
      if (!fm_admin_pending) {
        fmCleanInput(fm_admin_cmd, sizeof(fm_admin_cmd), c);
        id = ++fm_admin_seq;
        fm_admin_pending = true;
      }
      xSemaphoreGive(fm_snap_mutex);
    }
    if (id == 0) { request->send(429); return; }
    request->send(200, "application/json", String("{\"id\":") + id + "}");
  });
  fm_server->on("/api/admin", HTTP_GET, [](AsyncWebServerRequest* request) {
    if (!fmTokenOk(request)) { request->send(403); return; }
    uint32_t id = strtoul(fmParam(request, "id").c_str(), NULL, 10);
    String body = "{\"done\":false}";
    if (xSemaphoreTake(fm_snap_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
      if (id != 0 && id == fm_admin_done_id) {
        char buf[260];
        FmJson j(buf, sizeof(buf));
        j.raw("{\"done\":true,\"reply\":");
        j.str(fm_admin_reply);
        j.raw("}");
        j.done(buf);
        body = buf;
      }
      xSemaphoreGive(fm_snap_mutex);
    }
    AsyncWebServerResponse* res = request->beginResponse(200, "application/json", body);
    res->addHeader("Cache-Control", "no-store");
    request->send(res);
  });
  fm_server->on("/api/ping", HTTP_GET, [](AsyncWebServerRequest* request) {
    AsyncWebServerResponse* res = request->beginResponse(204);
    res->addHeader("Access-Control-Allow-Origin", "*");   // reachability check from the IP address
    res->addHeader("Cache-Control", "no-store");
    request->send(res);
  });
  fm_server->on("/api/time", HTTP_POST, [](AsyncWebServerRequest* request) {
    if (request->hasParam("t", true)) {
      uint32_t t = strtoul(request->getParam("t", true)->value().c_str(), NULL, 10);
      if (t > 0) fm_pending_time = t;   // checked and applied in loop()
    }
    request->send(204);
  });
  // Captive portal: the phones' connectivity checks and every unknown address lead to the page.
  // Apple: /hotspot-detect.html, Android: /generate_204, Windows: /connecttest.txt ...
  fm_server->onNotFound([](AsyncWebServerRequest* request) {
    request->redirect("http://192.168.4.1/");
  });
}

static void fmSnapAlloc(FmSnap& s) {
  if (s.buf == NULL) {
    s.buf = (char*)malloc(s.cap);   // > 4 KB: PSRAM
    if (s.buf) { s.buf[0] = 0; s.len = 0; }
  }
}

// ---- snapshot builders (run in loop(), mesh data only read here) ----

static const char* fmHex3(const uint8_t* k) {
  static char tmp[8];
  snprintf(tmp, sizeof(tmp), "%02X%02X%02X", k[0], k[1], k[2]);
  return tmp;
}

void MyMesh::fmWebBuildNodes(FmJson& j) {
  uint32_t now = getRTCClock()->getCurrentTime();
  j.f("{\"now\":%lu,\"nodes\":[", (unsigned long)now);
  bool first = true;
  for (int i = 0; i < _fm_max_nodes; i++) {
    FmNode& n = _fm_nodes[i];
    if (n.heard_at == 0) continue;
    bool team = fmTeamHas(n.pub_key);
    if (!team && now > n.heard_at + FM_WEB_NODES_MAX_AGE) continue;
    if (j.dp > j.end - 200) break;   // full: rest left out
    if (!first) j.raw(",");
    first = false;
    j.f("{\"k\":\"%02x%02x%02x%02x%02x%02x\",\"n\":", n.pub_key[0], n.pub_key[1], n.pub_key[2], n.pub_key[3], n.pub_key[4], n.pub_key[5]);
    j.str(n.name[0] ? n.name : fmHex3(n.pub_key));
    j.f(",\"y\":%u,\"h\":%u,\"s\":%.1f,\"a\":%lu,\"c\":%u,\"tm\":%d",
        (unsigned)n.type, (unsigned)n.hops, n.snr4 / 4.0f, (unsigned long)n.heard_at, (unsigned)n.adverts, team ? 1 : 0);
    if (n.has_pos) {
      j.f(",\"la\":%.6f,\"lo\":%.6f,\"pa\":%lu", n.lat / 1e6, n.lon / 1e6, (unsigned long)n.pos_at);
    }
    j.raw("}");
  }
  j.raw("]}");
}

void MyMesh::fmWebBuildNetwork(FmJson& j) {
  j.f("{\"now\":%lu,\"act\":[", (unsigned long)getRTCClock()->getCurrentTime());
  for (int k = 1; k <= FM_ACT_BUCKETS; k++) {   // oldest first, current bucket last
    FmActBucket& b = _fm_act[(_fm_act_idx + k) % FM_ACT_BUCKETS];
    j.f("%s[%u,%u]", k > 1 ? "," : "", (unsigned)b.rx, (unsigned)b.tx);
  }
  j.f("],\"duty_ms\":%lu,\"types\":[", (unsigned long)fmDutyMillisLastHour());
  for (int t = 0; t < 16; t++) j.f("%s%lu", t ? "," : "", (unsigned long)_fm_type_counts[t]);
  j.raw("],\"feed\":[");
  bool first = true;
  for (int k = 1; k <= FM_FEED_SIZE; k++) {   // newest first
    FmFeedEntry& e = _fm_feed[(_fm_feed_next - k + FM_FEED_SIZE) % FM_FEED_SIZE];
    if (e.at == 0) continue;
    if (!first) j.raw(",");
    first = false;
    j.f("{\"t\":%lu,\"y\":%u,\"f\":%u,\"h\":%u,\"s\":%.1f,\"n\":", (unsigned long)e.at, (unsigned)e.type,
        (unsigned)e.flood, (unsigned)e.hops, e.snr4 / 4.0f);
    char hex[8] = "";
    const char* name = NULL;
    if (e.has_src == 1) {
      name = fmNodeNameByHash(e.src_hash);
      snprintf(hex, sizeof(hex), "%02X", e.src_hash);
    } else if (e.has_src == 2) {
      FmNode* n = fmNodeFind(e.advert_key, sizeof(e.advert_key));
      if (n && n->name[0]) name = n->name;
      snprintf(hex, sizeof(hex), "%02X%02X", e.advert_key[0], e.advert_key[1]);
    }
    j.str(name ? name : hex);
    j.raw("}");
  }
  j.raw("],\"nb\":[");
  first = true;
#if MAX_NEIGHBOURS
  for (int i = 0; i < MAX_NEIGHBOURS; i++) {
    NeighbourInfo& nb = neighbours[i];
    if (nb.heard_timestamp == 0) continue;
    if (!first) j.raw(",");
    first = false;
    FmNode* n = fmNodeFind(nb.id.pub_key, PUB_KEY_SIZE);
    j.raw("{\"n\":");
    j.str(n && n->name[0] ? n->name : fmHex3(nb.id.pub_key));
    j.f(",\"s\":%.1f,\"a\":%lu}", nb.snr / 4.0f, (unsigned long)nb.heard_timestamp);
  }
#endif
  j.raw("],\"col\":[");   // hash collisions: nodes sharing the 1-byte ID
  int groups = 0;
  for (int i = 0; i < _fm_max_nodes && groups < 5; i++) {
    FmNode& a = _fm_nodes[i];
    if (a.heard_at == 0) continue;
    bool seen_before = false;
    for (int k = 0; k < i; k++) {
      if (_fm_nodes[k].heard_at && _fm_nodes[k].pub_key[0] == a.pub_key[0]) { seen_before = true; break; }
    }
    if (seen_before) continue;
    int count = 0;
    for (int k = i + 1; k < _fm_max_nodes; k++) {
      if (_fm_nodes[k].heard_at && _fm_nodes[k].pub_key[0] == a.pub_key[0]) count++;
    }
    if (count == 0) continue;
    j.f("%s{\"id\":\"%02X\",\"n\":[", groups ? "," : "", a.pub_key[0]);
    j.str(a.name);
    int listed = 1;
    for (int k = i + 1; k < _fm_max_nodes && listed < 4; k++) {
      if (_fm_nodes[k].heard_at && _fm_nodes[k].pub_key[0] == a.pub_key[0]) {
        j.raw(",");
        j.str(_fm_nodes[k].name);
        listed++;
      }
    }
    j.raw("]}");
    groups++;
  }
  j.f("],\"st\":{\"rx\":%lu,\"tx\":%lu,\"rf\":%lu,\"rd\":%lu,\"sf\":%lu,\"sd\":%lu,\"df\":%lu,\"dd\":%lu"
      ",\"err\":%lu,\"rxair\":%lu,\"txair\":%lu,\"snr\":%.1f,\"rssi\":%d}}",
      (unsigned long)radio_driver.getPacketsRecv(), (unsigned long)radio_driver.getPacketsSent(),
      (unsigned long)getNumRecvFlood(), (unsigned long)getNumRecvDirect(),
      (unsigned long)getNumSentFlood(), (unsigned long)getNumSentDirect(),
      (unsigned long)((SimpleMeshTables *)getTables())->getNumFloodDups(), (unsigned long)((SimpleMeshTables *)getTables())->getNumDirectDups(),
      (unsigned long)radio_driver.getPacketsRecvErrors(), (unsigned long)(getReceiveAirTime() / 1000), (unsigned long)(getTotalAirTime() / 1000),
      radio_driver.getLastSNR(), (int)radio_driver.getLastRSSI());
}

void MyMesh::fmWebBuildBoard(FmJson& j) {
  j.f("{\"now\":%lu,\"room\":%s,\"posts\":[", (unsigned long)getRTCClock()->getCurrentTime(), fmRoomActive() ? "true" : "false");
  bool first = true;
  for (int k = 1; k <= FM_ROOM_MAX_POSTS; k++) {   // newest first
    FmRoomPost& p = _room_posts[(_room_next_post_idx - k + FM_ROOM_MAX_POSTS) % FM_ROOM_MAX_POSTS];
    if (p.post_timestamp == 0) continue;
    if (!first) j.raw(",");
    first = false;
    char author[48];
    const char* text = p.text;
    char kind = 'p';   // p = post from the mesh, w = WiFi, s = SOS, y = box (system)
    if (_room_id_loaded && p.author.matches(_room_id)) {
      const char* wifi = strstr(p.text, " (WiFi): ");
      if (memcmp(p.text, "SOS from ", 9) == 0) {
        kind = 's';
        StrHelper::strncpy(author, fmRoomName(), sizeof(author));
      } else if (wifi) {
        kind = 'w';
        int n = wifi - p.text + 7;   // "Name (WiFi)"
        if (n > (int)sizeof(author) - 1) n = sizeof(author) - 1;
        memcpy(author, p.text, n);
        author[n] = 0;
        text = wifi + 9;
      } else {
        kind = 'y';
        StrHelper::strncpy(author, fmRoomName(), sizeof(author));
      }
    } else {
      FmNode* n = fmNodeFind(p.author.pub_key, PUB_KEY_SIZE);
      if (n && n->name[0]) StrHelper::strncpy(author, n->name, sizeof(author));
      else snprintf(author, sizeof(author), "user %s", fmHex3(p.author.pub_key));
    }
    j.f("{\"k\":\"%c\",\"t\":%lu,\"a\":", kind, (unsigned long)p.post_timestamp);
    j.str(author);
    j.raw(",\"x\":");
    j.str(text);
    j.raw("}");
  }
  j.raw("],\"sos\":[");
  first = true;
  for (int k = 1; k <= FM_MAX_SOS; k++) {   // newest first
    FmSosEntry& e = _fm_sos[(_fm_sos_next - k + FM_MAX_SOS) % FM_MAX_SOS];
    if (e.heard_millis == 0) continue;
    if (!first) j.raw(",");
    first = false;
    j.raw("{\"s\":");
    j.str(e.sender);
    j.raw(",\"i\":");
    j.str(e.info);
    j.f(",\"t\":%lu}", (unsigned long)e.heard_at);
  }
  j.raw("]}");
}

bool MyMesh::fmWebStart() {
  if (fm_web_running) return true;
  if (fm_snap_mutex == NULL) fm_snap_mutex = xSemaphoreCreateMutex();
  fmSnapAlloc(fm_snap_nodes);
  fmSnapAlloc(fm_snap_net);
  fmSnapAlloc(fm_snap_board);

  fmWebSsid(fm_web_ssid, sizeof(fm_web_ssid));
  WiFi.mode(WIFI_AP);
  if (!WiFi.softAP(fm_web_ssid, NULL, 1, 0, FM_WEB_MAX_STATIONS)) {   // open WiFi, channel 1
    WiFi.mode(WIFI_OFF);
    return false;
  }
  if (fm_dns == NULL) fm_dns = new DNSServer();
  fm_dns->setErrorReplyCode(DNSReplyCode::NoError);
  fm_dns->start(53, "*", WiFi.softAPIP());   // every name -> the box

  if (fm_server == NULL) {
    fm_server = new AsyncWebServer(80);
    fmWebSetupRoutes();
  }
  fm_server->begin();
  // .local name via mDNS: phones resolve it inside the WiFi, also when they keep mobile
  // data on ("use without internet"), where normal names would go to the mobile network
  fmWebHostName(fm_web_host, sizeof(fm_web_host), fm_web_ssid);
  if (MDNS.begin(fm_web_host)) {
    MDNS.addService("http", "tcp", 80);
  }
  fm_web_running = true;
  fm_next_snapshot = 0;
  return true;
}

void MyMesh::fmWebStop() {
  if (!fm_web_running) return;
  MDNS.end();
  fm_server->end();
  fm_dns->stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  fm_web_running = false;
}

// mDNS host name from the public name: lower case a-z 0-9, umlauts spelled out, everything
// else becomes '-' ("Base Camp Süd" -> "base-camp-sued"); max 63 characters
void MyMesh::fmWebHostName(char* dest, size_t sz, const char* name) {
  size_t n = 0;
  bool dash = false;
  for (const unsigned char* p = (const unsigned char*)name; *p && n + 3 < sz && n < 63; p++) {
    unsigned char c = *p;
    const char* rep = NULL;
    if (c == 0xC3 && p[1]) {   // UTF-8 umlauts
      switch (p[1]) {
        case 0xA4: case 0x84: rep = "ae"; break;
        case 0xB6: case 0x96: rep = "oe"; break;
        case 0xBC: case 0x9C: rep = "ue"; break;
        case 0x9F: rep = "ss"; break;
      }
      if (rep) p++;
    }
    if (rep) {
      dest[n++] = rep[0];
      dest[n++] = rep[1];
      dash = false;
    } else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
      dest[n++] = c;
      dash = false;
    } else if (c >= 'A' && c <= 'Z') {
      dest[n++] = c - 'A' + 'a';
      dash = false;
    } else if (n > 0 && !dash) {
      dest[n++] = '-';
      dash = true;
    }
  }
  while (n > 0 && dest[n - 1] == '-') n--;
  dest[n] = 0;
  if (n == 0) StrHelper::strncpy(dest, FM_WEB_MDNS_FALLBACK, sz);
}

const char* MyMesh::fmWebCurrentHost() const {
  return fm_web_host;
}

bool MyMesh::fmWebIsOn() const {
  return fm_web_running;
}

int MyMesh::fmWebStations() const {
  return fm_web_running ? WiFi.softAPgetStationNum() : 0;
}

const char* MyMesh::fmWebCurrentSsid() const {
  return fm_web_ssid;
}

void MyMesh::fmWebSsid(char* dest, size_t sz) {
  // the public name: the room's when it runs, else the repeater's
  StrHelper::strncpy(dest, fmRoomActive() ? fmRoomName() : _prefs.node_name, sz);
}

void MyMesh::fmWebLoop() {
  if (!fm_web_running) return;
  fm_dns->processNextRequest();

  // phone clock from the page: only forward (upstream rule), only plausible steps
  uint32_t t = fm_pending_time;
  if (t) {
    fm_pending_time = 0;
    uint32_t now = getRTCClock()->getCurrentTime();
    if (t > now + FM_WEB_CLOCK_MIN_STEP && t - now < FM_WEB_CLOCK_MAX_STEP) {
      getRTCClock()->setCurrentTime(t);
    }
  }

  // post from the WiFi page -> room board, as "Name (WiFi): text" by the room itself
  if (xSemaphoreTake(fm_snap_mutex, 0) == pdTRUE) {
    if (fm_post_pending) {
      if (fmRoomActive()) {
        char msg[FM_ROOM_POST_TEXT_LEN + 1];
        snprintf(msg, sizeof(msg), "%s (WiFi): %s", fm_post_name[0] ? fm_post_name : "Guest", fm_post_text);
        fmRoomStorePost(_room_id, msg);
        fm_next_snapshot = 0;   // show it right away
      }
      fm_post_pending = false;
    }
    xSemaphoreGive(fm_snap_mutex);
  }

  // admin command from the setup page: run as remote CLI (serial-only commands stay blocked)
  if (xSemaphoreTake(fm_snap_mutex, 0) == pdTRUE) {
    bool run = fm_admin_pending;
    char cmd[sizeof(fm_admin_cmd)];
    uint32_t id = fm_admin_seq;
    if (run) strcpy(cmd, fm_admin_cmd);
    xSemaphoreGive(fm_snap_mutex);
    if (run) {
      char reply[200];
      reply[0] = 0;
      uint32_t now = getRTCClock()->getCurrentTime();
      handleCommand(now ? now : 1, cmd, reply);   // may stop the WiFi (set wifi off)
      if (fm_snap_mutex && xSemaphoreTake(fm_snap_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        StrHelper::strncpy(fm_admin_reply, reply[0] ? reply : "OK", sizeof(fm_admin_reply));
        fm_admin_done_id = id;
        fm_admin_pending = false;
        xSemaphoreGive(fm_snap_mutex);
      }
      fm_next_snapshot = 0;   // show the change right away
      if (!fm_web_running) return;
    }
  }

  if (fm_next_snapshot && !millisHasNowPassed(fm_next_snapshot)) return;
  fm_next_snapshot = futureMillis(FM_WEB_SNAPSHOT_INTERVAL);

  if (xSemaphoreTake(fm_snap_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
    StrHelper::strncpy(fm_admin_pw, _prefs.password, sizeof(fm_admin_pw));   // setup login = repeater admin password
    xSemaphoreGive(fm_snap_mutex);
  }

  bool viewers = fmWebStations() > 0;
  if (viewers && fm_snap_board.buf && xSemaphoreTake(fm_snap_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
    FmJson j(fm_snap_board.buf, fm_snap_board.cap);
    fmWebBuildBoard(j);
    fm_snap_board.len = j.done(fm_snap_board.buf);
    xSemaphoreGive(fm_snap_mutex);
  }
  if (viewers && (fm_next_big_snapshot == 0 || millisHasNowPassed(fm_next_big_snapshot))) {
    fm_next_big_snapshot = futureMillis(FM_WEB_BIG_SNAPSHOT_INTERVAL);
    if (fm_snap_nodes.buf && xSemaphoreTake(fm_snap_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
      FmJson j(fm_snap_nodes.buf, fm_snap_nodes.cap);
      fmWebBuildNodes(j);
      fm_snap_nodes.len = j.done(fm_snap_nodes.buf);
      xSemaphoreGive(fm_snap_mutex);
    }
    if (fm_snap_net.buf && xSemaphoreTake(fm_snap_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
      FmJson j(fm_snap_net.buf, fm_snap_net.cap);
      fmWebBuildNetwork(j);
      fm_snap_net.len = j.done(fm_snap_net.buf);
      xSemaphoreGive(fm_snap_mutex);
    }
  }

  // public name changed (room renamed / switched, repeater renamed): restart the access point
  char want[33];
  fmWebSsid(want, sizeof(want));
  if (strcmp(want, fm_web_ssid) != 0) {
    fmWebStop();
    fmWebStart();
    return;
  }

  char buf[sizeof(fm_snap)];
  char* dp = buf;
  const char* end = buf + sizeof(buf) - 1;
  dp += snprintf(dp, end - dp, "{\"name\":");
  fmJsonStr(dp, end, fm_web_ssid);
  dp += snprintf(dp, end - dp, ",\"host\":\"%s.local\",\"repeater\":", fm_web_host);
  fmJsonStr(dp, end, _prefs.node_name);
  dp += snprintf(dp, end - dp,
    ",\"offgrid\":%s,\"freq\":%.4f,\"sf\":%u,\"bw\":%.1f,\"cr\":%u"
    ",\"room\":%s,\"posts\":%d,\"users\":%d,\"stations\":%d"
    ",\"uptime\":%lu,\"time\":%lu,\"noise\":%d,\"rx\":%lu,\"tx\":%lu,\"airtime\":%lu",
    _fm.offgrid ? "true" : "false", _prefs.freq, (unsigned)_prefs.sf, _prefs.bw, (unsigned)_prefs.cr,
    fmRoomActive() ? "true" : "false", fmRoomNumPosts(), _room_acl.getNumClients(), fmWebStations(),
    (unsigned long)(uptime_millis / 1000), (unsigned long)getRTCClock()->getCurrentTime(),
    (int)_radio->getNoiseFloor(), (unsigned long)radio_driver.getPacketsRecv(),
    (unsigned long)radio_driver.getPacketsSent(), (unsigned long)(getTotalAirTime() / 1000));
  {
    // box position (distances/radar on the page), sensor, duty cycle, team size, start reason
    double lat = _prefs.advert_loc_policy == ADVERT_LOC_SHARE ? sensors.node_lat : _prefs.node_lat;
    double lon = _prefs.advert_loc_policy == ADVERT_LOC_SHARE ? sensors.node_lon : _prefs.node_lon;
    if (lat != 0.0 || lon != 0.0) dp += snprintf(dp, end - dp, ",\"lat\":%.6f,\"lon\":%.6f", lat, lon);
    if (_fm_env_flags & 1) dp += snprintf(dp, end - dp, ",\"temp\":%.1f", _fm_env_temp);
    if (_fm_env_flags & 2) dp += snprintf(dp, end - dp, ",\"hum\":%.0f", _fm_env_hum);
    if (_fm_env_flags & 4) dp += snprintf(dp, end - dp, ",\"mcu\":%.0f", _fm_env_temp);
    dp += snprintf(dp, end - dp, ",\"duty_ms\":%lu,\"team\":%d,\"boot\":", (unsigned long)fmDutyMillisLastHour(), _fm_num_team);
    fmJsonStr(dp, end, _fm_boot_reason ? _fm_boot_reason : "");
    if (dp < end) *dp++ = '}';
  }
  *dp = 0;

  if (xSemaphoreTake(fm_snap_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
    memcpy(fm_snap, buf, dp - buf + 1);
    xSemaphoreGive(fm_snap_mutex);
  }
}

void MyMesh::fmSetWifi(bool on, char* reply) {
  char tmp[64];
  if (on) {
    if (fmWebStart()) {
      snprintf(tmp, sizeof(tmp), "OK - WiFi '%s' on, http://%s.local", fm_web_ssid, fm_web_host);
    } else {
      strcpy(tmp, "Err - WiFi could not start");
    }
  } else {
    fmWebStop();
    strcpy(tmp, "OK - WiFi off");
  }
  _fm.wifi_enabled = on && fm_web_running ? 1 : 0;
  fmSavePrefs();
  if (reply) strcpy(reply, tmp);
}

#elif defined(FIELDMESH_REPEATER)
// not ESP32: no WiFi dashboard
bool MyMesh::fmWebStart() { return false; }
void MyMesh::fmWebStop() { }
bool MyMesh::fmWebIsOn() const { return false; }
int MyMesh::fmWebStations() const { return 0; }
const char* MyMesh::fmWebCurrentSsid() const { return ""; }
const char* MyMesh::fmWebCurrentHost() const { return ""; }
void MyMesh::fmWebHostName(char* dest, size_t sz, const char* name) { if (sz) dest[0] = 0; }
void MyMesh::fmWebSsid(char* dest, size_t sz) { if (sz) dest[0] = 0; }
void MyMesh::fmWebLoop() { }
void MyMesh::fmSetWifi(bool on, char* reply) { if (reply) strcpy(reply, "Err - no WiFi on this board"); }
#endif
