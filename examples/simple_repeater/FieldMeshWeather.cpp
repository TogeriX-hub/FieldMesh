// FieldMesh repeater: weather for '!wetter' (P5f). Only compiled in with -D FIELDMESH_REPEATER=1.
//
// The box has no internet. A phone in the dashboard WiFi with mobile data fetches the forecast
// for the box position from Open-Meteo (every 3 h, in the background) and posts it to
// /api/weather (FieldMeshWeb.cpp). '!wetter' in the room builds one short German message from
// the stored hours with fixed rules (FieldMeshWeatherText.h, no AI on the box).

#include "MyMesh.h"

#ifdef FIELDMESH_REPEATER

#include "FieldMeshFs.h"

bool MyMesh::fmWxLoc(float& lat, float& lon) {
  if (_fm.wx_own) {
    lat = _fm.wx_lat;
    lon = _fm.wx_lon;
  } else {   // box position, as in the adverts / dashboard
    lat = _prefs.advert_loc_policy == ADVERT_LOC_SHARE ? sensors.node_lat : _prefs.node_lat;
    lon = _prefs.advert_loc_policy == ADVERT_LOC_SHARE ? sensors.node_lon : _prefs.node_lon;
  }
  return lat != 0.0f || lon != 0.0f;
}

bool MyMesh::fmWxValid() {
  float lat, lon;
  if (_fm_wx.n == 0 || !fmWxLoc(lat, lon)) return false;
  return fabsf(lat - _fm_wx.lat) < FM_WX_SAME_LOC && fabsf(lon - _fm_wx.lon) < FM_WX_SAME_LOC;
}

void MyMesh::fmWxText(char* out, size_t sz, bool en) {
  out[0] = 0;
  float lat, lon;
  if (!fmWxLoc(lat, lon)) {
    StrHelper::strncpy(out, fmWxMsgNoPos(en), sz);
    return;
  }
  if (!fmWxValid()) {
    StrHelper::strncpy(out, fmWxMsgNoData(en), sz);
    return;
  }
  fmWxBuildText(_fm_wx, getRTCClock()->getCurrentTime(), out, sz, en);
}

// "1|start|utc_off|lat|lon|t,pp,pr10,gust,code,day;t,pp,..." (built by the dashboard page)
bool MyMesh::fmWxStore(const char* d) {
  char* p;
  if (strtol(d, &p, 10) != 1 || *p != '|') return false;
  FmWx w;
  memset(&w, 0, sizeof(w));
  w.start = strtoul(p + 1, &p, 10);
  if (*p != '|') return false;
  w.utc_off = strtol(p + 1, &p, 10);
  if (*p != '|') return false;
  w.lat = strtof(p + 1, &p);
  if (*p != '|') return false;
  w.lon = strtof(p + 1, &p);
  if (*p != '|') return false;
  p++;
  if (w.start < 1700000000UL || w.utc_off < -14 * 3600 || w.utc_off > 14 * 3600) return false;

  float lat, lon;   // only data for our location
  if (!fmWxLoc(lat, lon) || fabsf(lat - w.lat) >= FM_WX_SAME_LOC || fabsf(lon - w.lon) >= FM_WX_SAME_LOC) return false;

  while (*p && w.n < FM_WX_HOURS) {
    long v[6];
    for (int k = 0; k < 6; k++) {
      v[k] = strtol(p, &p, 10);
      if (k < 5) { if (*p != ',') return false; p++; }
    }
    FmWxHour& h = w.h[w.n++];
    h.t = (int8_t)constrain(v[0], -60, 60);
    h.pp = (uint8_t)constrain(v[1], 0, 100);
    h.pr10 = (uint8_t)constrain(v[2], 0, 255);
    h.gust = (uint8_t)constrain(v[3], 0, 255);
    h.code = (uint8_t)constrain(v[4], 0, 99);
    h.day = v[5] ? 1 : 0;
    if (*p == ';') p++;
    else if (*p) return false;
  }
  if (w.n < 6) return false;
  w.fetched_at = getRTCClock()->getCurrentTime();
  _fm_wx = w;
  fmWxSave();
  return true;
}

void MyMesh::fmWxLoad() {
  memset(&_fm_wx, 0, sizeof(_fm_wx));
  if (!fmFsExists(_fs, FM_WX_FILE)) return;
  File f = fmFsOpenRead(_fs, FM_WX_FILE);
  if (!f) return;
  uint8_t hdr[5] = { 0 };
  uint16_t size = sizeof(FmWx);
  if (f.read(hdr, 5) == 5 && hdr[0] == 'F' && hdr[1] == 'W' && hdr[2] == 1 && memcmp(&hdr[3], &size, 2) == 0) {
    if (f.read((uint8_t*)&_fm_wx, sizeof(FmWx)) != sizeof(FmWx)) memset(&_fm_wx, 0, sizeof(_fm_wx));
  }
  f.close();
  if (_fm_wx.n > FM_WX_HOURS) memset(&_fm_wx, 0, sizeof(_fm_wx));
}

void MyMesh::fmWxSave() {
  File f = fmFsOpenWriteTmp(_fs, FM_WX_FILE);
  if (!f) return;
  uint16_t size = sizeof(FmWx);
  uint8_t hdr[5] = { 'F', 'W', 1, 0, 0 };
  memcpy(&hdr[3], &size, 2);
  bool ok = f.write(hdr, 5) == 5 && f.write((uint8_t*)&_fm_wx, sizeof(FmWx)) == sizeof(FmWx);
  f.close();
  if (ok) fmFsCommit(_fs, FM_WX_FILE);
}

bool MyMesh::fmWxHandleCommand(const char* command, char* reply) {
  if (memcmp(command, "set weather.loc", 15) == 0 && (command[15] == 0 || command[15] == ' ')) {
    const char* a = &command[15];
    while (*a == ' ') a++;
    if (strcmp(a, "box") == 0) {
      _fm.wx_own = 0;
      fmSavePrefs();
      strcpy(reply, "OK - weather for the box position");
      return true;
    }
    char* p;
    float lat = strtof(a, &p);
    while (*p == ' ' || *p == ',') p++;
    const char* lon_start = p;
    float lon = strtof(lon_start, &p);
    if (p == lon_start || p == a || fabsf(lat) > 90 || fabsf(lon) > 180 || (lat == 0 && lon == 0)) {
      strcpy(reply, "Err - use: set weather.loc <lat>,<lon>  or  set weather.loc box");
      return true;
    }
    _fm.wx_own = 1;
    _fm.wx_lat = lat;
    _fm.wx_lon = lon;
    fmSavePrefs();
    sprintf(reply, "OK - weather for %.5f,%.5f (new data with the next phone online)", lat, lon);
    return true;
  }
  if (strcmp(command, "get weather") == 0) {
    float lat, lon;
    if (!fmWxLoc(lat, lon)) {
      strcpy(reply, "> no location (set the box position or: set weather.loc <lat>,<lon>)");
    } else if (!fmWxValid()) {
      sprintf(reply, "> %s %.5f,%.5f, no data yet", _fm.wx_own ? "own" : "box", lat, lon);
    } else {
      uint32_t now = getRTCClock()->getCurrentTime();
      unsigned long age = now > _fm_wx.fetched_at ? (now - _fm_wx.fetched_at) / 60 : 0;
      sprintf(reply, "> %s %.5f,%.5f, data %lu min old, %u h", _fm.wx_own ? "own" : "box", lat, lon, age, (unsigned)_fm_wx.n);
    }
    return true;
  }
  return false;
}

#endif
