#pragma once

// FieldMesh v6.1 diagnostics: log of the GPS duty cycle on the secondary file system.
// Only compiled with -D FM_GPS_LOG=1 (local diagnostic builds, never in releases).
//
// One record per cycle event, only while GPS is enabled:
//   ON     GPS enabled (boot or switched on)        OFF    GPS disabled
//   FIX    end of a hold phase: search time, satellites, position
//   FAIL   search ended without fix, sats = most strong satellites (>= 25 dB-Hz) in one epoch,
//          s20 / s30 = the same >= 20 / >= 30 dB-Hz,
//          reason = limit / mid / long / lost (see reasonName()); "fast" (FIX) or "/fast" (FAIL)
//          while the cycle runs every 60 s because of fast movement
//   EXT    search extended (sky visible, no fix yet), sats = most strong satellites seen
//   AWAKE  kept running (GPS/Tracking page, SOS)     SLEEP  back to the normal cycle
// Each record carries UTC time (RTC), uptime and the battery voltage, plus receiver
// diagnostics: GSV sentences and best SNR during the search, NMEA sentence types seen since
// the last wake (bit 0 GGA, 1 RMC, 2 GSV, 3 GSA, 4 GLL, 5 VTG, 6 ZDA, 7 other/$PAIR) and the
// receiver configuration flags (cfg, GpsCycle::configFlags(): 0x01 $PAIR062 ok, 0x02 $PAIR067
// answered, 0x04 all satellite systems on, 0x08 $PAIR066 sent, 0x10 $PAIR066 ok, 0x20 $PAIR513
// ok, 0x40 $PCAS04 sent).
//
// Storage: ring buffer of 384 records (12 KB, ~12.5 h at one record per 2 min cycle) in
// /fm_gpsr.log on the secondary FS, written in blocks of 8 records; the oldest records are
// overwritten one by one. /fm_gpsr.idx holds the next write position and the count. The
// secondary FS also holds contacts, channels and advert blobs (T1000-E: 100 KB in internal
// flash, ~81 KB used with 350 contacts) - field test 22./23.10. with 2 x 16 KB files: the log
// stopped after ~20 h because the FS was full. Failed writes are counted; "gpslog" ends with
// the FS usage and the files on it. Files of older log versions are deleted at boot.
//
// USB serial commands (115200 baud, line end CR or LF):
//   gpslog        print all records as CSV
//   gpslog clear  delete the log
// While USB is connected every record is also printed live.

#ifdef FM_GPS_LOG

#include <Arduino.h>
#include <helpers/IdentityStore.h>   // FILESYSTEM
#include <MeshCore.h>
#include "GpsCycle.h"

#if defined(NRF52_PLATFORM)
lfs_ssize_t _getLfsUsedBlockCount(FILESYSTEM* fs);   // DataStore.cpp
#endif

class GpsLog {
public:
  enum Type : uint8_t { T_ON = 1, T_OFF, T_FIX, T_FAIL, T_AWAKE, T_SLEEP, T_EXT };

  struct __attribute__((packed)) Rec {
    uint32_t unix_time;
    uint32_t uptime_s;
    int32_t  lat, lon;        // degrees * 1e6, 0 = none
    uint16_t search_ds;       // search time in 0.1 s
    uint16_t pred_ds;         // learned search time in 0.1 s
    uint16_t batt_mv;
    uint8_t  type;
    uint8_t  sats;
    uint16_t gsv;             // GSV sentences during the search
    uint8_t  best_snr;        // best SNR (dB-Hz) during the search
    uint8_t  types;           // sentence types since the last wake
    uint8_t  cfg;             // receiver configuration (GpsCycle::configFlags()); was the
                              // $PAIR062 acknowledgement before 22.10.
    uint8_t  usable;          // most satellites >= 20 dB-Hz in one epoch (FAIL/EXT); was the
                              // $PAIR513 acknowledgement before 22.10. (always 0 in the tests)
    uint8_t  good;            // most satellites >= 30 dB-Hz in one epoch (FAIL/EXT)
    uint8_t  reason;          // FAIL: why the search ended (GpsCycle::EndReason); bit 7 = fast
  };   // 32 bytes

  static constexpr uint16_t RING_RECS = 384;
  static constexpr int BUF_RECS = 8;

