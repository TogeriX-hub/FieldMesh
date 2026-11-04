#pragma once
// FieldMesh repeater: local time for the box from a POSIX time zone rule, e.g.
//   CET-1CEST,M3.5.0,M10.5.0/3      (Central Europe)
//   EST5EDT,M3.2.0,M11.1.0          (US Eastern)
//   AEST-10AEDT,M10.1.0,M4.1.0/3    (Sydney, southern hemisphere)
//   <+03>-3                         (no daylight saving time)
// The whole rule is stored, not just today's offset, so the box still shows the right local
// time after a daylight saving change without a phone. The rule comes from the phone that
// opens the dashboard (derived from the browser) or from the setup page.
//
// Evaluated here instead of setenv("TZ")/tzset(): no global state in the firmware, and the
// rules can be tested on a PC (test/test_fieldmesh_timezone). Plain C++, no Arduino/MeshCore.
//
// Supported: names as letters or <...>, offsets [+-]hh[:mm[:ss]], daylight saving rules in
// the "Mm.w.d[/time]" form (the only form the browser and the setup list produce, and the
// one all current tzdata rules use). The "Jn" and "n" day forms are rejected.

#include <stdint.h>
#include <string.h>

#define FM_TZ_LEN  48            // longest rule we keep (incl. terminator)

// ---- calendar (proleptic Gregorian, no time zones) ----

// days since 1970-01-01 for a civil date (H. Hinnant's algorithm)
static inline int32_t fmDaysFromCivil(int32_t y, int32_t m, int32_t d) {
  y -= m <= 2;
  const int32_t era = (y >= 0 ? y : y - 399) / 400;
  const int32_t yoe = y - era * 400;
  const int32_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const int32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}

static inline void fmCivilFromDays(int32_t z, int32_t& y, int32_t& m, int32_t& d) {
  z += 719468;
  const int32_t era = (z >= 0 ? z : z - 146096) / 146097;
  const int32_t doe = z - era * 146097;
  const int32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const int32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const int32_t mp = (5 * doy + 2) / 153;
  d = doy - (153 * mp + 2) / 5 + 1;
  m = mp + (mp < 10 ? 3 : -9);
  y = yoe + era * 400 + (m <= 2);
}

static inline int32_t fmFloorDiv(int64_t a, int32_t b) {
  int64_t q = a / b;
  if ((a % b != 0) && ((a < 0) != (b < 0))) q--;
  return (int32_t)q;
}

// weekday 0 = Sunday .. 6 = Saturday (1970-01-01 was a Thursday)
static inline int fmWeekday(int32_t days) { int w = (int)((days + 4) % 7); return w < 0 ? w + 7 : w; }

static inline int fmDaysInMonth(int32_t y, int32_t m) {
  return fmDaysFromCivil(m == 12 ? y + 1 : y, m == 12 ? 1 : m + 1, 1) - fmDaysFromCivil(y, m, 1);
}

// ---- rule ----

struct FmTzTrans {
  uint8_t m, w, d;     // month 1-12, week 1-5 (5 = last), weekday 0 = Sunday
  int32_t time;        // seconds after local midnight, in the time valid before the change
};

struct FmTz {
  int32_t   std_off;   // seconds east of UTC (local = UTC + offset); POSIX writes the opposite sign
  int32_t   dst_off;
  bool      has_dst;
  FmTzTrans start;     // change to daylight saving time
  FmTzTrans end;       // change back to standard time
};

static inline bool fmTzName(const char*& p) {
  if (*p == '<') {
    const char* q = strchr(p, '>');
    if (!q || q == p + 1) return false;
    p = q + 1;
    return true;
  }
  const char* s = p;
  while ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z')) p++;
  return p - s >= 3;
}

static inline bool fmTzNum(const char*& p, int lo, int hi, int& v) {
  if (*p < '0' || *p > '9') return false;
  v = 0;
  while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); if (v > hi) return false; p++; }
  return v >= lo;
}

