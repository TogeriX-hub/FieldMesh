#pragma once

// FieldMesh v6.1: send cadence for active tracking (replaces the fixed 5-min GPS advert,
// issue #28). Smart-position idea from Meshtastic, values decided in PLAN_v6.1.md:
//   - first send at the first usable fix after tracking/GPS was switched on
//   - moved >= min_dist since the last sent position and >= min_interval since the last send
//   - otherwise a heartbeat every heartbeat_ms; when no fresh fix (<= 2 min) is available at
//     that moment it waits for the next fix (at most HEARTBEAT_WAIT_MS), then sends with or
//     without position
// Positions are evaluated at the end of a GPS hold phase (GpsCycle::EV_FIX), when the
// solution has settled. A fix with fewer than MIN_SATS satellites is not used for the
// movement check.
//
// Kept independent of adverts so the same cadence can drive team positions (#TEL) later;
// TEAM's interval / min_dist map to heartbeat / min_dist.

#include <Arduino.h>
#include <math.h>

class TrackScheduler {
public:
  static constexpr uint16_t DEFAULT_MIN_DIST_M      = 100;
  static constexpr uint32_t DEFAULT_MIN_INTERVAL_MS = 120000;   // 2 min
  static constexpr uint32_t DEFAULT_HEARTBEAT_MS    = 900000;   // 15 min
  static constexpr uint8_t  MIN_SATS                = 4;
  // fixes arrive about every 115 s (GpsCycle wakes a bit early) - without slack a moving
  // node would only send every second fix
  static constexpr uint32_t INTERVAL_SLACK_MS       = 15000;
  // field logs: just before the next fix the last one is 102..126 s old - a heartbeat due in
  // those few seconds would go out without position, so it waits for the next fix instead
  static constexpr uint32_t HEARTBEAT_WAIT_MS       = 150000;

  enum Reason : uint8_t { NONE = 0, FIRST, MOVED, HEARTBEAT };

  void configure(uint16_t min_dist_m, uint32_t min_interval_ms, uint32_t heartbeat_ms) {
    _min_dist_m = min_dist_m;
    _min_interval_ms = min_interval_ms;
    _heartbeat_ms = heartbeat_ms;
  }

  // forget the history (tracking switched on): the next usable fix is sent right away,
  // the heartbeat counts from now
  void reset(uint32_t now) {
    _last_sent_ms = now;
    _have_sent = false;
    _have_sent_pos = false;
    _last_dist_m = 0;
  }

  // call every loop while tracking is active.
  // fix_event: a settled position is available now (lat/lon in degrees * 1e6, sats)
  // fresh: a position <= 2 min old is available (it would be sent with the heartbeat)
  // returns why a send is due now (NONE = nothing to send)
  Reason poll(uint32_t now, bool fix_event, int32_t lat, int32_t lon, uint8_t sats, bool fresh) {
    bool usable = fix_event && sats >= MIN_SATS;
    uint32_t since = now - _last_sent_ms;

    if (usable) {
      if (!_have_sent || !_have_sent_pos) return FIRST;
      _last_dist_m = distanceM(_sent_lat, _sent_lon, lat, lon);
      if (_last_dist_m >= _min_dist_m && since + INTERVAL_SLACK_MS >= _min_interval_ms) return MOVED;
    }
    // also without a usable fix: report presence once per heartbeat - with position if one is
    // fresh now, otherwise after the next fix or at the latest HEARTBEAT_WAIT_MS later
    if (since >= _heartbeat_ms && (fresh || fix_event || since >= _heartbeat_ms + HEARTBEAT_WAIT_MS))
      return HEARTBEAT;
    return NONE;
  }

  // after sending; with_pos: the sent message carried this position
  void sent(uint32_t now, bool with_pos, int32_t lat, int32_t lon) {
    _have_sent = true;
    _last_sent_ms = now;
    if (with_pos) {
      _have_sent_pos = true;
      _sent_lat = lat;
      _sent_lon = lon;
    }
  }

  uint16_t lastDistanceM() const { return _last_dist_m; }

  // equirectangular approximation - accurate enough for distances up to some km
  static uint16_t distanceM(int32_t lat1, int32_t lon1, int32_t lat2, int32_t lon2) {
    const float k = 0.0174532925f / 1000000.0f;   // (deg * 1e6) -> rad
    // differences from the integer values, so float precision only limits the cosine term
    float x = (float)(lon2 - lon1) * k * cosf(((float)lat1 + (float)lat2) * 0.5f * k);
    float y = (float)(lat2 - lat1) * k;
    float d = sqrtf(x * x + y * y) * 6371000.0f;
    return d > 65535.0f ? 65535 : (uint16_t)d;
  }

private:
  uint16_t _min_dist_m = DEFAULT_MIN_DIST_M;
  uint32_t _min_interval_ms = DEFAULT_MIN_INTERVAL_MS;
  uint32_t _heartbeat_ms = DEFAULT_HEARTBEAT_MS;
  uint32_t _last_sent_ms = 0;
  int32_t  _sent_lat = 0, _sent_lon = 0;
  uint16_t _last_dist_m = 0;
  bool     _have_sent = false;
  bool     _have_sent_pos = false;
};
