#pragma once

// FieldMesh v6.1: GPS duty cycle (power saving), modelled on Meshtastic (src/gps/GPS.cpp,
// GPSUpdateScheduling.cpp) - see RECHERCHE_GPS_Power_M1.md and PLAN_v6.1.md.
//
// While GPS is enabled the receiver is not kept on all the time any more:
//   - every 2 min a search for a fix, then the fix is held for 20 s, then the GPS sleeps
//   - the wake-up is moved forward by the learned search time (EWMA 0.2), so a new fix is
//     available about every 2 min; no long back-off when there is no fix
//   - search limits: first search after GPS start 180 s, after a fix 30 s
//   - with GSV data (sky check) the search after a failure is graded by what the receiver
//     sees - "strong" = satellite with >= 25 dB-Hz, counted per one-second epoch:
//       no strong satellite                  -> 15 s, then give up (indoors)
//       1..3 strong                          -> up to 30 s (a fix needs 4; 3 such searches
//                                               in a row, then 15 s - window case)
//       >= 4 strong at any time of the search -> up to 120 s: the sky is there and only
//       and >= 2 of them >= 30 dB-Hz            time is missing (expired ephemeris needs
//                                               18..50 s of uninterrupted reception, and
//                                               decoding gets unreliable below ~27 dB-Hz)
//     A longer search ends at once with the fix, or when no strong satellite was seen for
//     15 s (back indoors). After a failed 120 s search the next one is allowed after 4, then
//     8, then 16 min (back to 4 after a fix or 30 min without failed 120 s search); in
//     between >= 4 strong is handled like 1..3. The count of 30 s searches is only cleared
//     by a fix or after 30 min.
//     Field test T1000-E 22.10. at an office window: 4..5 satellites with 25..32 dB-Hz (at
//     most 1 >= 30) never gave a fix, but the 120 s stage kept the GPS on ~65 % of the time.
//     Field test T1000-E 22.10. (urban canyon): 15 s searches with 4..11 strong satellites
//     never gave a fix, the first one came after 1 h 48 min.
//   - without GSV data: 30 s, after 3 failures 15 s, never less than twice the learned
//     search time, a blind 120 s search on every 5th failure
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
// All satellite systems the receiver can use are switched on (like Meshtastic):
//   L76K   $PCAS04,7 = GPS + BeiDou + GLONASS (all it supports), once per GPS start
//   Airoha $PAIR066,1,1,1,1,1,0 = GPS + GLONASS + Galileo + BeiDou + QZSS. The receiver
//          restarts on this command, so it is only sent when the query $PAIR067 (once per
//          boot) shows a system switched off; then saved with $PAIR513. Dual band (L1 + L5,
//          e.g. MeshTracker X1) is the receiver's own default and is not touched.
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
  static constexpr uint32_t SEARCH_SHORT_MS    = 15000;   // after a failure (without GSV: after 3)
  static constexpr uint32_t SEARCH_MID_MS      = 30000;   // 1..3 strong satellites seen
  static constexpr uint8_t  MID_MAX            = 3;       // 30 s searches in a row without fix
  static constexpr uint32_t SEARCH_LONG_MS     = 120000;  // >= 4 strong (without GSV: every 5th)
  static constexpr uint32_t MAX_PREDICTED_MS   = 60000;   // cap of the learned search time
  static constexpr uint32_t KEEP_AWAKE_PAGE_MS = 60000;   // GPS/Tracking page: +60 s after leaving
  static constexpr uint32_t KEEP_AWAKE_SOS_MS  = 600000;  // 10 min after sending SOS
  static constexpr uint32_t POLL_MS            = 250;
  static constexpr uint8_t  STRONG_SNR_DBHZ    = 25;      // signal good enough to decode data
  static constexpr uint8_t  STRONG_MIN_SATS    = 4;
  static constexpr uint8_t  GOOD_SNR_DBHZ      = 30;      // decodes new ephemeris reliably
  static constexpr uint8_t  GOOD_MIN_SATS      = 2;       // needed in one epoch for the 120 s stage
  static constexpr uint8_t  USABLE_SNR_DBHZ    = 20;      // diagnostics only (log column s20): enough
                                                          // for a fix with valid ephemeris?
  static constexpr uint32_t EXT_LOST_MS        = 15000;   // end a longer search when no strong satellite
  static constexpr uint32_t EXT_BLOCK_MIN_MS   = 240000;  // after a failed 120 s search: 4, 8, 16 min
  static constexpr uint8_t  EXT_BLOCK_STEPS    = 3;
  static constexpr uint32_t BACKOFF_RESET_MS   = 1800000; // forget the back-off after 30 min
  static constexpr uint32_t GSV_VALID_MS       = 900000;  // GSV seen recently -> sky check usable

  // why the last search ended (diagnostics)
  enum EndReason : uint8_t { END_FIX = 0, END_LIMIT, END_MID, END_LONG, END_LOST };

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
          if (_fail_count == 0 && !_extended && !_mid) learnSearchTime(_last_search_ms);
          else if (_predicted_ms == 0) learnSearchTime(_last_search_ms);
          _fail_count = 0;
          _first_search = false;
          _ext_level = 0;      // the sky works here again: no back-off
          _mid_count = 0;
          _end_reason = END_FIX;
          setState(HOLDING, now);
        } else if (now - _since >= _search_limit) {
          switch (nextStage(now)) {
            case STAGE_LONG:
              _extended = true;
              _search_limit = SEARCH_LONG_MS;
              ev |= EV_SEARCH_EXTENDED;
              break;
            case STAGE_MID:
              _mid = true;
              _search_limit = SEARCH_MID_MS;
              break;
            default:
              ev |= failSearch(sensors, loc, now, forced,
                               _extended ? END_LONG : (_mid ? END_MID : END_LIMIT));
              break;
          }
        } else if ((_extended || _mid) && (uint32_t)(now - _any_strong_ms) > EXT_LOST_MS) {
          ev |= failSearch(sensors, loc, now, forced, END_LOST);   // sky gone again (back indoors)
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
  uint8_t searchMaxGood() const { return _state == SEARCHING && _extended ? _search_max_good : _done_max_good; }   // >= 30 dB-Hz
  uint8_t searchMaxUsable() const { return _state == SEARCHING && _extended ? _search_max_usable : _done_max_usable; }   // >= 20 dB-Hz
  uint8_t endReason() const { return _end_reason; }   // EndReason of the last search
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
  // receiver configuration for the diagnostic log: bit 0 $PAIR062 acknowledged ok,
  // 1 $PAIR067 answered, 2 all satellite systems on, 3 $PAIR066 sent, 4 $PAIR066 ok,
  // 5 $PAIR513 ok, 6 $PCAS04 sent (L76K)
  uint8_t configFlags() const {
    return (_ack062 == 0 ? 0x01 : 0) | _gnss_flags | (_ack066 == 0 ? 0x10 : 0) | (_ack513 == 0 ? 0x20 : 0);
  }

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
  bool     _extended = false;      // current search was extended to SEARCH_LONG_MS
  bool     _mid = false;           // current search was extended to SEARCH_MID_MS
  uint32_t _gsv_ms = 0;            // last GSV sentence
  uint32_t _any_strong_ms = 0;     // last epoch with at least one strong satellite
  uint32_t _ext_block_until = 0;
  uint32_t _ext_fail_ms = 0;       // last failed 120 s search
  uint32_t _mid_fail_ms = 0;       // last failed 30 s search
  uint8_t  _ext_level = 0;         // failed 120 s searches in a row (back-off 4/8/16 min)
  uint8_t  _mid_count = 0;         // failed 30 s searches in a row
  uint8_t  _end_reason = END_FIX;
  uint8_t  _search_max_strong = 0;
  uint8_t  _search_max_good = 0;
  uint8_t  _search_max_usable = 0;
  uint8_t  _epoch_n = 0;           // satellites >= USABLE_SNR_DBHZ collected in the current epoch
  uint16_t _epoch_ids[32];         // (constellation, PRN) of those, to count each only once
  uint8_t  _epoch_snr[32];         // their best SNR in this epoch
  uint8_t  _cfg_step = 0;       // NMEA configuration: 0 = nothing to do
  uint32_t _cfg_due = 0;        // fallback: send even without a sentence from the receiver
  bool     _cfg_saved = false;  // Airoha: $PAIR513 sent this boot
  uint8_t  _gnss_flags = 0;     // satellite systems: see configFlags()
  uint8_t  _ack066 = 0xFF;
  bool     _talked = false;     // valid sentence received since the last start/wake
  uint8_t  _types = 0;
  uint8_t  _ack062 = 0xFF, _ack513 = 0xFF;
  uint16_t _search_gsv = 0;
  uint8_t  _search_best_snr = 0;
  // snapshot at the end of a search: a failed search >= 2 min restarts the next one at once,
  // which resets the running counters before the log reads them
  uint16_t _done_gsv = 0;
  uint8_t  _done_best_snr = 0, _done_max_strong = 0, _done_max_good = 0, _done_max_usable = 0;
  void snapshotSearch() {
    _done_gsv = _search_gsv; _done_best_snr = _search_best_snr;
    _done_max_strong = _search_max_strong; _done_max_good = _search_max_good;
    _done_max_usable = _search_max_usable;
  }
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
    // with GSV: short after a failure, nextStage() makes it longer when there is signal
    if (gsvAvailable()) return _fail_count == 0 ? SEARCH_MS : SEARCH_SHORT_MS;
    uint32_t limit;
    if (_fail_count == 0) limit = SEARCH_MS;
    else if (_fail_count % 5 == 0 && !gsvAvailable()) limit = SEARCH_LONG_MS;   // blind fallback
    else if (_fail_count >= 3) limit = SEARCH_SHORT_MS;
    else limit = SEARCH_MS;
    // receivers that need longer per fix (e.g. no backup power during hard sleep) get at
    // least twice their learned search time, otherwise nearly every search would fail.
    // (With GSV the graded search below decides instead - field test: 24.8 s instead of 15 s
    // kept the GPS on 36 % of a night indoors.)
    if (limit < 2 * _predicted_ms) limit = 2 * _predicted_ms;
    return limit;
  }

  void startSearch(uint32_t now) {
    _search_limit = searchLimit();
    _extended = false;
    _mid = false;
    _search_max_strong = 0;
    _search_max_good = 0;
    _search_max_usable = 0;
    _search_gsv = 0;
    _search_best_snr = 0;
    _epoch_n = 0;   // do not carry the last epoch of the previous search over
    _any_strong_ms = now;   // "no strong satellite for 15 s" counts from the start
    setState(SEARCHING, now);
  }

  enum Stage : uint8_t { STAGE_NONE = 0, STAGE_MID, STAGE_LONG };

  // search time is up without fix: search longer? (only with GSV data)
  Stage nextStage(uint32_t now) {
    if (_extended || !gsvAvailable() || _search_max_strong == 0) return STAGE_NONE;
    if ((uint32_t)(now - _any_strong_ms) > EXT_LOST_MS) return STAGE_NONE;   // sky gone again
    if (_ext_level > 0 && (uint32_t)(now - _ext_fail_ms) > BACKOFF_RESET_MS) _ext_level = 0;
    if (_mid_count > 0 && (uint32_t)(now - _mid_fail_ms) > BACKOFF_RESET_MS) _mid_count = 0;
    if (_search_max_strong >= STRONG_MIN_SATS && _search_max_good >= GOOD_MIN_SATS
        && _search_limit < SEARCH_LONG_MS
        && (_ext_level == 0 || (int32_t)(now - _ext_block_until) >= 0))
      return STAGE_LONG;
    if (!_mid && _search_limit < SEARCH_MID_MS && _mid_count < MID_MAX) return STAGE_MID;
    return STAGE_NONE;
  }

  // search ended without fix: keep trying every 2 min, counted from the start of this search
  uint8_t failSearch(SensorManager& sensors, LocationProvider* loc, uint32_t now, bool forced, uint8_t reason) {
    _last_search_ms = now - _since;
    snapshotSearch();
    _fail_count++;
    _first_search = false;
    _end_reason = reason;
    if (_extended) {   // sky but no fix after up to 120 s -> back-off 4, 8, 16 min
      if (_ext_level < EXT_BLOCK_STEPS) _ext_level++;
      _ext_block_until = now + (EXT_BLOCK_MIN_MS << (_ext_level - 1));
      _ext_fail_ms = now;
    } else if (_mid) {
      if (_mid_count < 255) _mid_count++;
      _mid_fail_ms = now;
    }
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
      else if (cmd == 66) _ack066 = (uint8_t)res;
      else if (cmd == 513) _ack513 = (uint8_t)res;
      _types |= 0x80;
      return;
    }
    if (strncmp(s, "$PAIR067,", 9) == 0) {       // Airoha satellite systems: GPS,GLONASS,Galileo,BDS,QZSS,..
      _gnss_flags |= GNSS_ANSWERED;
      bool all = true;
      // QZSS (field 5) is not checked: some receivers keep it fixed, a mismatch there would
      // restart the receiver on every boot
      for (int i = 1; i <= 4; i++) if (nmeaField(s, i) <= 0) all = false;
      if (all) _gnss_flags |= GNSS_ALL_ON;
      else _gnss_flags &= ~GNSS_ALL_ON;
      _types |= 0x80;
      return;
    }
    const char* type = &s[3];
    static const char* const names[7] = { "GGA", "RMC", "GSV", "GSA", "GLL", "VTG", "ZDA" };
    uint8_t bit = 0x80;
    for (uint8_t i = 0; i < 7; i++) if (strncmp(type, names[i], 3) == 0) { bit = 1 << i; break; }
    _types |= bit;
    if (strncmp(type, "GGA", 3) == 0) {          // GGA starts a new epoch (one per second)
      if (_state == SEARCHING) {
        uint8_t strong = 0, good = 0;
        for (uint8_t k = 0; k < _epoch_n; k++) {
          if (_epoch_snr[k] >= STRONG_SNR_DBHZ) strong++;
          if (_epoch_snr[k] >= GOOD_SNR_DBHZ) good++;
        }
        if (strong > 0) _any_strong_ms = millis();
        if (strong > _search_max_strong) _search_max_strong = strong;
        if (good > _search_max_good) _search_max_good = good;
        if (_epoch_n > _search_max_usable) _search_max_usable = _epoch_n;
      }
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
      if (snr < USABLE_SNR_DBHZ) continue;
      // a satellite can be listed more than once (several signals): count it once, best SNR
      uint16_t id = (sys << 8) | (uint16_t)(prn & 0xFF);
      bool dup = false;
      for (uint8_t k = 0; k < _epoch_n; k++) {
        if (_epoch_ids[k] == id) {
          if (snr > _epoch_snr[k]) _epoch_snr[k] = (uint8_t)snr;
          dup = true;
          break;
        }
      }
      if (!dup && _epoch_n < sizeof(_epoch_ids) / sizeof(_epoch_ids[0])) {
        _epoch_ids[_epoch_n] = id;
        _epoch_snr[_epoch_n++] = (uint8_t)(snr > 255 ? 255 : snr);
      }
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

  enum : uint8_t { GNSS_ANSWERED = 0x02, GNSS_ALL_ON = 0x04, GNSS_SET_SENT = 0x08, GNSS_PCAS04 = 0x40 };

  void runNmeaConfig(LocationProvider* loc, uint32_t now) {
    if (_cfg_step == 0 || _state == SLEEPING) return;
    if (!_talked && (int32_t)(now - _cfg_due) < 0) return;
#if defined(PIN_GPS_STANDBY)
    // soft sleep keeps the receiver's settings: this runs once per GPS start
    loc->sendSentence("$PCAS04,7");                            // L76K: GPS + BeiDou + GLONASS
    loc->sendSentence("$PCAS03,1,0,0,1,1,0,0,0,0,0,,,0,0");   // L76K: GGA + GSV + RMC
    _gnss_flags |= GNSS_PCAS04;
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
      loc->sendSentence("$PAIR067");       // which satellite systems are on? (once per boot)
      _cfg_step = 2;
      _cfg_due = now + 1000;               // answer or give up after 1 s
      _talked = false;   // step 2 waits only for the answer / the time below
    } else if (!(_gnss_flags & GNSS_ANSWERED) && (int32_t)(now - _cfg_due) < 0) {
      return;                              // wait for the $PAIR067 answer
    } else if ((_gnss_flags & GNSS_ANSWERED) && !(_gnss_flags & GNSS_ALL_ON)
               && !(_gnss_flags & GNSS_SET_SENT)) {
      // switch everything on - the receiver restarts, so configure the output again once it
      // talks (step 1), then check once more and save. Sent at most once per boot.
      loc->sendSentence("$PAIR066,1,1,1,1,1,0");   // GPS, GLONASS, Galileo, BeiDou, QZSS
      _gnss_flags |= GNSS_SET_SENT;
      _gnss_flags &= ~GNSS_ANSWERED;
      _cfg_step = 1;
      _cfg_due = now + 5000;
      _talked = false;
    } else {
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
