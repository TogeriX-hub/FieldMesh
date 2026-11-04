// FieldMesh repeater: sun times for '!sonne' / '!sun' in the room and the time zone of the
// box. Only compiled in with -D FIELDMESH_REPEATER=1.
//
// The G2 has no GPS and no internet: the clock and the time zone come from the phones that
// open the dashboard (FieldMeshWeb.cpp). The whole POSIX rule is kept (FieldMeshTimeZone.h),
// so local time stays right across a daylight saving change without a phone. Every phone
// may set it (decision 04.11.2026); the setup page and 'set tz' are the way without a phone.

#include "MyMesh.h"

#ifdef FIELDMESH_REPEATER

bool MyMesh::fmTzGet(FmTz& tz) {
  return _fm.tz[0] && fmTzParse(_fm.tz, tz);
}

bool MyMesh::fmTzStore(const char* rule) {
  FmTz tz;
  if (!fmTzParse(rule, tz)) return false;
  if (strcmp(rule, _fm.tz) != 0) {   // flash write only on a real change
    StrHelper::strncpy(_fm.tz, rule, sizeof(_fm.tz));
    fmSavePrefs();
  }
  return true;
}

// "UTC+1", "UTC-3:30", "UTC"
static void fmTzOffsetText(char* out, size_t sz, int32_t off) {
  if (off == 0) { snprintf(out, sz, "UTC"); return; }
  const char sign = off < 0 ? '-' : '+';
  if (off < 0) off = -off;
  const int h = off / 3600, m = (off % 3600) / 60;
  if (m) snprintf(out, sz, "UTC%c%d:%02d", sign, h, m);
  else   snprintf(out, sz, "UTC%c%d", sign, h);
}

void MyMesh::fmSunAnswer(char* out, size_t sz, bool en, bool tomorrow) {
  // box position, as in the adverts / dashboard (not the weather's own place)
  const float lat = _prefs.advert_loc_policy == ADVERT_LOC_SHARE ? sensors.node_lat : _prefs.node_lat;
  const float lon = _prefs.advert_loc_policy == ADVERT_LOC_SHARE ? sensors.node_lon : _prefs.node_lon;
  FmTz tz;
  const bool has_tz = fmTzGet(tz);
  fmSunText(out, sz, en, tomorrow, lat != 0.0f || lon != 0.0f, lat, lon,
            getRTCClock()->getCurrentTime(), has_tz ? &tz : NULL);
}

bool MyMesh::fmSunHandleCommand(const char* command, char* reply) {
  if (memcmp(command, "set tz", 6) == 0 && (command[6] == 0 || command[6] == ' ')) {
    const char* a = &command[6];
    while (*a == ' ') a++;
    if (strcmp(a, "none") == 0) {
      if (_fm.tz[0]) {
        _fm.tz[0] = 0;
        fmSavePrefs();
      }
      strcpy(reply, "OK - no time zone, sun times in UTC (the next phone in the dashboard sets it again)");
      return true;
    }
    if (!fmTzStore(a)) {
      strcpy(reply, "Err - use: set tz <POSIX rule>, e.g. set tz CET-1CEST,M3.5.0,M10.5.0/3  or  set tz none");
      return true;
    }
    FmTz tz;
    fmTzGet(tz);
    char off[16];
    fmTzOffsetText(off, sizeof(off), fmTzOffset(tz, getRTCClock()->getCurrentTime()));
    snprintf(reply, 160, "OK - time zone %s (now %s)", _fm.tz, off);
    return true;
  }
  if (strcmp(command, "get tz") == 0) {
    FmTz tz;
    if (!fmTzGet(tz)) {
      strcpy(reply, "> not set, sun times in UTC (phones in the dashboard set it)");
    } else {
      char off[16];
      fmTzOffsetText(off, sizeof(off), fmTzOffset(tz, getRTCClock()->getCurrentTime()));
      snprintf(reply, 160, "> %s, now %s", _fm.tz, off);
    }
    return true;
  }
  return false;
}

#endif
