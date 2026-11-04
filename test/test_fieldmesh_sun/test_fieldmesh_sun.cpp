// FieldMesh: tests for the sun maths behind '!sun' / '!sonne'
// (examples/simple_repeater/FieldMeshSunText.h). Reference values come from PyEphem 4.2.1
// (VSOP87 theory, independent of the NOAA formulas used in the firmware),
// generated with the same definition: events around the solar noon of the civil date,
// rise/set with the upper limb at -34' (centre -0.833 deg), civil twilight with the centre
// at -6 deg, no extra refraction. Places cover both hemispheres, the tropics, both sides of
// Greenwich and the polar circle (polar day/night).

#include <gtest/gtest.h>

#include <string>

#include "../../examples/simple_repeater/FieldMeshSunText.h"

namespace {

struct SunRef {
  double lat, lon;
  int y, m, d;
  int st;  uint32_t rise, set;      // sunrise / sunset
  int cst; uint32_t dawn, dusk;     // civil twilight
};

const SunRef SUN[] = {
  // Stuttgart
  {48.78, 9.18, 2026, 1, 15, 0, 1768461069u, 1768492474u, 0, 1768458925u, 1768494618u},
  {48.78, 9.18, 2026, 3, 20, 0, 1773984387u, 1774028155u, 0, 1773982504u, 1774030044u},
  {48.78, 9.18, 2026, 5, 10, 0, 1778384941u, 1778439072u, 0, 1778382723u, 1778441302u},
  {48.78, 9.18, 2026, 6, 21, 0, 1782012002u, 1782070209u, 0, 1782009447u, 1782072763u},
  {48.78, 9.18, 2026, 8, 30, 0, 1788064630u, 1788113383u, 0, 1788062649u, 1788115355u},
  {48.78, 9.18, 2026, 9, 23, 0, 1790140266u, 1790183960u, 0, 1790138377u, 1790185844u},
  {48.78, 9.18, 2026, 11, 4, 0, 1793772874u, 1793807909u, 0, 1793770874u, 1793809907u},
  {48.78, 9.18, 2026, 12, 21, 0, 1797837214u, 1797866945u, 0, 1797834982u, 1797869177u},
  {48.78, 9.18, 2030, 2, 28, 0, 1898489235u, 1898528705u, 0, 1898487337u, 1898530607u},
  {48.78, 9.18, 2040, 7, 4, 0, 2224985203u, 2225042916u, 0, 2224982693u, 2225045419u},
  // Tromso
  {69.65, 18.96, 2026, 1, 15, 0, 1768472950u, 1768475918u, 0, 1768463829u, 1768485042u},
  {69.65, 18.96, 2026, 3, 20, 0, 1773981834u, 1774026091u, 0, 1773978228u, 1774029724u},
  {69.65, 18.96, 2026, 5, 10, 0, 1778372716u, 1778446923u, 1, 0u, 0u},
  {69.65, 18.96, 2026, 6, 21, 1, 0u, 0u, 1, 0u, 0u},
  {69.65, 18.96, 2026, 8, 30, 0, 1788058362u, 1788114839u, 0, 1788053776u, 1788119335u},
  {69.65, 18.96, 2026, 9, 23, 0, 1790137672u, 1790181786u, 0, 1790134040u, 1790185390u},
  {69.65, 18.96, 2026, 11, 4, 0, 1793777113u, 1793798938u, 0, 1793772579u, 1793803465u},
  {69.65, 18.96, 2026, 12, 21, -1, 0u, 0u, 0, 1797841875u, 1797857588u},
  {69.65, 18.96, 2030, 2, 28, 0, 1898489675u, 1898523626u, 0, 1898486007u, 1898527309u},
  {69.65, 18.96, 2040, 7, 4, 1, 0u, 0u, 1, 0u, 0u},
  // Sydney
  {-33.87, 151.21, 2026, 1, 15, 0, 1768417168u, 1768468140u, 0, 1768415469u, 1768469835u},
  {-33.87, 151.21, 2026, 3, 20, 0, 1773950277u, 1773994017u, 0, 1773948781u, 1773995510u},
  {-33.87, 151.21, 2026, 5, 10, 0, 1778358975u, 1778396791u, 0, 1778357395u, 1778398371u},
  {-33.87, 151.21, 2026, 6, 21, 0, 1781989199u, 1782024827u, 0, 1781987534u, 1782026492u},
  {-33.87, 151.21, 2026, 8, 30, 0, 1788034601u, 1788075338u, 0, 1788033091u, 1788076849u},
  {-33.87, 151.21, 2026, 9, 23, 0, 1790106238u, 1790149916u, 0, 1790104744u, 1790151412u},
  {-33.87, 151.21, 2026, 11, 4, 0, 1793731940u, 1793780737u, 0, 1793730342u, 1793782340u},
  {-33.87, 151.21, 2026, 12, 21, 0, 1797792037u, 1797843926u, 0, 1797790289u, 1797845674u},
  {-33.87, 151.21, 2030, 2, 28, 0, 1898451706u, 1898497986u, 0, 1898450178u, 1898499511u},
  {-33.87, 151.21, 2040, 7, 4, 0, 2224962050u, 2224997916u, 0, 2224960395u, 2224999571u},
  // Denver
  {39.74, -104.99, 2026, 1, 15, 0, 1768486748u, 1768521605u, 0, 1768484974u, 1768523379u},
  {39.74, -104.99, 2026, 3, 20, 0, 1774011789u, 1774055528u, 0, 1774010176u, 1774057144u},
  {39.74, -104.99, 2026, 5, 10, 0, 1778413806u, 1778464989u, 0, 1778412004u, 1778466797u},
  {39.74, -104.99, 2026, 6, 21, 0, 1782041533u, 1782095487u, 0, 1782039570u, 1782097450u},
  {39.74, -104.99, 2026, 8, 30, 0, 1788092780u, 1788140040u, 0, 1788091111u, 1788141704u},
  {39.74, -104.99, 2026, 9, 23, 0, 1790167721u, 1790211307u, 0, 1790166105u, 1790212920u},
  {39.74, -104.99, 2026, 11, 4, 0, 1793799130u, 1793836465u, 0, 1793797439u, 1793838154u},
  {39.74, -104.99, 2026, 12, 21, 0, 1797862651u, 1797896329u, 0, 1797860826u, 1797898155u},
  {39.74, -104.99, 2030, 2, 28, 0, 1898516054u, 1898556668u, 0, 1898514431u, 1898558294u},
  {39.74, -104.99, 2040, 7, 4, 0, 2225014665u, 2225068270u, 0, 2225012723u, 2225070209u},
  // Quito
  {-0.18, -78.47, 2026, 1, 15, 0, 1768475964u, 1768519638u, 0, 1768474636u, 1768520965u},
  {-0.18, -78.47, 2026, 3, 20, 0, 1774005480u, 1774049070u, 0, 1774004240u, 1774050310u},
  {-0.18, -78.47, 2026, 5, 10, 0, 1778411221u, 1778454810u, 0, 1778409919u, 1778456113u},
  {-0.18, -78.47, 2026, 6, 21, 0, 1782040344u, 1782083945u, 0, 1782038991u, 1782085299u},
  {-0.18, -78.47, 2026, 8, 30, 0, 1788088277u, 1788131857u, 0, 1788087022u, 1788133112u},
  {-0.18, -78.47, 2026, 9, 23, 0, 1790161377u, 1790204965u, 0, 1790160137u, 1790206205u},
  {-0.18, -78.47, 2026, 11, 4, 0, 1793789627u, 1793833267u, 0, 1793788341u, 1793834554u},
  {-0.18, -78.47, 2026, 12, 21, 0, 1797851278u, 1797894968u, 0, 1797849927u, 1797896319u},
  {-0.18, -78.47, 2030, 2, 28, 0, 1898508174u, 1898551784u, 0, 1898506923u, 1898553034u},
  {-0.18, -78.47, 2040, 7, 4, 0, 2225013310u, 2225056909u, 0, 2225011963u, 2225058255u},
  // Anchorage
  {61.22, -149.90, 2026, 1, 15, 0, 1768503385u, 1768526546u, 0, 1768500028u, 1768529904u},
  {61.22, -149.90, 2026, 3, 20, 0, 1774022396u, 1774066522u, 0, 1774019808u, 1774069123u},
  {61.22, -149.90, 2026, 5, 10, 0, 1778419485u, 1778480944u, 0, 1778415602u, 1778484892u},
  {61.22, -149.90, 2026, 6, 21, 0, 1782044419u, 1782114159u, 1, 0u, 0u},
  {61.22, -149.90, 2026, 8, 30, 0, 1788101224u, 1788153087u, 0, 1788098359u, 1788155928u},
  {61.22, -149.90, 2026, 9, 23, 0, 1790178413u, 1790222121u, 0, 1790175816u, 1790224705u},
  {61.22, -149.90, 2026, 11, 4, 0, 1793813790u, 1793843335u, 0, 1793810898u, 1793846224u},
  {61.22, -149.90, 2026, 12, 21, 0, 1797880460u, 1797900084u, 0, 1797876720u, 1797903825u},
  {61.22, -149.90, 2030, 2, 28, 0, 1898528539u, 1898565775u, 0, 1898525928u, 1898568394u},
  {61.22, -149.90, 2040, 7, 4, 0, 2225017962u, 2225086482u, 1, 0u, 0u},
};

// first and last days of midnight sun and polar night in Tromsoe, day by day
struct EdgeRef { int y, m, d; int st; uint32_t rise, set; };
const EdgeRef EDGE[] = {
  {2026, 5, 12, 0, 1778544619u, 1778620673u},
  {2026, 5, 13, 0, 1778630528u, 1778707606u},
  {2026, 5, 14, 0, 1778716395u, 1778794600u},
  {2026, 5, 15, 0, 1778802203u, 1778881688u},
  {2026, 5, 16, 0, 1778887919u, 1778968952u},
  {2026, 5, 17, 0, 1778973458u, 1779056858u},
  {2026, 5, 18, 1, 0u, 0u},
  {2026, 5, 19, 1, 0u, 0u},
  {2026, 5, 20, 1, 0u, 0u},
  {2026, 5, 21, 1, 0u, 0u},
  {2026, 5, 22, 1, 0u, 0u},
  {2026, 5, 23, 1, 0u, 0u},
  {2026, 5, 24, 1, 0u, 0u},
  {2026, 5, 25, 1, 0u, 0u},
  {2026, 7, 22, 1, 0u, 0u},
  {2026, 7, 23, 1, 0u, 0u},
  {2026, 7, 24, 1, 0u, 0u},
  {2026, 7, 25, 1, 0u, 0u},
  {2026, 7, 26, 0, 1785020755u, 1785103976u},
  {2026, 7, 27, 0, 1785108563u, 1785189531u},
  {2026, 7, 28, 0, 1785195808u, 1785275254u},
  {2026, 7, 29, 0, 1785282883u, 1785361067u},
  {2026, 7, 30, 0, 1785369867u, 1785446938u},
  {2026, 7, 31, 0, 1785456793u, 1785532849u},
  {2026, 8, 1, 0, 1785543676u, 1785618791u},
  {2026, 8, 2, 0, 1785630528u, 1785704756u},
  {2026, 8, 3, 0, 1785717355u, 1785790740u},
  {2026, 8, 4, 0, 1785804162u, 1785876740u},
  {2026, 11, 22, 0, 1795338856u, 1795347925u},
  {2026, 11, 23, 0, 1795425774u, 1795433841u},
  {2026, 11, 24, 0, 1795512746u, 1795519705u},
  {2026, 11, 25, 0, 1795599801u, 1795605486u},
  {2026, 11, 26, 0, 1795687017u, 1795691109u},
  {2026, 11, 27, -1, 0u, 0u},
  {2026, 11, 28, -1, 0u, 0u},
  {2026, 11, 29, -1, 0u, 0u},
  {2026, 11, 30, -1, 0u, 0u},
  {2026, 12, 1, -1, 0u, 0u},
  {2026, 12, 2, -1, 0u, 0u},
  {2026, 12, 3, -1, 0u, 0u},
  {2027, 1, 8, -1, 0u, 0u},
  {2027, 1, 9, -1, 0u, 0u},
  {2027, 1, 10, -1, 0u, 0u},
  {2027, 1, 11, -1, 0u, 0u},
  {2027, 1, 12, -1, 0u, 0u},
  {2027, 1, 13, -1, 0u, 0u},
  {2027, 1, 14, -1, 0u, 0u},
  {2027, 1, 15, 0, 1800009278u, 1800011581u},
  {2027, 1, 16, 0, 1800094584u, 1800099119u},
  {2027, 1, 17, 0, 1800180256u, 1800186289u},
  {2027, 1, 18, 0, 1800266060u, 1800273325u},
  {2027, 1, 19, 0, 1800351938u, 1800360286u},
};

constexpr int SUN_TOL_S = 60;           // one minute, the precision we show

uint32_t day0(const SunRef& r) { return (uint32_t)fmDaysFromCivil(r.y, r.m, r.d) * 86400UL; }

}  // namespace

