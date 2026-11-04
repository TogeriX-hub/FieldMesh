#pragma once
// FieldMesh repeater: sun and moon for the SunCast room commands ('!sun', '!day', '!moon').
// Plain C++ without Arduino/MeshCore, so the maths can be tested on a PC
// (test/test_fieldmesh_sun). Everything is computed offline from the box position and the
// date - no internet needed, which is the point for an off-grid box.
//
// Sun: NOAA solar position equations (low-precision version of Meeus, "Astronomical
//   Algorithms", ch. 25), iterated at the event time, so rise/set land within about a
//   minute of ephem/astral up to the polar circles.
//   Sunrise/sunset: centre of the sun at -0.833 deg (34' refraction + 16' semi-diameter).
//   "Daylight until": end of civil twilight, centre at -6 deg (enough light outdoors).
// Moon: illuminated fraction and phase from the phase angle (Meeus ch. 48, short series),
//   good to about 1 % - plenty for "waxing gibbous, 78 %".
//
// All times are unix seconds UTC. Local time is the caller's job.

#include <math.h>
#include <stdint.h>

#define FM_SUN_ALT_RISESET   (-0.833)     // deg, sunrise / sunset
#define FM_SUN_ALT_CIVIL     (-6.0)       // deg, civil dawn / dusk

#define FM_SUN_NORMAL        0            // event exists
#define FM_SUN_ALWAYS_UP     1            // sun stays above the altitude all day (polar day)
#define FM_SUN_ALWAYS_DOWN   (-1)         // sun stays below it all day (polar night)

static const double FM_PI  = 3.14159265358979323846;
static inline double fmRad(double d) { return d * FM_PI / 180.0; }
static inline double fmDeg(double r) { return r * 180.0 / FM_PI; }
static inline double fmNorm360(double a) { a = fmod(a, 360.0); return a < 0 ? a + 360.0 : a; }

// ---- calendar (proleptic Gregorian, no time zones) ----

// days since 1970-01-01 for a civil date (H. Hinnant's algorithm, valid for all our dates)
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

// ---- sun ----

// Solar declination (deg) and equation of time (minutes) at unix time t.
static inline void fmSunPos(double t, double& decl, double& eqt) {
  const double T = (t / 86400.0 + 2440587.5 - 2451545.0) / 36525.0;   // Julian centuries J2000
  const double L0 = fmNorm360(280.46646 + T * (36000.76983 + T * 0.0003032));
  const double M = 357.52911 + T * (35999.05029 - 0.0001537 * T);
  const double e = 0.016708634 - T * (0.000042037 + 0.0000001267 * T);
  const double Mr = fmRad(M);
  const double C = sin(Mr) * (1.914602 - T * (0.004817 + 0.000014 * T))
                 + sin(2 * Mr) * (0.019993 - 0.000101 * T) + sin(3 * Mr) * 0.000289;
  const double omega = fmRad(125.04 - 1934.136 * T);
  const double lambda = fmRad(L0 + C - 0.00569 - 0.00478 * sin(omega));   // apparent longitude
  const double eps0 = 23.0 + (26.0 + (21.448 - T * (46.815 + T * (0.00059 - T * 0.001813))) / 60.0) / 60.0;
  const double eps = fmRad(eps0 + 0.00256 * cos(omega));                  // obliquity, corrected
  decl = fmDeg(asin(sin(eps) * sin(lambda)));
  const double y = tan(eps / 2) * tan(eps / 2);
  const double L0r = fmRad(L0);
  eqt = 4.0 * fmDeg(y * sin(2 * L0r) - 2 * e * sin(Mr) + 4 * e * y * sin(Mr) * cos(2 * L0r)
                    - 0.5 * y * y * sin(4 * L0r) - 1.25 * e * e * sin(2 * Mr));
}

// Solar noon (unix UTC) of the day that starts at day0 (00:00 UTC of the civil date).
// The "day" is the one whose solar noon falls on that date at this longitude, so for any
// place inside its normal time zone it is the local calendar day.
static inline uint32_t fmSunNoon(double lon, uint32_t day0) {
  double t = day0 + 43200.0 - lon * 240.0;           // first guess: 12:00 local mean time
  for (int i = 0; i < 2; i++) {
    double decl, eqt;
    fmSunPos(t, decl, eqt);
    t = day0 + (720.0 - 4.0 * lon - eqt) * 60.0;
  }
  return (uint32_t)lround(t);
}

