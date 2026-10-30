// FieldMesh additions to MyMesh (companion radio).
//
// Same class as MyMesh.cpp, only a separate file: everything FieldMesh adds to MyMesh is
// implemented here, MyMesh.cpp only calls the fm*() hooks at a few places. Upstream never
// touches this file, so upstream syncs only have to check those hook lines
// (see UPSTREAM_SYNC.md).

#include "MyMesh.h"

#include <Arduino.h>
#include <Mesh.h>
#include "ChannelText.h"   // FieldMesh: sender name / SOS channel rules
#include "OffGrid.h"       // FieldMesh v6.1: off-grid frequency follows the region of the normal frequency
#include "FloodPolicy.h"   // FieldMesh v6.2: Off-Grid managed flooding
#include "FloodLog.h"      // FieldMesh v6.2: flooding diagnostics (only with -D FM_FLOOD_LOG=1)

// Auto-advert interval: 5 minutes in milliseconds
// To change, adjust the 5 and recompile
#define AUTO_ADVERT_INTERVAL_MS  (5UL * 60UL * 1000UL)

// ── Channel messages ─────────────────────────────────────────────────────────

// ── V5: Helper — extract sender name from channel message text ─────────────
// MeshCore-Format: "NodeName: text"
// Returns true if a name was found; out_name is filled in.
// Only ": " is used as separator — node names like "Juno48 | DE.BW" contain " | " as part of the name.
bool MyMesh::extractSenderName(const char* text, char* out_name, int max_len) {
  return fmExtractSenderName(text, out_name, max_len);   // ChannelText.h (unit-tested)
}

// V5: Favourite check for channel messages.
// Extracts the sender name from the text and searches the contact list by name.
// Namens-Kollision (zwei Kontakte gleicher Name): erster Treffer gewinnt — akzeptiertes Risiko.
// IMPORTANT: ContactInfo declared static to avoid stack overflow.
// With MAX_CONTACTS=350 and a deep call stack (BaseChatMesh::loop), a local
// ContactInfo would overflow the nRF52840 stack and crash the BLE stack.
bool MyMesh::isSenderFavorite(const char* text) {
  char sender_name[33];
  if (!extractSenderName(text, sender_name, sizeof(sender_name))) {
    return false;
  }
  static ContactInfo contact;  // static: BSS segment, not on the stack
  // V6: contacts[] now starts with MAX_ANON_CONTACTS reserved anon slots (empty name -> skipped)
  for (uint32_t i = 0; i < MAX_CONTACTS + MAX_ANON_CONTACTS; i++) {
    if (!getContactByIdx(i, contact)) break;
    if (contact.name[0] == 0) continue;
    if (strcmp(contact.name, sender_name) == 0) {
      return (contact.flags & 0x01) != 0;
    }
  }
  return false;
}

// V5: Send channel message from UI (without smartphone).
// Analogous to sendSOS() — wrapper so UITask stays free of getRTCClock() logic.
bool MyMesh::sendChannelMessage(uint8_t channel_idx, const char* text) {
  if (text == nullptr || text[0] == 0) return false;
  ChannelDetails ch;
  if (!getChannel(channel_idx, ch)) return false;
  // getCurrentTimeUnique() statt getCurrentTime() — verhindert Replay-Schutz-Probleme
  // for rapidly successive messages within the same second
  uint32_t ts = getRTCClock()->getCurrentTimeUnique();
  return sendGroupMessage(ts, ch.channel, _prefs.node_name, text, strlen(text));
}

// ── Off-Grid / SOS ───────────────────────────────────────────────────────────

