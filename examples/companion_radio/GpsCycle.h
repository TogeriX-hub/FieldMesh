#pragma once

// FieldMesh v6.1: GPS duty cycle (power saving), modelled on Meshtastic (src/gps/GPS.cpp,
// GPSUpdateScheduling.cpp) - see RECHERCHE_GPS_Power_M1.md and PLAN_v6.1.md.
//
// While GPS is enabled the receiver is not kept on all the time any more:
//   - every 2 min a search for a fix, then the fix is held for 20 s, then the GPS sleeps
//   - the wake-up is moved forward by the learned search time (EWMA 0.2), so a new fix is
//     available about every 2 min; no long back-off when there is no fix
//   - search limits: first search after GPS start 180 s, then 30 s, after 3 failures 15 s;
//     without GSV data never less than twice the learned search time
//   - sky check from the GSV sentences: when the search time is up without fix but >= 4
//     satellites with >= 25 dB-Hz are in view, the sky is there and only time is missing
//     (e.g. ephemeris expired after hours indoors) -> extend the search up to 120 s. The
//     extension ends early when the strong satellites are gone for 15 s; after a failed
//     extension there is none for 10 min (window case: signal but no fix possible).
//     Receivers without GSV data fall back to a blind 120 s search on every 5th failure.
//   - "keep awake" requests (GPS/Tracking page on screen, SOS) keep the GPS running
//
// Sleep mode is chosen centrally from the board defines, like Meshtastic:
//   PIN_GPS_STANDBY defined -> soft sleep: only the standby pin (L76K: M1, Wio L1 family),
//                              power and reset stay untouched
//   otherwise               -> hard sleep: the board's own stop_gps()/start_gps() via the
//                              "gps" setting (Airoha RTC backup on T1000-E/X1, power off on
//                              Heltec T1)
// Boards that cannot switch their GPS off (no standby/enable pin, e.g. T-Beam with PMU,
// T-Deck, Station G2/G3, XIAO C3) keep it running as in v6.0; the cycle then only tracks
// fix freshness and reports a position about every 2 min.
// Board files are not modified. Without provider or "gps" setting the cycle stays off.
//
// NMEA output is reduced to GGA + RMC + GSV (GSV for the sky check), only for the two known
// receiver families: L76K ($PCAS03) and Airoha ($PAIR062, saved once per boot with $PAIR513).
// Sent after every GPS start/wake as soon as the receiver talks (first valid sentence), not
// relying on the receiver keeping it - field test T1000-E 21.10.: a once-per-boot command
// never showed any GSV (receiver may have "GSV off" saved, e.g. by other firmware).
// Airoha acknowledgements ($PAIR001) are kept for the diagnostic log.
// Other receivers are left untouched.
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
    EV_SEARCH_EXTENDED = 0x10, // search extended: sky visible, but no fix yet
  };

  static constexpr uint32_t INTERVAL_MS        = 120000;  // fix every 2 min (Meshtastic default)
  static constexpr uint32_t HOLD_MS            = 20000;   // keep running 20 s after a fix
  static constexpr uint32_t MAX_FIX_AGE_MS     = 120000;  // never use an older position
  // wake a bit early -> fix age stays < 2 min (field logs with 5 s: 102..126 s before the next fix)
  static constexpr uint32_t WAKE_MARGIN_MS     = 12000;
  static constexpr uint32_t SEARCH_FIRST_MS    = 180000;  // first search after GPS start
  static constexpr uint32_t SEARCH_MS          = 30000;
  static constexpr uint32_t SEARCH_SHORT_MS    = 15000;   // after 3 failed searches
  static constexpr uint32_t SEARCH_LONG_MS     = 120000;  // every 5th failed search
  static constexpr uint32_t MAX_PREDICTED_MS   = 60000;   // cap of the learned search time
  static constexpr uint32_t KEEP_AWAKE_PAGE_MS = 60000;   // GPS/Tracking page: +60 s after leaving
  static constexpr uint32_t KEEP_AWAKE_SOS_MS  = 600000;  // 10 min after sending SOS
  static constexpr uint32_t POLL_MS            = 250;
  static constexpr uint8_t  STRONG_SNR_DBHZ    = 25;      // signal good enough to decode data
  static constexpr uint8_t  STRONG_MIN_SATS    = 4;
  static constexpr uint32_t STRONG_RECENT_MS   = 3000;    // "sky visible now"
  static constexpr uint32_t EXT_LOST_MS        = 15000;   // end an extension when sky is gone
  static constexpr uint32_t EXT_BLOCK_MS       = 600000;  // no extension 10 min after a failed one
  static constexpr uint32_t GSV_VALID_MS       = 900000;  // GSV seen recently -> sky check usable

  GpsCycle() { }

  // call from MyMesh::loop(); returns a mask of Event
  uint8_t loop(SensorManager& sensors, NodePrefs& prefs) {
    _last_ev = step(sensors, prefs);
    return _last_ev;
  }
  uint8_t lastEvents() const { return _last_ev; }   // events of the last loop() (diagnostics)

