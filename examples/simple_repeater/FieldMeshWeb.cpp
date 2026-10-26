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
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>

#define FM_WEB_SNAPSHOT_INTERVAL  2000
#define FM_WEB_MAX_STATIONS       10              // phones at the same time (ESP32-S3 allows up to 15)
#define FM_WEB_CLOCK_MIN_STEP     30              // s: smaller differences are ignored
#define FM_WEB_CLOCK_MAX_STEP     (365UL * 86400) // s: a phone further ahead is not trusted

static AsyncWebServer* fm_server = NULL;
static DNSServer* fm_dns = NULL;
static SemaphoreHandle_t fm_snap_mutex = NULL;
static char fm_snap[1024] = "{}";
static volatile uint32_t fm_pending_time = 0;   // phone clock, handed over to loop()
static bool fm_web_running = false;
static char fm_web_ssid[33] = "";
static unsigned long fm_next_snapshot = 0;

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
<p class="note">Dashboard preview (P5a). Board, SOS, Mesh and Setup follow.</p>
</div><script>
var last=0,fails=0;
function set(id,v){var e=document.getElementById(id);if(e.textContent!==v)e.textContent=v;}
function dur(s){var d=Math.floor(s/86400),h=Math.floor(s/3600)%24,m=Math.floor(s/60)%60;return (d?d+' d ':'')+h+' h '+m+' min';}
function hm(t){var d=new Date(t*1000);return ('0'+d.getHours()).slice(-2)+':'+('0'+d.getMinutes()).slice(-2);}
function draw(j){set('name',j.name);set('mode',j.offgrid?'OFF-GRID':'NORMAL');
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

bool MyMesh::fmWebStart() {
  if (fm_web_running) return true;
  if (fm_snap_mutex == NULL) fm_snap_mutex = xSemaphoreCreateMutex();

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
  fm_web_running = true;
  fm_next_snapshot = 0;
  return true;
}

void MyMesh::fmWebStop() {
  if (!fm_web_running) return;
  fm_server->end();
  fm_dns->stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  fm_web_running = false;
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

  if (fm_next_snapshot && !millisHasNowPassed(fm_next_snapshot)) return;
  fm_next_snapshot = futureMillis(FM_WEB_SNAPSHOT_INTERVAL);

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
  dp += snprintf(dp, end - dp, ",\"repeater\":");
  fmJsonStr(dp, end, _prefs.node_name);
  dp += snprintf(dp, end - dp,
    ",\"offgrid\":%s,\"freq\":%.4f,\"sf\":%u,\"bw\":%.1f,\"cr\":%u"
    ",\"room\":%s,\"posts\":%d,\"users\":%d,\"stations\":%d"
    ",\"uptime\":%lu,\"time\":%lu,\"noise\":%d,\"rx\":%lu,\"tx\":%lu,\"airtime\":%lu}",
    _fm.offgrid ? "true" : "false", _prefs.freq, (unsigned)_prefs.sf, _prefs.bw, (unsigned)_prefs.cr,
    fmRoomActive() ? "true" : "false", fmRoomNumPosts(), _room_acl.getNumClients(), fmWebStations(),
    (unsigned long)(uptime_millis / 1000), (unsigned long)getRTCClock()->getCurrentTime(),
    (int)_radio->getNoiseFloor(), (unsigned long)radio_driver.getPacketsRecv(),
    (unsigned long)radio_driver.getPacketsSent(), (unsigned long)(getTotalAirTime() / 1000));
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
      snprintf(tmp, sizeof(tmp), "OK - WiFi '%s' on, http://192.168.4.1", fm_web_ssid);
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
void MyMesh::fmWebSsid(char* dest, size_t sz) { if (sz) dest[0] = 0; }
void MyMesh::fmWebLoop() { }
void MyMesh::fmSetWifi(bool on, char* reply) { if (reply) strcpy(reply, "Err - no WiFi on this board"); }
#endif
