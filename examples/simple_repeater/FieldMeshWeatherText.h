#pragma once
// FieldMesh repeater: weather data and the rules that turn it into the '!wetter' text (German).
// Plain C++ without Arduino/MeshCore, so the rules can be tested on a PC.
//
// Thresholds follow common practice:
//   precipitation hour: >= 0.2 mm/h (just above the 0.1 mm measuring limit) or >= 50 %,
//   "möglich" from 30 %; gusts mentioned above 50 km/h (DWD warning level 1), "Sturmböen" from
//   65 km/h (level 2); "Starkregen" from 15 mm/h (DWD level 2); frost below 0 degC; "heiß" from 30 degC

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define FM_WX_HOURS             48
#define FM_WX_REFRESH_S         10800                // phones fetch new data when older than 3 h

struct FmWxHour {
  int8_t  t;        // temperature, degC (rounded)
  uint8_t pp;       // precipitation probability, %
  uint8_t pr10;     // precipitation, mm x 10 (max 25.5 mm)
  uint8_t gust;     // wind gusts, km/h
  uint8_t code;     // WMO weather code
  uint8_t day;      // 1 = daylight
};
struct FmWx {
  uint32_t start;       // unix time of the first hour
  int32_t  utc_off;     // local time offset of the place (s), for "ab 17 Uhr"
  float    lat, lon;    // place the data is for
  uint32_t fetched_at;  // our clock
  uint8_t  n;           // hours stored (0 = no data)
  FmWxHour h[FM_WX_HOURS];
};

#define FM_WX_SAME_LOC      0.02f   // degrees: data still belongs to the location

static inline void fmWxCopy(char* dest, const char* src, size_t sz) {
  if (sz == 0) return;
  size_t n = strlen(src);
  if (n >= sz) n = sz - 1;
  memcpy(dest, src, n);
  dest[n] = 0;
}

#define FM_WX_WINDOW        12      // hours looked ahead for rain, thunder and wind
#define FM_WX_WET_PR10      2       // 0.2 mm/h
#define FM_WX_WET_PP        50      // %
#define FM_WX_MAYBE_PP      30      // %
#define FM_WX_HEAVY_PR10    150     // 15 mm/h
#define FM_WX_GUST          50      // km/h, mention above
#define FM_WX_STORM         65      // km/h
#define FM_WX_HOT           30      // degC
#define FM_WX_OLD_S         21600   // say how old the data is after 6 h

// Two languages: '!wetter' answers in German, '!weather' in English (international UI).
static inline const char* fmWxWord(uint8_t c, bool day, bool en) {
  switch (c) {
    case 0: return en ? (day ? "sunny" : "clear") : (day ? "sonnig" : "klar");
    case 1: return en ? (day ? "mostly sunny" : "mostly clear") : (day ? "meist sonnig" : "meist klar");
    case 2: return en ? "partly cloudy" : "teils bewölkt";
    case 3: return en ? "overcast" : "bedeckt";
    case 45: case 48: return en ? "fog" : "Nebel";
    case 51: case 53: case 55: return en ? "drizzle" : "Nieselregen";
    case 56: case 57: return en ? "freezing drizzle" : "gefrierender Niesel";
    case 61: return en ? "light rain" : "leichter Regen";
    case 63: return en ? "rain" : "Regen";
    case 65: return en ? "heavy rain" : "starker Regen";
    case 66: case 67: return en ? "freezing rain" : "gefrierender Regen";
    case 71: return en ? "light snow" : "leichter Schneefall";
    case 73: return en ? "snow" : "Schneefall";
    case 75: return en ? "heavy snow" : "starker Schneefall";
    case 77: return en ? "snow grains" : "Schneegriesel";
    case 80: return en ? "light showers" : "leichte Schauer";
    case 81: return en ? "showers" : "Schauer";
    case 82: return en ? "heavy showers" : "heftige Schauer";
    case 85: case 86: return en ? "snow showers" : "Schneeschauer";
    case 95: return en ? "thunderstorm" : "Gewitter";
    case 96: case 99: return en ? "thunderstorm with hail" : "Gewitter mit Hagel";
  }
  return en ? "changeable" : "wechselhaft";
}

// what falls, for "Regen ab 17 Uhr" / "Rain from 17:00"
static inline const char* fmWxKind(const FmWxHour& h, bool shower, bool en) {
  uint8_t c = h.code;
  bool snow = (c >= 71 && c <= 77) || c == 85 || c == 86 || (h.t <= 0 && !(c >= 51 && c <= 57) && c != 66 && c != 67);
  if (snow) return shower ? (en ? "Snow showers" : "Schneeschauer") : (en ? "Snow" : "Schnee");
  if (c == 56 || c == 57 || c == 66 || c == 67) return en ? "Freezing rain" : "Glatteisregen";
  if (c >= 51 && c <= 55) return en ? "Drizzle" : "Niesel";
  if (h.pr10 >= FM_WX_HEAVY_PR10) return en ? "Heavy rain" : "Starkregen";
  if (c >= 80 && c <= 82) return en ? "Showers" : "Schauer";
  return shower ? (en ? "Showers" : "Schauer") : (en ? "Rain" : "Regen");
}