TEST(FieldMeshSun, CalendarRoundTrip) {
  EXPECT_EQ(fmDaysFromCivil(1970, 1, 1), 0);
  EXPECT_EQ(fmDaysFromCivil(2026, 11, 4), 20761);
  EXPECT_EQ(fmDaysFromCivil(2028, 2, 29) + 1, fmDaysFromCivil(2028, 3, 1));
  for (int32_t z = 19000; z < 26000; z += 37) {
    int32_t y, m, d;
    fmCivilFromDays(z, y, m, d);
    EXPECT_EQ(fmDaysFromCivil(y, m, d), z);
  }
}

TEST(FieldMeshSun, SunriseSunsetMatchEphem) {
  for (const SunRef& r : SUN) {
    uint32_t rise = 0, set = 0;
    int st = fmSunEvent(r.lat, r.lon, day0(r), FM_SUN_ALT_RISESET, rise, set);
    SCOPED_TRACE(testing::Message() << r.lat << "," << r.lon << " " << r.y << "-" << r.m << "-" << r.d);
    ASSERT_EQ(st, r.st);
    if (st != FM_SUN_NORMAL) continue;
    EXPECT_NEAR((double)rise, (double)r.rise, SUN_TOL_S);
    EXPECT_NEAR((double)set, (double)r.set, SUN_TOL_S);
  }
}

