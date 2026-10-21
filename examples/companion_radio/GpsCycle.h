#pragma once

// FieldMesh v6.1: GPS duty cycle (power saving), modelled on Meshtastic (src/gps/GPS.cpp,
// GPSUpdateScheduling.cpp) - see RECHERCHE_GPS_Power_M1.md and PLAN_v6.1.md.
//
// While GPS is enabled the receiver is not kept on all the time any more:
//   - every 2 min a search for a fix, then the fix is held for 20 s, then the GPS sleeps
//   - the wake-up is moved forward by the learned search time (EWMA 0.2), so a new fix is
//     available about every 2 min; no long back-off when there is no fix
//   - search limits: first search after GPS start 180 s, then 30 s, after 3 failures 15 s,
//     every 5th failed search 120 s (enough for a new ephemeris download); never less than
//     twice the learned search time
//   - "keep awake" requests (GPS/Tracking page on screen, SOS) keep the GPS running
//
// Sleep mode is chosen centrally from the board defines, like Meshtastic:
//   PIN_GPS_STANDBY defined -> soft sleep: only the standby pin (L76K: M1, Wio L1 family),
//                              power and reset stay untouched
//   otherwise               -> hard sleep: the board's own stop_gps()/start_gps() via the
//                              "gps" setting (Airoha RTC backup on T1000-E/X1, power off on
//                              Heltec T1)
// Board files are not modified. If neither works (no provider / no "gps" setting) the cycle
// only tracks fix freshness and never sleeps.
//
// The cycle never changes prefs.gps_enabled itself, except for the M1 hardware GPS switch
// (PIN_GPS_SWITCH): GPS only runs when switch AND setting are on; switching it on sets the
// setting (reported as EV_PREFS_CHANGED so the caller saves prefs).
//
// Own position for adverts/SOS/tracking comes from here (latitude()/longitude()), only while
// hasFreshFix() - i.e. a fix from a newly parsed sentence not older than 2 min.

#include <Arduino.h>
#include <helpers/SensorManager.h>
#include "NodePrefs.h"

class GpsCycle {
public:
  enum State : uint8_t { OFF = 0, SEARCHING, HOLDING, SLEEPING, AWAKE };
  enum Event : uint8_t {
    EV_NONE          = 0,
    EV_FIX           = 0x01,   // fresh position at the end of a hold phase (about every 2 min)
    EV_SEARCH_FAILED = 0x02,   // search ended without fix
    EV_SOS_RESEND    = 0x04,   // SOS was sent without position and a fix is now available
    EV_PREFS_CHANGED = 0x08,   // prefs.gps_enabled changed (M1 hardware switch) -> save prefs
  };

  static constexpr uint32_t INTERVAL_MS        = 120000;  // fix every 2 min (Meshtastic default)
  static constexpr uint32_t HOLD_MS            = 20000;   // keep running 20 s after a fix
  static constexpr uint32_t MAX_FIX_AGE_MS     = 120000;  // never use an older position
  static constexpr uint32_t WAKE_MARGIN_MS     = 5000;    // wake a bit early -> fix age stays < 2 min
  static constexpr uint32_t SEARCH_FIRST_MS    = 180000;  // first search after GPS start
  static constexpr uint32_t SEARCH_MS          = 30000;
  static constexpr uint32_t SEARCH_SHORT_MS    = 15000;   // after 3 failed searches
  static constexpr uint32_t SEARCH_LONG_MS     = 120000;  // every 5th failed search
  static constexpr uint32_t MAX_PREDICTED_MS   = 60000;   // cap of the learned search time
  static constexpr uint32_t KEEP_AWAKE_PAGE_MS = 60000;   // GPS/Tracking page: +60 s after leaving
  static constexpr uint32_t KEEP_AWAKE_SOS_MS  = 600000;  // 10 min after sending SOS
  static constexpr uint32_t POLL_MS            = 250;

  GpsCycle() { }