private:
  uint8_t step(SensorManager& sensors, NodePrefs& prefs) {
    uint8_t ev = EV_NONE;
    uint32_t now = millis();
    LocationProvider* loc = sensors.getLocationProvider();
    if (loc != NULL && !_cb_set) {
      loc->setSentenceCallback(onSentence, this);   // GSV sky check
      _cb_set = true;
    }

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
#ifdef PIN_GPS_STANDBY
      // boards with a standby pin separate from the power/enable pin (ThinkNode M6/M8/M9,
      // Heltec Tower V2, T-Echo Lite/Card, SenseCAP Solar): the board's start_gps() only
      // switches power, so a standby pin left in sleep (GPS switched off while the cycle
      // slept, or a variant init) would keep the receiver asleep for good
      pinMode(PIN_GPS_STANDBY, OUTPUT);
      digitalWrite(PIN_GPS_STANDBY, HIGH);
#endif
      _first_search = true;
      _fail_count = 0;
      startSearch(now);
      scheduleNmeaConfig(now);
    }
    runNmeaConfig(loc, now);

    bool new_fix = pollFix(loc, now);
    bool forced = (int32_t)(_awake_until - now) > 0 || !canSleep();

    if (_sos_pending) {
      if (new_fix) { ev |= EV_SOS_RESEND; _sos_pending = false; }
      else if ((int32_t)(now - _sos_until) > 0) _sos_pending = false;
    }

    switch (_state) {
      case SEARCHING:
        if (new_fix) {
          _last_search_ms = now - _since;
          snapshotSearch();
          // a search after failures (e.g. new ephemeris after hours indoors) is no typical
          // search time - learning it would make every following search longer. Exception:
          // nothing learned yet (receivers that always need long, e.g. without backup power)
          if (_fail_count == 0 && !_extended) learnSearchTime(_last_search_ms);
          else if (_predicted_ms == 0) learnSearchTime(_last_search_ms);
          _fail_count = 0;
          _first_search = false;
          setState(HOLDING, now);
        } else if (now - _since >= _search_limit) {
          if (canExtend(now)) {
            _extended = true;
            _search_limit = SEARCH_LONG_MS;
            ev |= EV_SEARCH_EXTENDED;
          } else {
            ev |= failSearch(sensors, loc, now, forced);
          }
        } else if (_extended && (uint32_t)(now - _strong_ms) > EXT_LOST_MS) {
          ev |= failSearch(sensors, loc, now, forced);   // sky gone again (back indoors)
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
          if (_hard) scheduleNmeaConfig(now);   // receiver was powered down: configure again
          if (forced) { setState(AWAKE, now); _next_awake_ev = now; }
          else startSearch(now);
        }
        break;

      default:
        break;
    }
    return ev;
  }