TEST(FieldMeshSun, CivilTwilightMatchesEphem) {
  for (const SunRef& r : SUN) {
    uint32_t dawn = 0, dusk = 0;
    int st = fmSunEvent(r.lat, r.lon, day0(r), FM_SUN_ALT_CIVIL, dawn, dusk);
    SCOPED_TRACE(testing::Message() << r.lat << "," << r.lon << " " << r.y << "-" << r.m << "-" << r.d);
    ASSERT_EQ(st, r.cst);
    if (st != FM_SUN_NORMAL) continue;
    EXPECT_NEAR((double)dawn, (double)r.dawn, SUN_TOL_S);
    EXPECT_NEAR((double)dusk, (double)r.dusk, SUN_TOL_S);
  }
}

TEST(FieldMeshSun, PolarDayAndNight) {
  // Tromsoe: midnight sun in June, polar night (sun never rises) in December,
  // but civil twilight still gives a few hours of light in December
  const uint32_t jun = (uint32_t)fmDaysFromCivil(2026, 6, 21) * 86400UL;
  const uint32_t dec = (uint32_t)fmDaysFromCivil(2026, 12, 21) * 86400UL;
  uint32_t a = 0, b = 0;
  EXPECT_EQ(fmSunEvent(69.65, 18.96, jun, FM_SUN_ALT_RISESET, a, b), FM_SUN_ALWAYS_UP);
  EXPECT_EQ(fmSunEvent(69.65, 18.96, dec, FM_SUN_ALT_RISESET, a, b), FM_SUN_ALWAYS_DOWN);
  EXPECT_EQ(fmSunEvent(69.65, 18.96, dec, FM_SUN_ALT_CIVIL, a, b), FM_SUN_NORMAL);
  EXPECT_LT(a, b);
  // McMurdo (Antarctica) the other way round
  EXPECT_EQ(fmSunEvent(-77.85, 166.67, dec, FM_SUN_ALT_RISESET, a, b), FM_SUN_ALWAYS_UP);
  EXPECT_EQ(fmSunEvent(-77.85, 166.67, jun, FM_SUN_ALT_RISESET, a, b), FM_SUN_ALWAYS_DOWN);
}