// [+-]hh[:mm[:ss]] -> seconds; hours up to max_h (24 for offsets, 167 for rule times)
static inline bool fmTzHms(const char*& p, int max_h, int32_t& out) {
  int sign = 1;
  if (*p == '+') p++;
  else if (*p == '-') { sign = -1; p++; }
  int h, m = 0, s = 0;
  if (!fmTzNum(p, 0, max_h, h)) return false;
  if (*p == ':') { p++; if (!fmTzNum(p, 0, 59, m)) return false;
    if (*p == ':') { p++; if (!fmTzNum(p, 0, 59, s)) return false; } }
  out = sign * (h * 3600 + m * 60 + s);
  return true;
}

static inline bool fmTzTrans(const char*& p, FmTzTrans& t) {
  int m, w, d;
  if (*p++ != 'M') return false;
  if (!fmTzNum(p, 1, 12, m) || *p++ != '.') return false;
  if (!fmTzNum(p, 1, 5, w) || *p++ != '.') return false;
  if (!fmTzNum(p, 0, 6, d)) return false;
  t.m = (uint8_t)m; t.w = (uint8_t)w; t.d = (uint8_t)d;
  t.time = 2 * 3600;
  if (*p == '/') { p++; if (!fmTzHms(p, 167, t.time)) return false; }
  return true;
}

// Parses a rule; false for anything malformed or unsupported (then 'tz' is undefined).
static inline bool fmTzParse(const char* s, FmTz& tz) {
  if (!s || !*s || strlen(s) >= FM_TZ_LEN) return false;
  const char* p = s;
  int32_t off;
  if (!fmTzName(p) || !fmTzHms(p, 24, off)) return false;
  tz.std_off = -off;
  tz.has_dst = false;
  tz.dst_off = tz.std_off;
  if (*p == 0) return true;
  if (!fmTzName(p)) return false;
  tz.has_dst = true;
  tz.dst_off = tz.std_off + 3600;
  if (*p != ',' && *p != 0) {
    if (!fmTzHms(p, 24, off)) return false;
    tz.dst_off = -off;
  }
  // a daylight saving name without rules (US default rules in POSIX) is not supported
  if (*p++ != ',' || !fmTzTrans(p, tz.start)) return false;
  if (*p++ != ',' || !fmTzTrans(p, tz.end)) return false;
  return *p == 0;
}

// Local wall-clock seconds since 1970 (as if it were UTC) at which the change happens in year y.
static inline int64_t fmTzTransLocal(int32_t y, const FmTzTrans& t) {
  const int32_t first = fmDaysFromCivil(y, t.m, 1);
  int day = 1 + (t.d - fmWeekday(first) + 7) % 7 + (t.w - 1) * 7;
  const int dim = fmDaysInMonth(y, t.m);
  while (day > dim) day -= 7;                    // week 5 = last such weekday
  return (int64_t)(first + day - 1) * 86400 + t.time;
}

// UTC offset (seconds east) valid at unix time t.
static inline int32_t fmTzOffset(const FmTz& tz, int64_t t) {
  if (!tz.has_dst) return tz.std_off;
  int32_t y, m, d;
  fmCivilFromDays(fmFloorDiv(t + tz.std_off, 86400), y, m, d);
  // start is given in standard time, end in daylight saving time
  const int64_t start = fmTzTransLocal(y, tz.start) - tz.std_off;
  const int64_t end = fmTzTransLocal(y, tz.end) - tz.dst_off;
  bool dst = start < end ? (t >= start && t < end)       // northern hemisphere
                         : !(t >= end && t < start);     // southern: DST over New Year
  return dst ? tz.dst_off : tz.std_off;
}

// Local time for unix time t (seconds since 1970 as if it were UTC); 'off' gets the offset used.
static inline int64_t fmTzLocal(const FmTz& tz, int64_t t, int32_t* off = 0) {
  const int32_t o = fmTzOffset(tz, t);
  if (off) *off = o;
  return t + o;
}
