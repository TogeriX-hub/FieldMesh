#pragma once
// FieldMesh repeater: sun times for the room command '!sun' / '!sonne' (after Tobias' SunCast).
// Plain C++ without Arduino/MeshCore, so the maths can be tested on a PC
// (test/test_fieldmesh_sun). Everything is computed offline from the box position and the
// date - no internet needed, which is the point for an off-grid box.
//
// Sun: NOAA solar position equations (low-precision version of Meeus, "Astronomical
//   Algorithms", ch. 25), iterated at the event time, so rise/set land within about a
//   minute of ephem/astral up to the polar circles.
//   Sunrise/sunset: centre of the sun at -0.833 deg (34' refraction + 16' semi-diameter).
//   "Daylight until": end of civil twilight, centre at -6 deg (enough light outdoors).
//
// All times are unix seconds UTC. Local time is the caller's job.

#include <math.h>
#include <stdint.h>

#include "FieldMeshTimeZone.h"   // calendar helpers

#define FM_SUN_ALT_RISESET   (-0.833)     // deg, sunrise / sunset
#define FM_SUN_ALT_CIVIL     (-6.0)       // deg, civil dawn / dusk

#define FM_SUN_NORMAL        0            // event exists
#define FM_SUN_ALWAYS_UP     1            // sun stays above the altitude all day (polar day)
#define FM_SUN_ALWAYS_DOWN   (-1)         // sun stays below it all day (polar night)

static const double FM_PI  = 3.14159265358979323846;
static inline double fmRad(double d) { return d * FM_PI / 180.0; }
static inline double fmDeg(double r) { return r * 180.0 / FM_PI; }
static inline double fmNorm360(double a) { a = fmod(a, 360.0); return a < 0 ? a + 360.0 : a; }

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