TEST(FieldMeshSun, PolarEdgeDaysMatchEphem) {
  // On these days only one of sunrise/sunset may happen; the whole day must then count as
  // midnight sun / polar night, as ephem decides. Here the sun moves almost along the
  // horizon: 0.01 deg is a minute, and real refraction varies by +-0.2 deg. So on grazing
  // days (the sun is up, or down, for less than 30 minutes: 0.02 deg above the threshold
  // already gives 21 minutes at 70 deg north) either answer is accepted, and
  // times may differ by 5 minutes.
  constexpr uint32_t GRAZE_S = 30 * 60;
  constexpr int EDGE_TOL_S = 5 * 60;
  for (const EdgeRef& r : EDGE) {
    uint32_t rise = 0, set = 0;
    const uint32_t d0 = (uint32_t)fmDaysFromCivil(r.y, r.m, r.d) * 86400UL;
    SCOPED_TRACE(testing::Message() << r.y << "-" << r.m << "-" << r.d);
    const int st = fmSunEvent(69.65, 18.96, d0, FM_SUN_ALT_RISESET, rise, set);
    if (st != r.st) {
      // the side that found the events must see a grazing day
      const bool ours_found = st == FM_SUN_NORMAL;
      ASSERT_TRUE(ours_found || r.st == FM_SUN_NORMAL);
      const uint32_t up = ours_found ? set - rise : r.set - r.rise;
      const int other = ours_found ? r.st : st;
      if (other == FM_SUN_ALWAYS_DOWN) { EXPECT_LT(up, GRAZE_S); }
      else                             { EXPECT_GT(up, 86400u - GRAZE_S); }
      continue;
    }
    if (st != FM_SUN_NORMAL) continue;
    EXPECT_NEAR((double)rise, (double)r.rise, EDGE_TOL_S);
    EXPECT_NEAR((double)set, (double)r.set, EDGE_TOL_S);
  }
}