// Hour angle (deg) at which the sun's centre reaches 'alt'; returns FM_SUN_* .
static inline int fmSunHourAngle(double lat, double decl, double alt, double& H) {
  const double c = (sin(fmRad(alt)) - sin(fmRad(lat)) * sin(fmRad(decl)))
                 / (cos(fmRad(lat)) * cos(fmRad(decl)));
  if (c > 1.0)  return FM_SUN_ALWAYS_DOWN;
  if (c < -1.0) return FM_SUN_ALWAYS_UP;
  H = fmDeg(acos(c));
  return FM_SUN_NORMAL;
}

// Morning and evening time (unix UTC) at which the sun's centre passes 'alt' on the day that
// starts at day0. Returns FM_SUN_NORMAL with rise/set filled, or FM_SUN_ALWAYS_UP /
// FM_SUN_ALWAYS_DOWN (polar day / night, rise/set untouched). Each event is iterated with the
// sun position at that moment (declination moves up to 0.4 deg per day around the equinoxes).
static inline int fmSunEvent(double lat, double lon, uint32_t day0, double alt,
                             uint32_t& rise, uint32_t& set) {
  const double noon = fmSunNoon(lon, day0);
  double decl, eqt, H;
  fmSunPos(noon, decl, eqt);
  int st = fmSunHourAngle(lat, decl, alt, H);
  if (st != FM_SUN_NORMAL) return st;

  double ev[2];
  for (int k = 0; k < 2; k++) {
    const double sign = k == 0 ? -1.0 : 1.0;
    double t = noon + sign * H * 240.0;
    for (int i = 0; i < 3; i++) {
      fmSunPos(t, decl, eqt);
      double h;
      // On the first/last days of polar day or night only one of the two events happens
      // (e.g. the sun dips below the altitude after this day but not in the night before).
      // Like ephem, the whole day then counts as polar day / night.
      st = fmSunHourAngle(lat, decl, alt, h);
      if (st != FM_SUN_NORMAL) return st;
      const double tn = day0 + (720.0 - 4.0 * lon - eqt) * 60.0;   // transit near t
      t = tn + sign * h * 240.0;
    }
    ev[k] = t;
  }
  rise = (uint32_t)lround(ev[0]);
  set = (uint32_t)lround(ev[1]);
  return FM_SUN_NORMAL;
}

// Length of daylight (sunrise to sunset) in seconds; 86400 for polar day, 0 for polar night.
static inline uint32_t fmSunDayLength(double lat, double lon, uint32_t day0) {
  uint32_t r, s;
  int st = fmSunEvent(lat, lon, day0, FM_SUN_ALT_RISESET, r, s);
  if (st == FM_SUN_ALWAYS_UP) return 86400;
  if (st == FM_SUN_ALWAYS_DOWN) return 0;
  return s - r;
}

// Longest and shortest daylight of the year 'y' (seconds). The solstices move by about a day
// between years and the day length changes by seconds there, so a few days around 21 June and
// 21 December are enough. 'long_md' / 'short_md' get month*100+day of those days (swapped on
// the southern hemisphere).
static inline void fmSunYearExtremes(double lat, double lon, int32_t y,
                                     uint32_t& longest, uint32_t& shortest,
                                     int& long_md, int& short_md) {
  longest = 0; shortest = 86401; long_md = short_md = 0;
  const int cand[2] = { 6, 12 };
  for (int c = 0; c < 2; c++) {
    for (int d = 19; d <= 23; d++) {
      uint32_t day0 = (uint32_t)fmDaysFromCivil(y, cand[c], d) * 86400UL;
      uint32_t len = fmSunDayLength(lat, lon, day0);
      if (len > longest)  { longest = len;  long_md = cand[c] * 100 + d; }
      if (len < shortest) { shortest = len; short_md = cand[c] * 100 + d; }
    }
  }
}

// ---- moon ----

#define FM_MOON_NEW              0
#define FM_MOON_WAXING_CRESCENT  1
#define FM_MOON_FIRST_QUARTER    2
#define FM_MOON_WAXING_GIBBOUS   3
#define FM_MOON_FULL             4
#define FM_MOON_WANING_GIBBOUS   5
#define FM_MOON_LAST_QUARTER     6
#define FM_MOON_WANING_CRESCENT  7