  void loop(GpsCycle& gps, uint32_t unix_time, mesh::MainBoard& board, FILESYSTEM* fs) {
    _fs = fs;
    _board = &board;
    if (!_init) init();

    GpsCycle::State st = gps.state();
    uint8_t ev = gps.lastEvents();

    if (st != _last_state) {
      if (_last_state == GpsCycle::OFF) add(T_ON, gps, unix_time, false);
      else if (st == GpsCycle::OFF)     { add(T_OFF, gps, unix_time, false); flush(); }
      if (st == GpsCycle::AWAKE)        add(T_AWAKE, gps, unix_time, false);
      else if (_last_state == GpsCycle::AWAKE && st != GpsCycle::OFF) add(T_SLEEP, gps, unix_time, false);
      _last_state = st;
    }
    if (ev & GpsCycle::EV_FIX)           add(T_FIX, gps, unix_time, true);
    if (ev & GpsCycle::EV_SEARCH_EXTENDED) add(T_EXT, gps, unix_time, false);
    if (ev & GpsCycle::EV_SEARCH_FAILED) add(T_FAIL, gps, unix_time, false);

    pollSerial();
  }

private:
  FILESYSTEM* _fs = NULL;
  mesh::MainBoard* _board = NULL;
  Rec  _buf[BUF_RECS];
  int  _n = 0;
  uint16_t _next = 0;        // ring position of the next record
  uint16_t _count = 0;       // records in the ring
  bool _init = false;
  uint16_t _write_errors = 0;
  uint32_t _first_error = 0;   // unix time of the first failed write
  GpsCycle::State _last_state = GpsCycle::OFF;
  char _line[24];
  uint8_t _line_len = 0;

  static constexpr const char* LOG_FILE = "/fm_gpsr.log";
  static constexpr const char* IDX_FILE = "/fm_gpsr.idx";

  size_t logSize() {
    if (_fs == NULL || !_fs->exists(LOG_FILE)) return 0;
    File f = _fs->open(LOG_FILE, FILE_O_READ);
    if (!f) return 0;
    size_t n = f.size();
    f.close();
    return n;
  }

  void init() {
    _init = true;
    if (_fs == NULL) return;
    static const char* const old_files[4] = { "/fm_gps0.log", "/fm_gps1.log", "/fm_gpsb0.log", "/fm_gpsb1.log" };
    for (int i = 0; i < 4; i++) _fs->remove(old_files[i]);
    size_t recs = logSize() / sizeof(Rec);
    if (recs > RING_RECS) { _fs->remove(LOG_FILE); recs = 0; }
    _count = recs;
    _next = recs % RING_RECS;
    File f = _fs->open(IDX_FILE, FILE_O_READ);
    if (f) {
      uint16_t idx[2];
      if (f.read((uint8_t*)idx, sizeof(idx)) == sizeof(idx) && idx[1] <= recs && idx[0] < RING_RECS
          && (idx[1] == RING_RECS || idx[0] == idx[1])) {
        _next = idx[0];
        _count = idx[1];
      }
      f.close();
    }
  }

  void saveIndex() {
    _fs->remove(IDX_FILE);
    File f = _fs->open(IDX_FILE, FILE_O_WRITE);
    if (!f) return;
    uint16_t idx[2] = { _next, _count };
    f.write((const uint8_t*)idx, sizeof(idx));
    f.close();
  }

  void add(Type t, GpsCycle& gps, uint32_t unix_time, bool with_pos) {
    Rec& r = _buf[_n];
    r.unix_time = unix_time;
    r.uptime_s  = millis() / 1000;
    r.type      = t;
    r.search_ds = (t == T_FIX || t == T_FAIL) ? clamp16(gps.lastSearchMs() / 100) : 0;
    if (t == T_EXT) r.search_ds = clamp16(gps.searchElapsedMs() / 100);
    r.pred_ds   = clamp16(gps.predictedSearchMs() / 100);
    r.batt_mv   = _board->getBattMilliVolts();   // read only when a record is written
    bool pos = with_pos && gps.hasFreshFix();
    r.sats      = pos ? gps.satellites() : ((t == T_FAIL || t == T_EXT) ? gps.searchMaxStrong() : 0);
    r.lat       = pos ? gps.latitude() : 0;
    r.lon       = pos ? gps.longitude() : 0;
    bool search = (t == T_FIX || t == T_FAIL || t == T_EXT);
    r.gsv       = search ? gps.searchGsvCount() : 0;
    r.best_snr  = search ? gps.searchBestSnr() : 0;
    r.types     = gps.sentenceTypes();
    r.cfg       = gps.configFlags();
    r.usable    = (t == T_FAIL || t == T_EXT) ? gps.searchMaxUsable() : 0;
    r.good      = (t == T_FAIL || t == T_EXT) ? gps.searchMaxGood() : 0;
    r.reason    = (t == T_FAIL) ? gps.endReason() : 0;
    if (gps.isFast()) r.reason |= 0x80;   // moving fast: 60 s cycle
    if (Serial) printRec(r);
    if (++_n >= BUF_RECS) flush();
  }

  static uint16_t clamp16(uint32_t v) { return v > 65535 ? 65535 : (uint16_t)v; }