TEST(FieldMeshSun, EventsStayOnTheirDay) {
  // rise before noon before set, all within the civil day around local noon,
  // also far west/east where 00:00 UTC is in the local evening/morning
  const double lons[] = { -149.9, -104.99, -78.47, 0.0, 9.18, 151.21 };
  for (double lon : lons) {
    for (int m = 1; m <= 12; m++) {
      const uint32_t d0 = (uint32_t)fmDaysFromCivil(2026, m, 10) * 86400UL;
      uint32_t r, s;
      ASSERT_EQ(fmSunEvent(40.0, lon, d0, FM_SUN_ALT_RISESET, r, s), FM_SUN_NORMAL);
      const uint32_t noon = fmSunNoon(lon, d0);
      EXPECT_LT(r, noon);
      EXPECT_LT(noon, s);
      EXPECT_NEAR((double)noon, d0 + 43200.0 - lon * 240.0, 17 * 60);   // equation of time <= 16.5 min
    }
  }
}

// ---- room answer '!sonne' / '!sun' ----
// Expected times checked against the PyEphem values above (Stuttgart, Tromsoe) and the
// published tables for Stuttgart and Trondheim.

namespace {

FmTz tzOf(const char* rule) { FmTz tz; EXPECT_TRUE(fmTzParse(rule, tz)); return tz; }
std::string sun(bool en, bool tomorrow, double lat, double lon, uint32_t now, const FmTz* tz, bool has_pos = true) {
  char out[160];
  fmSunText(out, sizeof(out), en, tomorrow, has_pos, lat, lon, now, tz);
  return out;
}
constexpr uint32_t NOV4_1022 = 1793787720u;   // 2026-11-04 10:22 UTC
constexpr double STG_LAT = 48.78, STG_LON = 9.18;

}  // namespace