// Off-grid mode: save normal parameters and switch to off-grid settings (or back)
// FieldMesh v6.1: off-grid frequency follows the region of the normal frequency (OffGrid.h)
void MyMesh::toggleOffGrid() {
  if (!_prefs.isRepeatEn()) {
    // Off-Grid aktivieren: Normal-Parameter sichern
    _prefs.normal_freq = _prefs.freq;
    _prefs.normal_bw   = _prefs.bw;
    _prefs.normal_sf   = _prefs.sf;
    _prefs.normal_cr   = _prefs.cr;
    // Off-Grid-Parameter setzen
    _prefs.freq = offGridFreqFor(_prefs.normal_freq);   // EU < 900 MHz <= US
    _prefs.bw   = OFFGRID_BW;
    _prefs.sf   = OFFGRID_SF;
    _prefs.cr   = OFFGRID_CR;
    _prefs.setRepeatEn(true);
  } else {
    // Off-Grid deaktivieren: Normal-Parameter wiederherstellen
    // Fallback if normal_freq was never set (first use)
    if (_prefs.normal_freq > 0.0f) {
      _prefs.freq = _prefs.normal_freq;
      _prefs.bw   = _prefs.normal_bw;
      _prefs.sf   = _prefs.normal_sf;
      _prefs.cr   = _prefs.normal_cr;
    }
    _prefs.setRepeatEn(false);
  }
  fmOnNetworkChange();   // v6.2: no SOS resend / queued relay into the other network
  radio_driver.setParams(_prefs.freq, _prefs.bw, _prefs.sf, _prefs.cr);  // V6: wrapper method (radio_set_params() removed upstream)
  savePrefs();
  advert();  // V4.07: RX restart via normal TX->RX cycle of the wrapper
}

// V3: SOS-Nachricht in den SOS-Channel senden
// Searches for a channel named "SOS" and sends "!SOS <pos>" or "!SOS no GPS"
bool MyMesh::isSosChannel(const char* name) {
  // Case-insensitive: accept "SOS", "sos", "#sos" etc.
  return fmIsSosChannelName(name);   // ChannelText.h (unit-tested)
}

bool MyMesh::sendSOS() {
  for (int i = 0; i < MAX_GROUP_CHANNELS; i++) {
    ChannelDetails ch;
    if (!getChannel(i, ch)) continue;
    if (!isSosChannel(ch.name)) continue;

    // Nachrichtentext zusammenbauen
    char text[80];
    // FieldMesh v6.1: fresh fix (<= 2 min) from the GPS duty cycle
    bool has_pos = _gps_cycle.hasFreshFix();
    if (has_pos) {
      snprintf(text, sizeof(text), "!SOS %.4f,%.4f",
        _gps_cycle.latitude()  / 1000000.0,
        _gps_cycle.longitude() / 1000000.0);
    } else {
      strncpy(text, "!SOS no GPS", sizeof(text));
    }
    // FieldMesh v6.1: battery level, same scale as the battery icon of the UI
#ifndef BATT_MIN_MILLIVOLTS
  #define BATT_MIN_MILLIVOLTS 3000
#endif
#ifndef BATT_MAX_MILLIVOLTS
  #define BATT_MAX_MILLIVOLTS 4200
#endif
    int mv = board.getBattMilliVolts();
    if (mv > 0) {
      int pct = ((mv - BATT_MIN_MILLIVOLTS) * 100) / (BATT_MAX_MILLIVOLTS - BATT_MIN_MILLIVOLTS);
      if (pct < 0) pct = 0;
      if (pct > 100) pct = 100;
      size_t n = strlen(text);
      snprintf(&text[n], sizeof(text) - n, " bat%d%%", pct);
    }

    uint32_t ts = getRTCClock()->getCurrentTimeUnique();
    bool sent = sendGroupMessage(ts, ch.channel, _prefs.node_name, text, strlen(text));
    if (sent) {
      // FieldMesh v6.2: SOS switches the GPS on and leaves it on (position also helps later
      // telemetry requests). Tracking/GPS-Share stays as it is. ThinkNode M1: the hardware
      // switch keeps priority - the setting is stored, the GPS runs once the switch is on.
      if (!_prefs.gps_enabled && sensors.getSettingByKey("gps") != NULL) {
        _prefs.gps_enabled = 1;
        if (GpsCycle::hardwareSwitchOn()) sensors.setSettingValue("gps", "1");
        savePrefs();
      }
      _gps_cycle.onSosSent(has_pos);   // GPS on for 10 min, resend once with position
    }
    return sent;
  }
  return false;  // no SOS channel found
}