  // call from MyMesh::loop(); returns a mask of Event
  uint8_t loop(SensorManager& sensors, NodePrefs& prefs) {
    uint8_t ev = EV_NONE;
    uint32_t now = millis();
    LocationProvider* loc = sensors.getLocationProvider();

    ev |= checkHardwareSwitch(sensors, prefs);

    bool enabled = loc != NULL && prefs.gps_enabled && hardwareSwitchOn()
                   && sensors.getSettingByKey("gps") != NULL;
    if (!enabled) {
      // GPS was switched off by the user/app/switch: their code already stopped the
      // hardware; the cycle only forgets its state.
      _state = OFF;
      _sos_pending = false;
      return ev;
    }

    if (_state == OFF) {   // GPS (re)started: boot, UI, app or switch already woke it
      _first_search = true;
      _fail_count = 0;
      startSearch(now);
    }

    bool new_fix = pollFix(loc, now);
    bool forced = (int32_t)(_awake_until - now) > 0;

    if (_sos_pending) {
      if (new_fix) { ev |= EV_SOS_RESEND; _sos_pending = false; }
      else if ((int32_t)(now - _sos_until) > 0) _sos_pending = false;
    }

    switch (_state) {
      case SEARCHING:
        if (new_fix) {
          learnSearchTime(now - _since);
          _fail_count = 0;
          _first_search = false;
          setState(HOLDING, now);
        } else if (now - _since >= _search_limit) {
          _fail_count++;
          _first_search = false;
          ev |= EV_SEARCH_FAILED;
          // keep trying every 2 min, counted from the start of this search
          uint32_t elapsed = now - _since;
          uint32_t wait = (elapsed < INTERVAL_MS) ? INTERVAL_MS - elapsed : 0;
          goSleep(sensors, loc, now, forced, wait);
        }
        break;

      case HOLDING:
        if (now - _since >= HOLD_MS) {
          ev |= EV_FIX;
          goSleep(sensors, loc, now, forced, nextSleepMs());
        }
        break;

      case AWAKE:   // kept running by a keep-awake request
        if (new_fix && (int32_t)(now - _next_awake_ev) >= 0) {
          ev |= EV_FIX;   // give tracking a position every 2 min while kept awake
          _next_awake_ev = now + INTERVAL_MS;
        }
        if (!forced) {
          if (hasFreshFix()) goSleep(sensors, loc, now, false, nextSleepMs());
          else startSearch(now);
        }
        break;

      case SLEEPING:
        if (_hard && boardGpsOn(sensors)) {
          startSearch(now);          // woken from outside (e.g. app "gps:1")
        } else if (forced || (int32_t)(now - _wake_at) >= 0) {
          wake(sensors, loc);
          if (forced) { setState(AWAKE, now); _next_awake_ev = now; }
          else startSearch(now);
        }
        break;

      default:
        break;
    }
    return ev;
  }

  // a fix from a newly parsed sentence, not older than 2 min, and GPS still enabled
  bool hasFreshFix() const {
    return _state != OFF && _have_fix && (uint32_t)(millis() - _last_fix_ms) <= MAX_FIX_AGE_MS;
  }
  int32_t latitude() const { return _lat; }     // degrees * 1e6 (LocationProvider format)
  int32_t longitude() const { return _lon; }
  uint8_t satellites() const { return _sats; }  // of the last accepted fix
  uint32_t fixAgeMs() const { return _have_fix ? (uint32_t)(millis() - _last_fix_ms) : 0xFFFFFFFF; }

  // keep the GPS running for at least ms from now (call repeatedly while needed)
  void keepAwake(uint32_t ms) {
    uint32_t until = millis() + ms;
    if ((int32_t)(until - _awake_until) > 0) _awake_until = until;
  }

  // after sending SOS: GPS stays on 10 min; without position report the first fix once
  void onSosSent(bool with_position) {
    keepAwake(KEEP_AWAKE_SOS_MS);
    _sos_pending = !with_position;
    _sos_until = millis() + KEEP_AWAKE_SOS_MS;
  }

  // value of a sensor setting as shown to the app: while hard-sleeping the board reports
  // "gps" = "0", but for the user the GPS is still on
  const char* reportSetting(const char* name, const char* value) const {
    if (_state == SLEEPING && _hard && strcmp(name, "gps") == 0) return "1";
    return value;
  }

  State state() const { return _state; }
  bool isHardSleep() const { return _hard; }
  uint32_t predictedSearchMs() const { return _predicted_ms; }
  uint8_t failCount() const { return _fail_count; }

  static bool hardwareSwitchOn() {
#ifdef PIN_GPS_SWITCH
    return digitalRead(PIN_GPS_SWITCH) == HIGH;   // ThinkNode M1: HIGH = switch on
#else
    return true;
#endif
  }

private:
  State    _state = OFF;
  uint32_t _since = 0;          // millis of the current state's start
  uint32_t _search_limit = SEARCH_MS;
  uint32_t _wake_at = 0;
  uint32_t _awake_until = 0;
  uint32_t _next_awake_ev = 0;
  uint32_t _last_poll = 0;
  uint32_t _last_fix_ms = 0;
  uint32_t _last_ts = 0;        // sentence timestamp of the last accepted fix
  uint32_t _predicted_ms = 0;   // learned search time (EWMA), 0 = unknown
  uint32_t _sos_until = 0;
  int32_t  _lat = 0, _lon = 0;
  uint8_t  _sats = 0;
  uint8_t  _fail_count = 0;
  uint8_t  _samples = 0;
  bool     _have_fix = false;
  bool     _first_search = true;
  bool     _sos_pending = false;
  bool     _sw_last = true;
  bool     _sw_init = false;
#ifdef PIN_GPS_STANDBY
  static constexpr bool _hard = false;
#else
  static constexpr bool _hard = true;
#endif

  void setState(State s, uint32_t now) { _state = s; _since = now; }