static inline bool fmWxWet(const FmWxHour& h) { return h.pr10 >= FM_WX_WET_PR10 || h.pp >= FM_WX_WET_PP; }

static inline int fmWxHour(const FmWx& w, int i) {   // local hour of day of entry i
  int64_t t = (int64_t)w.start + (int64_t)i * 3600 + w.utc_off;
  return (int)((t / 3600) % 24);
}
static inline int fmWxDay(const FmWx& w, int i) {    // local day number of entry i
  int64_t t = (int64_t)w.start + (int64_t)i * 3600 + w.utc_off;
  return (int)(t / 86400);
}

// "17 Uhr" / "17:00", or "Mitternacht" / "midnight" for 0
static inline const char* fmWxAt(char* buf, size_t sz, int hour, bool en) {
  if (hour == 0) fmWxCopy(buf, en ? "midnight" : "Mitternacht", sz);
  else if (en) snprintf(buf, sz, "%02d:00", hour);
  else snprintf(buf, sz, "%d Uhr", hour);
  return buf;
}

// appends text only if it still fits (one mesh message), so the important parts come first
static inline void fmWxAdd(char* out, size_t sz, const char* part) {
  size_t a = strlen(out), b = strlen(part);
  if (a + b + 1 <= sz) memcpy(out + a, part, b + 1);
}

// messages without forecast text
static inline const char* fmWxMsgNoPos(bool en) {
  return en ? "Weather: the box has no position. Admin: set it in the dashboard (Setup)"
            : "Wetter: Box hat keine Position. Admin: im Dashboard unter Setup setzen";
}
static inline const char* fmWxMsgNoData(bool en) {
  return en ? "Weather: no data yet. A phone with internet in the box WiFi loads it automatically"
            : "Wetter: noch keine Daten. Ein Handy mit Internet im Box-WLAN lädt sie automatisch";
}