struct FmMoon {
  double  illum;      // illuminated fraction 0..1
  double  elong;      // elongation from the sun 0..360 deg (0 new, 90 first quarter, 180 full)
  uint8_t phase;      // FM_MOON_*
};

// Moon at unix time t (Meeus ch. 48, phase angle from the short series).
static inline FmMoon fmMoonAt(double t) {
  const double T = (t / 86400.0 + 2440587.5 - 2451545.0) / 36525.0;
  const double D  = fmNorm360(297.8501921 + T * (445267.1114034 + T * (-0.0018819 + T * (1.0 / 545868.0 - T / 113065000.0))));
  const double M  = fmNorm360(357.5291092 + T * (35999.0502909 + T * (-0.0001536 + T / 24490000.0)));
  const double Mp = fmNorm360(134.9633964 + T * (477198.8675055 + T * (0.0087414 + T * (1.0 / 69699.0 - T / 14712000.0))));
  const double Dr = fmRad(D), Mr = fmRad(M), Mpr = fmRad(Mp);
  const double i = 180.0 - D - 6.289 * sin(Mpr) + 2.100 * sin(Mr) - 1.274 * sin(2 * Dr - Mpr)
                 - 0.658 * sin(2 * Dr) - 0.214 * sin(2 * Mpr) - 0.110 * sin(Dr);
  FmMoon m;
  m.illum = (1.0 + cos(fmRad(i))) / 2.0;
  m.elong = fmNorm360(180.0 - i);
  // Named phases cover +-1 day around the exact moment (about 12 deg of elongation), so
  // "full moon" is shown on the evening people would call it full.
  const double e = m.elong;
  if (e < 6.0 || e >= 354.0)      m.phase = FM_MOON_NEW;
  else if (e < 84.0)              m.phase = FM_MOON_WAXING_CRESCENT;
  else if (e < 96.0)              m.phase = FM_MOON_FIRST_QUARTER;
  else if (e < 174.0)             m.phase = FM_MOON_WAXING_GIBBOUS;
  else if (e < 186.0)             m.phase = FM_MOON_FULL;
  else if (e < 264.0)             m.phase = FM_MOON_WANING_GIBBOUS;
  else if (e < 276.0)             m.phase = FM_MOON_LAST_QUARTER;
  else                            m.phase = FM_MOON_WANING_CRESCENT;
  return m;
}

static inline const char* fmMoonPhaseName(uint8_t p, bool en) {
  switch (p) {
    case FM_MOON_NEW:             return en ? "new moon" : "Neumond";
    case FM_MOON_WAXING_CRESCENT: return en ? "waxing crescent" : "zunehmende Sichel";
    case FM_MOON_FIRST_QUARTER:   return en ? "first quarter" : "zunehmender Halbmond";
    case FM_MOON_WAXING_GIBBOUS:  return en ? "waxing gibbous" : "zunehmender Mond";
    case FM_MOON_FULL:            return en ? "full moon" : "Vollmond";
    case FM_MOON_WANING_GIBBOUS:  return en ? "waning gibbous" : "abnehmender Mond";
    case FM_MOON_LAST_QUARTER:    return en ? "last quarter" : "abnehmender Halbmond";
  }
  return en ? "waning crescent" : "abnehmende Sichel";
}

// Next moment after t (unix UTC) when the elongation reaches 'target' deg (0 = new moon,
// 180 = full moon). Steps half a day and bisects; accurate to a couple of hours, enough for
// "full moon in 3 days". Returns 0 if nothing is found within 31 days (cannot happen).
static inline uint32_t fmMoonNext(double t, double target) {
  auto diff = [target](double tt) {            // signed distance to target, -180..180
    double d = fmNorm360(fmMoonAt(tt).elong - target);
    return d >= 180.0 ? d - 360.0 : d;
  };
  double a = t, da = diff(a);
  for (int s = 0; s < 62; s++) {
    double b = a + 43200.0, db = diff(b);
    if (da < 0 && db >= 0 && db - da < 90.0) {  // crossing (not the wrap on the far side)
      for (int i = 0; i < 20; i++) {
        double m = (a + b) / 2, dm = diff(m);
        if (dm < 0) a = m; else b = m;
      }
      return (uint32_t)lround((a + b) / 2);
    }
    a = b; da = db;
  }
  return 0;
}