TEST(FieldMeshSunText, GermanAndEnglish) {
  const FmTz cet = tzOf("CET-1CEST,M3.5.0,M10.5.0/3");
  EXPECT_EQ(sun(false, false, STG_LAT, STG_LON, NOV4_1022, &cet),
            "Sonne 04.11.: Aufgang 07:15, Untergang 16:58, Tageslicht bis 17:32");
  EXPECT_EQ(sun(true, false, STG_LAT, STG_LON, NOV4_1022, &cet),
            "Sun Nov 4: sunrise 07:15, sunset 16:58, daylight until 17:32");
  EXPECT_EQ(sun(false, true, STG_LAT, STG_LON, NOV4_1022, &cet),
            "Sonne 05.11.: Aufgang 07:16, Untergang 16:57, Tageslicht bis 17:30");
}

TEST(FieldMeshSunText, BrowserRuleGivesTheSameAnswer) {
  const FmTz cet = tzOf("CET-1CEST,M3.5.0,M10.5.0/3");
  const FmTz web = tzOf("<+01>-1<+02>,M3.5.0,M10.5.0/3");
  EXPECT_EQ(sun(true, false, STG_LAT, STG_LON, NOV4_1022, &cet), sun(true, false, STG_LAT, STG_LON, NOV4_1022, &web));
}

TEST(FieldMeshSunText, WithoutTimeZoneInUtc) {
  EXPECT_EQ(sun(true, false, STG_LAT, STG_LON, NOV4_1022, nullptr),
            "Sun Nov 4 (UTC): sunrise 06:15, sunset 15:58, daylight until 16:32");
  EXPECT_EQ(sun(false, false, STG_LAT, STG_LON, NOV4_1022, nullptr),
            "Sonne 04.11. (UTC): Aufgang 06:15, Untergang 15:58, Tageslicht bis 16:32");
}

TEST(FieldMeshSunText, LocalDateDecidesTheDay) {
  // 23:30 local is still the 4th, 00:30 local already the 5th (UTC: both on the 4th)
  const FmTz cet = tzOf("CET-1CEST,M3.5.0,M10.5.0/3");
  EXPECT_EQ(sun(false, false, STG_LAT, STG_LON, 1793831400u, &cet).substr(0, 13), "Sonne 04.11.:");
  EXPECT_EQ(sun(false, false, STG_LAT, STG_LON, 1793835000u, &cet).substr(0, 13), "Sonne 05.11.:");
}

TEST(FieldMeshSunText, DaylightSavingDayUsesSummerTime) {
  // 2026-03-29, the change at 02:00: sunrise (after the change) in CEST
  const FmTz cet = tzOf("CET-1CEST,M3.5.0,M10.5.0/3");
  EXPECT_EQ(sun(false, false, STG_LAT, STG_LON, 1774778400u, &cet),
            "Sonne 29.03.: Aufgang 07:08, Untergang 19:49, Tageslicht bis 20:21");
}