  // the board's own GPS state (start_gps/stop_gps) - provider isEnabled() is not usable for
  // this on boards without an enable pin (always true there)
  static bool boardGpsOn(SensorManager& sensors) {
    const char* v = sensors.getSettingByKey("gps");
    return v != NULL && strcmp(v, "1") == 0;
  }

  uint32_t searchLimit() const {
    if (_first_search) return SEARCH_FIRST_MS;
    uint32_t limit;
    if (_fail_count == 0) limit = SEARCH_MS;
    else if (_fail_count % 5 == 0) limit = SEARCH_LONG_MS;
    else if (_fail_count >= 3) limit = SEARCH_SHORT_MS;
    else limit = SEARCH_MS;
    // receivers that need longer per fix (e.g. no backup power during hard sleep) get at
    // least twice their learned search time, otherwise nearly every search would fail
    if (limit < 2 * _predicted_ms) limit = 2 * _predicted_ms;
    return limit;
  }

  void startSearch(uint32_t now) {
    _search_limit = searchLimit();
    setState(SEARCHING, now);
  }

  // Meshtastic: EWMA 0.2, first sample (cold start) discarded, failures not counted
  void learnSearchTime(uint32_t ms) {
    if (_samples++ == 0) return;
    if (ms > MAX_PREDICTED_MS) ms = MAX_PREDICTED_MS;
    _predicted_ms = (_predicted_ms == 0) ? ms : (_predicted_ms * 4 + ms) / 5;
  }

  // sleep time after a hold so that the next fix arrives about INTERVAL_MS after this one
  uint32_t nextSleepMs() const {
    uint32_t early = _predicted_ms + WAKE_MARGIN_MS;
    return (INTERVAL_MS > early) ? INTERVAL_MS - early : 0;
  }

  bool pollFix(LocationProvider* loc, uint32_t now) {
    if (now - _last_poll < POLL_MS) return false;
    _last_poll = now;
    if (_state == SLEEPING || !loc->isEnabled() || !loc->isValid()) return false;
    uint32_t ts = (uint32_t)loc->getTimestamp();
    if (ts == _last_ts) return false;   // nothing newly parsed since the last accepted fix
    _last_ts = ts;
    _last_fix_ms = now;
    _lat = loc->getLatitude();
    _lon = loc->getLongitude();
    long sats = loc->satellitesCount();
    _sats = (sats < 0) ? 0 : (sats > 255 ? 255 : (uint8_t)sats);
    _have_fix = true;
    return true;
  }

  void goSleep(SensorManager& sensors, LocationProvider* loc, uint32_t now, bool forced, uint32_t sleep_ms) {
    if (forced) { setState(AWAKE, now); _next_awake_ev = now + INTERVAL_MS; return; }
    if (sleep_ms < 1000) { startSearch(now); return; }   // no time to sleep
#ifdef PIN_GPS_STANDBY
    pinMode(PIN_GPS_STANDBY, OUTPUT);
    digitalWrite(PIN_GPS_STANDBY, LOW);   // L76K standby: LOW = sleep (Meshtastic GPS_STANDBY_ACTIVE)
#else
    sensors.setSettingValue("gps", "0");  // board stop_gps(): Airoha RTC backup / power off
#endif
    // drop the last parsed sentence: otherwise the parser keeps reporting a valid fix (and an
    // old time, which the clock sync would apply) while the receiver sleeps
    loc->syncTime();
    _wake_at = now + sleep_ms;
    setState(SLEEPING, now);
  }

  void wake(SensorManager& sensors, LocationProvider* loc) {
#ifdef PIN_GPS_STANDBY
    pinMode(PIN_GPS_STANDBY, OUTPUT);
    digitalWrite(PIN_GPS_STANDBY, HIGH);  // force awake
#else
    sensors.setSettingValue("gps", "1");  // board start_gps()
#endif
    loc->drain();   // discard UART bytes from before the sleep
  }

  // ThinkNode M1: hardware switch has priority (PLAN_v6.1: GPS runs only with switch AND setting).
  // The board itself starts/stops GPS on switch edges; here: switching on also sets the
  // setting, and while the switch is off nothing (app, prefs) may keep GPS running.
  uint8_t checkHardwareSwitch(SensorManager& sensors, NodePrefs& prefs) {
#ifdef PIN_GPS_SWITCH
    uint8_t ev = EV_NONE;
    bool sw = hardwareSwitchOn();
    if (!_sw_init) { _sw_init = true; _sw_last = sw; }
    if (sw && !_sw_last && !prefs.gps_enabled) {
      prefs.gps_enabled = 1;
      ev = EV_PREFS_CHANGED;
    }
    _sw_last = sw;
    if (!sw) {
      const char* v = sensors.getSettingByKey("gps");
      if (v != NULL && strcmp(v, "1") == 0) sensors.setSettingValue("gps", "0");
    }
    return ev;
#else
    (void)sensors; (void)prefs;
    return EV_NONE;
#endif
  }
};