// ── Auto-advert / GPS sharing ────────────────────────────────────────────────

// begin(): restore auto-advert timer - active when GPS sharing was enabled
void MyMesh::fmBegin() {
#ifdef FM_FLOOD_LOG
  _fm_flog.begin(_store->getSecondaryFS());   // diagnostic builds only (FloodLog.h)
#endif
  FM_FLOOD_LOG_MODE(_prefs.isRepeatEn(), _prefs.freq, _prefs.bw, _prefs.sf);
  if (_prefs.advert_loc_policy == ADVERT_LOC_SHARE) {
    next_auto_advert = futureMillis(AUTO_ADVERT_INTERVAL_MS);
  }
}

// App-Schalter "GPS in Adverts": Timer sofort starten oder stoppen
void MyMesh::fmUpdateAutoAdvertTimer() {
  if (_prefs.advert_loc_policy == ADVERT_LOC_SHARE) {
    // V3.05: only start timer if not already running (no reset on phone connect)
    // Beim ersten Aktivieren: sofort senden + Timer starten
    if (next_auto_advert == 0) {
      advert();
      next_auto_advert = futureMillis(AUTO_ADVERT_INTERVAL_MS);
    }
  } else {
    next_auto_advert = 0;
  }
}

// Enable/disable GPS sharing and auto-advert together (called from UI button)
// V3.05: only start timer if not already running (no reset on repeated toggle)
void MyMesh::setGPSAdvertEnabled(bool enabled) {
  _prefs.advert_loc_policy = enabled ? ADVERT_LOC_SHARE : ADVERT_LOC_NONE;
  fmUpdateAutoAdvertTimer();
  savePrefs();
}

// ── Adverts ──────────────────────────────────────────────────────────────────

// onDiscoveredContact(): advert_paths is always written (RECENT page);
// gps_timestamp + has_gps only updated when GPS was included in this advert (TRACKING page).
// V6: GPS-Info kommt aus BaseChatMesh::lastAdvertHasLatLon() (ersetzt Bit 4 in contact.type)
void MyMesh::fmNoteAdvertGps(AdvertPath* p) {
  if (lastAdvertHasLatLon()) {
    p->has_gps = true;
    p->gps_timestamp = getRTCClock()->getCurrentTime(); // only when GPS present
  }
}

// advert(): only include GPS with a current fix - cached/stale coordinates are intentionally ignored
mesh::Packet* MyMesh::fmCreateSelfAdvert() {
  bool send_gps = false;
  if (_prefs.advert_loc_policy != ADVERT_LOC_NONE) {
    // FieldMesh v6.1: fresh fix (<= 2 min) from the GPS duty cycle
    send_gps = _gps_cycle.hasFreshFix();
  }
  if (send_gps) {
    return createSelfAdvert(_prefs.node_name, _gps_cycle.latitude() / 1000000.0, _gps_cycle.longitude() / 1000000.0);
  }
  return createSelfAdvert(_prefs.node_name);
}

void MyMesh::fmSendSelfAdvert(mesh::Packet* pkt) {
  if (_prefs.isRepeatEn()) {
    // Off-grid mode: flood advert so other M1s can relay it
    sendFlood(pkt, (uint32_t)0, _prefs.path_hash_mode + 1);
  } else {
    sendZeroHop(pkt);
  }
}

// ── Off-Grid managed flooding (v6.2, FloodPolicy.h) ──────────────────────────
// Only while Off-Grid (repeat on); normal operation keeps the upstream behaviour.