  // write the buffered records at the ring position: appended while the ring is filling,
  // afterwards in place over the oldest records (split at the end of the ring)
  void flush() {
    if (_n == 0 || _fs == NULL) { _n = 0; return; }
    File f = _fs->open(LOG_FILE, FILE_O_WRITE);
    bool ok = (bool)f;
    int done = 0;
    while (ok && done < _n) {
      int part = RING_RECS - _next;
      if (part > _n - done) part = _n - done;
      size_t bytes = part * sizeof(Rec);
      ok = f.seek((uint32_t)_next * sizeof(Rec))
           && f.write((const uint8_t*)&_buf[done], bytes) == bytes;
      if (ok) {
        done += part;
        _next = (_next + part) % RING_RECS;
        _count = (_count + part > RING_RECS) ? RING_RECS : _count + part;
      }
    }
    if (f) f.close();
    if (!ok) {
      if (_write_errors == 0) _first_error = _buf[0].unix_time;
      if (_write_errors < 0xFFFF) _write_errors++;
    }
    if (done > 0) saveIndex();
    _n = 0;
  }

  // end of the "gpslog" output: secondary FS usage, its files and the failed log writes
  void printStorage() {
#if defined(NRF52_PLATFORM)
    const lfs_config* cfg = _fs->_getFS()->cfg;
    unsigned long used = (unsigned long)_getLfsUsedBlockCount(_fs) * cfg->block_size;
    Serial.printf("# fs used %lu of %lu bytes\n", used, (unsigned long)(cfg->block_size * cfg->block_count));
#endif
    File root = _fs->open("/", FILE_O_READ);
    if (root) {
      File file = root.openNextFile();
      while (file) {
        Serial.printf("# file %s %lu\n", file.name(), (unsigned long)file.size());
        file.close();
        file = root.openNextFile();
      }
      root.close();
    }
    Serial.printf("# log write errors %u, first at %lu\n", _write_errors, (unsigned long)_first_error);
  }

  static const char* typeName(uint8_t t) {
    switch (t) {
      case T_ON: return "ON";       case T_OFF: return "OFF";
      case T_FIX: return "FIX";     case T_FAIL: return "FAIL";
      case T_AWAKE: return "AWAKE"; case T_SLEEP: return "SLEEP";
      case T_EXT: return "EXT";
      default: return "?";
    }
  }

  // FAIL: limit = 15/30 s search time up, mid = 30 s (1..3 strong) up, long = 120 s up,
  // lost = no strong satellite for 15 s during a longer search
  static const char* reasonName(uint8_t type, uint8_t reason) {
    if (type != T_FAIL) return "";
    switch (reason & 0x7F) {
      case GpsCycle::END_LIMIT: return "limit";
      case GpsCycle::END_MID:   return "mid";
      case GpsCycle::END_LONG:  return "long";
      case GpsCycle::END_LOST:  return "lost";
      default: return "?";
    }
  }

  static void printRec(const Rec& r) {
    char line[128];
    snprintf(line, sizeof(line), "%lu,%lu,%s,%u.%u,%u.%u,%u,%.6f,%.6f,%u,%u,%u,0x%02X,0x%02X,%d,%u,%s%s",
      (unsigned long)r.unix_time, (unsigned long)r.uptime_s, typeName(r.type),
      r.search_ds / 10, r.search_ds % 10, r.pred_ds / 10, r.pred_ds % 10, r.sats,
      r.lat / 1000000.0, r.lon / 1000000.0, r.batt_mv,
      r.gsv, r.best_snr, r.types,
      r.cfg, (int)r.usable,
      r.good, reasonName(r.type, r.reason),
      (r.reason & 0x80) ? (r.type == T_FAIL ? "/fast" : "fast") : "");
    Serial.println(line);
  }

  // oldest record first
  void dumpRing() {
    if (_count == 0) return;
    File f = _fs->open(LOG_FILE, FILE_O_READ);
    if (!f) return;
    uint16_t pos = (_count < RING_RECS) ? 0 : _next;
    Rec r;
    for (uint16_t i = 0; i < _count; i++) {
      if (!f.seek((uint32_t)pos * sizeof(Rec)) || f.read((uint8_t*)&r, sizeof(r)) != sizeof(r)) break;
      printRec(r);
      pos = (pos + 1) % RING_RECS;
    }
    f.close();
  }

  void handleCommand() {
    if (strcmp(_line, "gpslog") == 0) {
      flush();
      Serial.println("unix,uptime_s,event,search_s,pred_s,sats,lat,lon,batt_mv,gsv,best_snr,types,cfg,s20,s30,reason");
      if (_fs != NULL) {
        dumpRing();
        printStorage();
      }
      Serial.println("# end");
    } else if (strcmp(_line, "gpslog clear") == 0) {
      _n = 0;
      if (_fs != NULL) { _fs->remove(LOG_FILE); _fs->remove(IDX_FILE); }
      _next = 0;
      _count = 0;
      Serial.println("# gpslog cleared");
    }
  }

  void pollSerial() {
    while (Serial.available()) {
      char c = Serial.read();
      if (c == '\r' || c == '\n') {
        if (_line_len > 0) { _line[_line_len] = 0; handleCommand(); }
        _line_len = 0;
      } else if (_line_len < sizeof(_line) - 1) {
        _line[_line_len++] = c;
      }
    }
  }
};

#endif