TEST(FieldMeshSunText, PolarDayNightAndWhiteNights) {
  const FmTz no = tzOf("<+01>-1<+02>,M3.5.0,M10.5.0/3");
  EXPECT_EQ(sun(true, false, 69.65, 18.96, 1782043200u, &no), "Sun Jun 21: the sun does not set (midnight sun)");
  EXPECT_EQ(sun(false, false, 69.65, 18.96, 1782043200u, &no), "Sonne 21.06.: die Sonne geht nicht unter (Mitternachtssonne)");
  EXPECT_EQ(sun(false, false, 69.65, 18.96, 1797854400u, &no),
            "Sonne 21.12.: die Sonne geht nicht auf (Polarnacht), Tageslicht 09:31-13:53");
  EXPECT_EQ(sun(true, false, 69.65, 18.96, 1797854400u, &no),
            "Sun Dec 21: the sun does not rise (polar night), daylight 09:31-13:53");
  // Trondheim at midsummer: the sun sets, but it never gets darker than civil twilight
  EXPECT_EQ(sun(true, false, 63.43, 10.39, 1782043200u, &no), "Sun Jun 21: sunrise 03:02, sunset 23:38, light all night");
  // far north in polar night: not even civil twilight
  const std::string dark = sun(true, false, 85.0, 0.0, 1797854400u, nullptr);
  EXPECT_NE(dark.find("dark all day"), std::string::npos) << dark;
}

TEST(FieldMeshSunText, NoClockNoPosition) {
  const FmTz cet = tzOf("CET-1CEST,M3.5.0,M10.5.0/3");
  EXPECT_EQ(sun(true, false, STG_LAT, STG_LON, 1000u, &cet).rfind("Sun: the box clock is not set", 0), 0u);
  EXPECT_EQ(sun(false, false, STG_LAT, STG_LON, 1000u, &cet).rfind("Sonne: Uhr der Box noch nicht gestellt", 0), 0u);
  EXPECT_EQ(sun(true, false, 0, 0, NOV4_1022, &cet, false), "Sun: the box has no position. Admin: set it in the dashboard (Setup)");
  EXPECT_EQ(sun(false, false, 0, 0, NOV4_1022, &cet, false), "Sonne: Box hat keine Position. Admin: im Dashboard unter Setup setzen");
}

TEST(FieldMeshSunText, FitsOneRoomPost) {
  // FM_ROOM_POST_TEXT_LEN = 151; the longest answer is a polar night or the clock hint
  const FmTz cet = tzOf("CET-1CEST,M3.5.0,M10.5.0/3");
  EXPECT_LT(sun(false, false, 69.65, 18.96, 1797854400u, nullptr).size(), 151u);
  EXPECT_LT(sun(false, false, STG_LAT, STG_LON, 1000u, &cet).size(), 151u);
  char small[24];   // a short buffer is cut, never overrun
  fmSunText(small, sizeof(small), false, false, true, STG_LAT, STG_LON, NOV4_1022, &cet);
  EXPECT_EQ(strlen(small), sizeof(small) - 1);
}

TEST(FieldMeshSunText, ParseCommand) {
  bool en = false, tm = true;
  EXPECT_TRUE(fmSunParseCmd("!sonne", en, tm));          EXPECT_FALSE(en); EXPECT_FALSE(tm);
  EXPECT_TRUE(fmSunParseCmd("  !SUN  ", en, tm));        EXPECT_TRUE(en);  EXPECT_FALSE(tm);
  EXPECT_TRUE(fmSunParseCmd("!sonne morgen", en, tm));   EXPECT_FALSE(en); EXPECT_TRUE(tm);
  EXPECT_TRUE(fmSunParseCmd("!Sun   Tomorrow ", en, tm)); EXPECT_TRUE(en); EXPECT_TRUE(tm);
  const char* no[] = { "!sunny", "!sonnenbrand", "!sun morgen", "!sonne tomorrow", "!sun today",
                       "!sun tomorrow please", "sun", "! sun", "", "!so" };
  for (const char* t : no) EXPECT_FALSE(fmSunParseCmd(t, en, tm)) << "'" << t << "'";
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