public:
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
  uint32_t lastSearchMs() const { return _last_search_ms; }   // duration of the last search
  uint8_t searchMaxStrong() const { return _state == SEARCHING && _extended ? _search_max_strong : _done_max_strong; }
  uint32_t searchElapsedMs() const { return _state == SEARCHING ? (uint32_t)(millis() - _since) : 0; }
  bool gsvAvailable() const { return _gsv_ever && (uint32_t)(millis() - _gsv_ms) < GSV_VALID_MS; }
  // diagnostics (GpsLog): GSV sentences and best SNR in the current/last search, sentence types
  // seen since the last wake (bit 0 GGA, 1 RMC, 2 GSV, 3 GSA, 4 GLL, 5 VTG, 6 ZDA, 7 other),
  // last Airoha acknowledgements for $PAIR062 / $PAIR513 (0 = ok, 0xFF = none)
  // values of the search that just ended (FIX/FAIL), or of the running one (EXT)
  uint16_t searchGsvCount() const { return _state == SEARCHING && _extended ? _search_gsv : _done_gsv; }
  uint8_t searchBestSnr() const { return _state == SEARCHING && _extended ? _search_best_snr : _done_best_snr; }
  uint8_t sentenceTypes() const { return _types; }
  uint8_t ack062() const { return _ack062; }
  uint8_t ack513() const { return _ack513; }

  // can this board put its GPS to sleep at all (standby pin, enable/power pin, Airoha
  // backup, RAK WisBlock IO pin)? Pins defined as -1 do not count.
  static bool canSleep() {
#if defined(PIN_GPS_STANDBY) || defined(GPS_VRTC_EN) || defined(RAK_WISBLOCK_GPS)
    return true;
#else
    int en = -1;
  #if defined(PIN_GPS_EN)
    en = (int)(PIN_GPS_EN);
  #elif defined(GPS_EN)
    en = (int)(GPS_EN);
  #endif
    return en >= 0;
#endif
  }

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
  uint32_t _last_search_ms = 0;
  uint8_t  _last_ev = EV_NONE;
  // GSV sky check
  bool     _cb_set = false;
  bool     _gsv_ever = false;
  bool     _extended = false;      // current search was extended
  uint32_t _gsv_ms = 0;            // last GSV sentence
  uint32_t _strong_ms = 0;         // last epoch with >= STRONG_MIN_SATS strong satellites
  uint32_t _ext_block_until = 0;
  uint8_t  _search_max_strong = 0;
  uint8_t  _epoch_n = 0;           // strong satellites collected in the current epoch
  uint16_t _epoch_ids[24];         // (constellation, PRN) of those, to count each only once
  uint8_t  _cfg_step = 0;       // NMEA configuration: 0 = nothing to do
  uint32_t _cfg_due = 0;        // fallback: send even without a sentence from the receiver
  bool     _cfg_saved = false;  // Airoha: $PAIR513 sent this boot
  bool     _talked = false;     // valid sentence received since the last start/wake
  uint8_t  _types = 0;
  uint8_t  _ack062 = 0xFF, _ack513 = 0xFF;
  uint16_t _search_gsv = 0;
  uint8_t  _search_best_snr = 0;
  // snapshot at the end of a search: a failed search >= 2 min restarts the next one at once,
  // which resets the running counters before the log reads them
  uint16_t _done_gsv = 0;
  uint8_t  _done_best_snr = 0, _done_max_strong = 0;
  void snapshotSearch() { _done_gsv = _search_gsv; _done_best_snr = _search_best_snr; _done_max_strong = _search_max_strong; }
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
    else if (_fail_count % 5 == 0 && !gsvAvailable()) limit = SEARCH_LONG_MS;   // blind fallback
    else if (_fail_count >= 3) limit = SEARCH_SHORT_MS;
    else limit = SEARCH_MS;
    // receivers that need longer per fix (e.g. no backup power during hard sleep) get at
    // least twice their learned search time, otherwise nearly every search would fail.
    // With GSV the sky check extends searches that need longer, so short searches stay short
    // indoors (field test: 24.8 s instead of 15 s kept the GPS on 36 % of the night)
    if (!gsvAvailable() && limit < 2 * _predicted_ms) limit = 2 * _predicted_ms;
    return limit;
  }

  void startSearch(uint32_t now) {
    _search_limit = searchLimit();
    _extended = false;
    _search_max_strong = 0;
    _search_gsv = 0;
    _search_best_snr = 0;
    setState(SEARCHING, now);
  }

  bool canExtend(uint32_t now) const {
    return !_extended && _search_limit < SEARCH_LONG_MS
        && _strong_ms != 0 && (uint32_t)(now - _strong_ms) < STRONG_RECENT_MS
        && (int32_t)(now - _ext_block_until) >= 0;
  }

  // search ended without fix: keep trying every 2 min, counted from the start of this search
  uint8_t failSearch(SensorManager& sensors, LocationProvider* loc, uint32_t now, bool forced) {
    _last_search_ms = now - _since;
    snapshotSearch();
    _fail_count++;
    _first_search = false;
    if (_extended) _ext_block_until = now + EXT_BLOCK_MS;   // sky but no fix -> pause extensions
    uint32_t elapsed = now - _since;
    uint32_t wait = (elapsed < INTERVAL_MS) ? INTERVAL_MS - elapsed : 0;
    goSleep(sensors, loc, now, forced, wait);
    return EV_SEARCH_FAILED;
  }

  // ---- GSV sky check ------------------------------------------------------------------------
  static void onSentence(const char* sentence, void* ctx) { ((GpsCycle*)ctx)->handleSentence(sentence); }

  // field i (0 = "$xxGSV") of an NMEA sentence as integer, -1 if empty/missing
  static int nmeaField(const char* s, int idx) {
    int i = 0;
    while (*s && *s != '*' && i < idx) { if (*s == ',') i++; s++; }
    if (i != idx || *s == ',' || *s == '*' || *s == 0) return -1;
    int v = 0; bool any = false;
    while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); s++; any = true; }
    return any ? v : -1;
  }

  void handleSentence(const char* s) {
    if (s[0] != '$' || strlen(s) < 7) return;
    _talked = true;
    if (strncmp(s, "$PAIR001,", 9) == 0) {       // Airoha acknowledgement: $PAIR001,<cmd>,<result>
      int cmd = nmeaField(s, 1), res = nmeaField(s, 2);
      if (cmd == 62) _ack062 = (uint8_t)res;
      else if (cmd == 513) _ack513 = (uint8_t)res;
      _types |= 0x80;
      return;
    }
    const char* type = &s[3];
    static const char* const names[7] = { "GGA", "RMC", "GSV", "GSA", "GLL", "VTG", "ZDA" };
    uint8_t bit = 0x80;
    for (uint8_t i = 0; i < 7; i++) if (strncmp(type, names[i], 3) == 0) { bit = 1 << i; break; }
    _types |= bit;
    if (strncmp(type, "GGA", 3) == 0) {          // GGA starts a new epoch (one per second)
      if (_epoch_n >= STRONG_MIN_SATS) _strong_ms = millis();
      if (_state == SEARCHING && _epoch_n > _search_max_strong) _search_max_strong = _epoch_n;
      _epoch_n = 0;
      return;
    }
    if (strncmp(type, "GSV", 3) != 0) return;
    _gsv_ever = true;
    _gsv_ms = millis();
    if (_state == SEARCHING && _search_gsv < 0xFFFF) _search_gsv++;
    uint16_t sys = ((uint8_t)s[1] * 31u + (uint8_t)s[2]) & 0xFF;   // talker = constellation
    // satellites in groups of 4 fields from field 4: PRN, elevation, azimuth, SNR
    for (int g = 0; g < 4; g++) {
      int prn = nmeaField(s, 4 + g * 4);
      int snr = nmeaField(s, 7 + g * 4);
      if (prn < 0) break;
      if (_state == SEARCHING && snr > _search_best_snr) _search_best_snr = (uint8_t)snr;
      if (snr < STRONG_SNR_DBHZ) continue;
      uint16_t id = (sys << 8) | (uint16_t)(prn & 0xFF);
      bool dup = false;
      for (uint8_t k = 0; k < _epoch_n; k++) if (_epoch_ids[k] == id) { dup = true; break; }
      if (!dup && _epoch_n < sizeof(_epoch_ids) / sizeof(_epoch_ids[0])) _epoch_ids[_epoch_n++] = id;
    }
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
    _talked = false;
    _types = 0;
  }

  // NMEA reduction (Meshtastic GPS.cpp): GGA + RMC, plus GSV for the sky check.
  // Sent as soon as the receiver talks after start/wake (it listens then), at the latest 5 s
  // after start.
  void scheduleNmeaConfig(uint32_t now) {
#if defined(PIN_GPS_STANDBY) || defined(GPS_VRTC_EN)
    _cfg_step = 1;
#endif
    _talked = false;
    _types = 0;
    _cfg_due = now + 5000;
  }

  void runNmeaConfig(LocationProvider* loc, uint32_t now) {
    if (_cfg_step == 0 || _state == SLEEPING) return;
    if (!_talked && (int32_t)(now - _cfg_due) < 0) return;
#if defined(PIN_GPS_STANDBY)
    loc->sendSentence("$PCAS03,1,0,0,1,1,0,0,0,0,0,,,0,0");   // L76K: GGA + GSV + RMC
    _cfg_step = 0;
#elif defined(GPS_VRTC_EN)
    if (_cfg_step == 1) {
      loc->sendSentence("$PAIR062,0,1");   // GGA on
      loc->sendSentence("$PAIR062,1,0");   // GLL off
      loc->sendSentence("$PAIR062,2,0");   // GSA off
      loc->sendSentence("$PAIR062,3,1");   // GSV on (sky check)
      loc->sendSentence("$PAIR062,4,1");   // RMC on
      loc->sendSentence("$PAIR062,5,0");   // VTG off
      loc->sendSentence("$PAIR062,6,0");   // ZDA off
      if (_cfg_saved) { _cfg_step = 0; return; }
      _cfg_step = 2;
      _cfg_due = now + 300;
      _talked = false;   // step 2 waits only for the time below
    } else if ((int32_t)(now - _cfg_due) >= 0) {
      loc->sendSentence("$PAIR513");       // save configuration (once per boot)
      _cfg_step = 0;
      _cfg_saved = true;
    }
#else
    (void)loc;
    _cfg_step = 0;
#endif
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