// A: wait up to 30 s for a free channel instead of transmitting over others after 4 s.
// The radio wrappers clear stuck preamble/header flags themselves (_preambleMillis / _maxPayloadMillis).
uint32_t MyMesh::getCADFailMaxDuration() const {
  if (_prefs.isRepeatEn()) return FM_FLOOD_CAD_MAX_MS;
  return mesh::Mesh::getCADFailMaxDuration();
}

// B: relay delay like a Meshtastic client - weak signal relays early, strong signal waits longer.
uint32_t MyMesh::fmRelayDelay(const mesh::Packet* packet) {
  uint32_t d = fmFloodRelayDelay(packet->getSNR(), fmFloodSlotMs(_prefs.sf, _prefs.bw), getRNG()->nextInt(0, 0x10000));
  FM_FLOOD_LOG_EV(Q, packet, d);
  return d;
}

// B: flood-routed direct messages / requests get more time for the reply
uint32_t MyMesh::fmFloodTimeoutExtra() const {
  if (!_prefs.isRepeatEn()) return 0;
  return fmFloodTimeoutExtraMs(fmFloodSlotMs(_prefs.sf, _prefs.bw));
}

// C: someone else relayed this advert first -> drop our queued relay of it. Only adverts
// (they come again every few minutes); messages, SOS and ACKs are always relayed.
// Called from filterRecvFloodPacket(), i.e. before the duplicate check and before the
// next send attempt, so a relay that is still waiting for a free channel is caught too.
void MyMesh::fmFilterRelay(mesh::Packet* packet) {
  if (!fmFloodCancelOnDuplicate(packet->getPayloadType())) return;
  uint8_t hash[MAX_HASH_SIZE];
  packet->calculatePacketHash(hash);
  int i = fmFloodFindQueuedRelay(*_mgr, hash);
  if (i >= 0) {
    mesh::Packet* q = _mgr->removeOutboundByIdx(i);
    FM_FLOOD_LOG_EV(SKIP, q, 0);
    _mgr->free(q);
    _fm_relay_skipped++;
  }
}

// Network change (Off-Grid toggled, new radio parameters from the app): nothing heard in one
// network may be sent into the other one. Relays can wait up to ~22 s (+ busy channel) in
// Off-Grid. Own queued packets (path 0) stay.
void MyMesh::fmOnNetworkChange() {
  _gps_cycle.cancelSosResend();   // v6.2: no SOS resend into the other network
  int n = fmFloodDropQueuedRelays(*_mgr);
  if (n > 0) MESH_DEBUG_PRINTLN("FieldMesh: network change, %d queued relays dropped", n);
  FM_FLOOD_LOG_EV(NET, NULL, n);
  FM_FLOOD_LOG_MODE(_prefs.isRepeatEn(), _prefs.freq, _prefs.bw, _prefs.sf);
}

#ifdef FM_FLOOD_LOG
// Diagnostic builds only: every received / sent packet (FloodLog.h)
void MyMesh::logRx(mesh::Packet* packet, int len, float score) { FM_FLOOD_LOG_EV(RX, packet, len); }
void MyMesh::logTx(mesh::Packet* packet, int len) { FM_FLOOD_LOG_EV(TX, packet, len); }
void MyMesh::logTxFail(mesh::Packet* packet, int len) { FM_FLOOD_LOG_EV(TXF, packet, len); }
#endif

// ── Loop: GPS cycle, tracking, auto-advert, clock jump ───────────────────────