// out: one mesh message (max sz-1 bytes); w must hold data for the current location.
// imperial (English only): degF and mph for US users; rules and thresholds stay metric.
static inline void fmWxBuildText(const FmWx& w, uint32_t now, char* out, size_t sz, bool en, bool imperial = false) {
  out[0] = 0;
  bool imp = en && imperial;
  auto T = [imp](int c) { int n = c * 9 + 160; return imp ? (n >= 0 ? n + 2 : n - 2) / 5 : c; };   // degC -> degF (c*9/5+32), rounded
  auto V = [imp](int kmh) { return imp ? (kmh * 621 + 500) / 1000 : kmh; };               // km/h -> mph
  int idx = now > w.start ? (int)((now - w.start) / 3600) : 0;
  if (idx >= w.n - 1) {
    fmWxCopy(out, en ? "Weather: data too old. A phone with internet in the box WiFi loads new data"
                     : "Wetter: Daten zu alt. Ein Handy mit Internet im Box-WLAN lädt neue", sz);
    return;
  }
  int end = idx + FM_WX_WINDOW;
  if (end > w.n) end = w.n;
  const FmWxHour& h0 = w.h[idx];
  const char* deg = imp ? "°F" : (en ? "°C" : "°");
  char part[96], at[16];

  // 1. now
  snprintf(part, sizeof(part), en ? "Weather: %d%s, %s." : "Wetter: %d%s, %s.", T(h0.t), deg, fmWxWord(h0.code, h0.day, en));
  fmWxAdd(out, sz, part);

  // 2. one event in the next hours: thunder > rain/snow > maybe > dry
  int thunder = -1, wet = -1, maybe = -1;
  for (int i = idx + 1; i < end; i++) {
    if (thunder < 0 && w.h[i].code >= 95) thunder = i;
    if (wet < 0 && fmWxWet(w.h[i])) wet = i;
    if (maybe < 0 && w.h[i].pp >= FM_WX_MAYBE_PP) maybe = i;
  }
  part[0] = 0;
  if (fmWxWet(h0) || h0.code >= 95) {   // already wet: when does it stop?
    int dry = -1;
    for (int i = idx + 1; i < end; i++) {
      if (!fmWxWet(w.h[i]) && w.h[i].code < 95) { dry = i; break; }
    }
    if (dry >= 0) {
      snprintf(part, sizeof(part), en ? " Dry from about %s." : " Trocken ab ca. %s.", fmWxAt(at, sizeof(at), fmWxHour(w, dry), en));
    } else {
      snprintf(part, sizeof(part), en ? " %s until at least %s." : " %s hält bis mind. %s an.", fmWxKind(h0, false, en),
               fmWxAt(at, sizeof(at), (fmWxHour(w, end - 1) + 1) % 24, en));
    }
  } else if (thunder >= 0 && (wet < 0 || thunder <= wet)) {
    snprintf(part, sizeof(part), en ? " Thunderstorms possible from %s (%u %%)." : " Ab %s Gewitter möglich (%u %%).",
             fmWxAt(at, sizeof(at), fmWxHour(w, thunder), en), (unsigned)w.h[thunder].pp);
  } else if (wet >= 0) {
    // showers: wet hours with dry gaps in between
    int last = wet, wet_hours = 0;
    uint8_t pp = 0;
    for (int i = wet; i < end; i++) {
      if (fmWxWet(w.h[i])) { last = i; wet_hours++; if (w.h[i].pp > pp) pp = w.h[i].pp; }
    }
    bool shower = wet_hours < last - wet + 1;
    snprintf(part, sizeof(part), en ? " %s from %s (%u %%)." : " %s ab %s (%u %%).", fmWxKind(w.h[wet], shower, en),
             fmWxAt(at, sizeof(at), fmWxHour(w, wet), en), (unsigned)pp);
  } else if (maybe >= 0) {
    if (en) {
      snprintf(part, sizeof(part), " %s possible from %s (%u %%).", fmWxKind(w.h[maybe], true, en),
               fmWxAt(at, sizeof(at), fmWxHour(w, maybe), en), (unsigned)w.h[maybe].pp);
    } else {
      snprintf(part, sizeof(part), " Ab %s %s möglich (%u %%).", fmWxAt(at, sizeof(at), fmWxHour(w, maybe), en),
               fmWxKind(w.h[maybe], true, en), (unsigned)w.h[maybe].pp);
    }
  } else {
    snprintf(part, sizeof(part), en ? " Dry until at least %s." : " Trocken bis mind. %s.",
             fmWxAt(at, sizeof(at), (fmWxHour(w, end - 1) + 1) % 24, en));
  }
  fmWxAdd(out, sz, part);

  // 3. wind, only when it matters
  int gust_max = 0, gust_from = -1;
  for (int i = idx; i < end; i++) {
    if (w.h[i].gust > gust_max) gust_max = w.h[i].gust;
    if (gust_from < 0 && w.h[i].gust > FM_WX_GUST) gust_from = i;
  }
  if (gust_from >= 0) {
    char from[24] = "";
    if (gust_from > idx) snprintf(from, sizeof(from), en ? " from %s" : " ab %s", fmWxAt(at, sizeof(at), fmWxHour(w, gust_from), en));
    const char* g = gust_max >= FM_WX_STORM ? (en ? "Storm gusts" : "Sturmböen") : (en ? "Gusts" : "Böen");
    snprintf(part, sizeof(part), en ? " %s up to %d %s%s." : " %s bis %d %s%s.", g, V(gust_max), imp ? "mph" : "km/h", from);
    fmWxAdd(out, sz, part);
  }

  // 4. night low and the day's range ("heute"/"today" before 5 o'clock, else "morgen"/"tomorrow")
  int night_min = 100, night_n = 0;
  for (int i = idx; i < w.n && i < idx + 24; i++) {
    int hr = fmWxHour(w, i);
    bool night = hr >= 22 || hr <= 6;
    if (night) { night_n++; if (w.h[i].t < night_min) night_min = w.h[i].t; }
    else if (night_n > 0) break;   // first night only
  }
  int day = fmWxDay(w, idx) + (fmWxHour(w, idx) < 5 ? 0 : 1);
  int dmin = 100, dmax = -100, dn = 0;
  for (int i = idx; i < w.n; i++) {
    if (fmWxDay(w, i) != day) continue;
    dn++;
    if (w.h[i].t < dmin) dmin = w.h[i].t;
    if (w.h[i].t > dmax) dmax = w.h[i].t;
  }
  part[0] = 0;
  if (night_n > 0) {
    snprintf(part, sizeof(part), en ? " Night %d%s%s" : " Nacht %d%s%s", T(night_min), deg, night_min < 0 ? (en ? " (frost)" : " (Frost)") : "");
  }
  if (dn >= 12) {
    char d[56];
    const char* when = fmWxHour(w, idx) < 5 ? (en ? " today" : " heute") : (en ? " tomorrow" : " morgen");
    const char* hot = dmax >= FM_WX_HOT ? (en ? " (hot)" : " (heiß)") : "";
    if (T(dmin) < 0) snprintf(d, sizeof(d), en ? "%s%s %d to %d%s%s" : "%s%s %d bis %d%s%s", part[0] ? "," : "", when, T(dmin), T(dmax), deg, hot);
    else snprintf(d, sizeof(d), "%s%s %d–%d%s%s", part[0] ? "," : "", when, T(dmin), T(dmax), deg, hot);
    fmWxCopy(part + strlen(part), d, sizeof(part) - strlen(part));
  }
  if (part[0]) {
    fmWxCopy(part + strlen(part), ".", sizeof(part) - strlen(part));
    fmWxAdd(out, sz, part);
  }

  // 5. age, only when old
  if (now > w.fetched_at + FM_WX_OLD_S) {
    snprintf(part, sizeof(part), en ? " Data %lu h old." : " Stand vor %lu Std.", (unsigned long)((now - w.fetched_at) / 3600));
    fmWxAdd(out, sz, part);
  }
}