void MyMesh::fmLoop() {
  // FieldMesh v6.1: GPS duty cycle
  uint8_t gps_ev = _gps_cycle.loop(sensors, _prefs);
  if (gps_ev & GpsCycle::EV_PREFS_CHANGED) savePrefs();
  if (gps_ev & GpsCycle::EV_SOS_RESEND) sendSOS();   // first SOS went out without position

  // FieldMesh v6.1: tracking (GPS sharing + GPS running) -> movement / heartbeat cadence,
  // otherwise the fixed auto-advert timer as before
  if (isAutoAdvertEnabled() && _gps_cycle.state() != GpsCycle::OFF) {
    if (!_tracking_active) { _track.reset(millis()); _tracking_active = true; }
    if (_track.poll(millis(), (gps_ev & GpsCycle::EV_FIX) != 0, _gps_cycle.latitude(),
                    _gps_cycle.longitude(), _gps_cycle.satellites(), _gps_cycle.hasFreshFix(),
                    _gps_cycle.isFast()) != TrackScheduler::NONE) {
      bool with_pos = _gps_cycle.hasFreshFix();   // advert() uses the same check
      advert();
      _track.sent(millis(), with_pos, _gps_cycle.latitude(), _gps_cycle.longitude());
    }
    if (next_auto_advert) next_auto_advert = futureMillis(AUTO_ADVERT_INTERVAL_MS);  // fallback restarts later
  } else {
    _tracking_active = false;
    // auto-advert timer: send 0-hop advert at configured interval
    if (next_auto_advert && millisHasNowPassed(next_auto_advert)) {
      advert();
      next_auto_advert = futureMillis(AUTO_ADVERT_INTERVAL_MS);  // schedule next
    }
  }

  checkClockJump();  // FieldMesh (#23)

#ifdef FM_FLOOD_LOG
  static unsigned long fm_log_mode_at = 0;   // MODE record every 10 min
  if (millis() - fm_log_mode_at >= 600000UL) {
    fm_log_mode_at = millis();
    FM_FLOOD_LOG_MODE(_prefs.isRepeatEn(), _prefs.freq, _prefs.bw, _prefs.sf);
  }
  _fm_flog.loop();
  _fm_flog.pollSerial(_prefs.node_name);   // "floodlog" / "floodlog clear" over USB
#endif
}

// FieldMesh (#23): detect RTC jumps and shift stored timestamps.
// Without a phone or GPS fix the RTC starts near epoch / at a stale value. When the app
// (CMD_SET_DEVICE_TIME) or GPS later corrects the clock, timestamps recorded before the
// correction would yield implausible relative times ("129h"). Instead of hooking every
// time source, compare the RTC against millis(): a deviation of more than
// CLOCK_JUMP_THRESHOLD_SECS is treated as a clock correction.
#define CLOCK_JUMP_THRESHOLD_SECS  60

void MyMesh::checkClockJump() {
  uint32_t now = getRTCClock()->getCurrentTime();
  unsigned long ms = millis();
  if (_clk_ref_ms == 0) {         // first call: anchor only
    _clk_ref_rtc = now;
    _clk_ref_ms = ms ? ms : 1;
    return;
  }
  unsigned long elapsed_ms = ms - _clk_ref_ms;
  int64_t expected = (int64_t)_clk_ref_rtc + (int64_t)(elapsed_ms / 1000);
  int64_t delta = (int64_t)now - expected;

  if (delta > CLOCK_JUMP_THRESHOLD_SECS || delta < -CLOCK_JUMP_THRESHOLD_SECS) {
    int32_t d = (int32_t)delta;
    for (int i = 0; i < ADVERT_PATH_TABLE_SIZE; i++) {
      auto p = &advert_paths[i];
      if (p->recv_timestamp) {
        int64_t v = (int64_t)p->recv_timestamp + d;
        p->recv_timestamp = (v > 1) ? (uint32_t)v : 1;
      }
      if (p->gps_timestamp) {
        int64_t v = (int64_t)p->gps_timestamp + d;
        p->gps_timestamp = (v > 1) ? (uint32_t)v : 1;
      }
    }
    if (_listener) _listener->onClockAdjusted(d);
    _clk_ref_rtc = now;
    _clk_ref_ms = ms;
  } else if (elapsed_ms >= 60000) {
    // re-anchor once a minute so slow drift never adds up to a false "jump"
    _clk_ref_rtc = now;
    _clk_ref_ms = ms;
  }
}
