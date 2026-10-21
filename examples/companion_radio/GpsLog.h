#pragma once

// FieldMesh v6.1 diagnostics: log of the GPS duty cycle on the secondary file system.
// Only compiled with -D FM_GPS_LOG=1 (local diagnostic builds, never in releases).
//
// One record per cycle event, only while GPS is enabled:
//   ON     GPS enabled (boot or switched on)        OFF    GPS disabled
//   FIX    end of a hold phase: search time, satellites, position
//   FAIL   search ended without fix (search time = limit)
//   AWAKE  kept running (GPS/Tracking page, SOS)     SLEEP  back to the normal cycle
// Each record carries UTC time (RTC), uptime and the battery voltage.
//
// Storage: two files on the secondary FS (/fm_gps0.log, /fm_gps1.log), 16 KB each
// (~680 records), written in blocks of 8 records. When the current file is full the other
// one is replaced, so the last ~1400 records (> 2 days) are kept.
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

class GpsLog {
public:
  enum Type : uint8_t { T_ON = 1, T_OFF, T_FIX, T_FAIL, T_AWAKE, T_SLEEP };

  struct __attribute__((packed)) Rec {
    uint32_t unix_time;
    uint32_t uptime_s;
    int32_t  lat, lon;        // degrees * 1e6, 0 = none
    uint16_t search_ds;       // search time in 0.1 s
    uint16_t pred_ds;         // learned search time in 0.1 s
    uint16_t batt_mv;
    uint8_t  type;
    uint8_t  sats;
  };   // 24 bytes

  static constexpr uint32_t FILE_MAX = 16384;
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
    if (ev & GpsCycle::EV_SEARCH_FAILED) add(T_FAIL, gps, unix_time, false);

    pollSerial();
  }

private:
  FILESYSTEM* _fs = NULL;
  mesh::MainBoard* _board = NULL;
  Rec  _buf[BUF_RECS];
  int  _n = 0;
  uint8_t _cur = 0;          // file being filled
  bool _init = false;
  GpsCycle::State _last_state = GpsCycle::OFF;
  char _line[24];
  uint8_t _line_len = 0;

  static const char* fileName(uint8_t i) { return i ? "/fm_gps1.log" : "/fm_gps0.log"; }

  size_t fileSize(uint8_t i) {
    if (_fs == NULL || !_fs->exists(fileName(i))) return 0;
    File f = _fs->open(fileName(i), FILE_O_READ);
    if (!f) return 0;
    size_t n = f.size();
    f.close();
    return n;
  }

  // the file being filled is the one that is not full (see header)
  void init() {
    _init = true;
    if (_fs == NULL) return;
    size_t s0 = fileSize(0), s1 = fileSize(1);
    if (s0 >= FILE_MAX && s1 >= FILE_MAX) { _fs->remove(fileName(0)); _cur = 0; }
    else if (s0 >= FILE_MAX) _cur = 1;
    else if (s1 >= FILE_MAX) _cur = 0;
    else _cur = (s1 > 0 && s0 == 0) ? 1 : 0;
  }

  void add(Type t, GpsCycle& gps, uint32_t unix_time, bool with_pos) {
    Rec& r = _buf[_n];
    r.unix_time = unix_time;
    r.uptime_s  = millis() / 1000;
    r.type      = t;
    r.search_ds = (t == T_FIX || t == T_FAIL) ? clamp16(gps.lastSearchMs() / 100) : 0;
    r.pred_ds   = clamp16(gps.predictedSearchMs() / 100);
    r.batt_mv   = _board->getBattMilliVolts();   // read only when a record is written
    bool pos = with_pos && gps.hasFreshFix();
    r.sats      = pos ? gps.satellites() : 0;
    r.lat       = pos ? gps.latitude() : 0;
    r.lon       = pos ? gps.longitude() : 0;
    if (Serial) printRec(r);
    if (++_n >= BUF_RECS) flush();
  }

  static uint16_t clamp16(uint32_t v) { return v > 65535 ? 65535 : (uint16_t)v; }

  void flush() {
    if (_n == 0 || _fs == NULL) { _n = 0; return; }
    if (fileSize(_cur) + _n * sizeof(Rec) > FILE_MAX) {   // switch: replace the older file
      _cur ^= 1;
      _fs->remove(fileName(_cur));
    }
    File f = _fs->open(fileName(_cur), FILE_O_WRITE);   // appends
    if (f) {
      f.write((const uint8_t*)_buf, _n * sizeof(Rec));
      f.close();
    }
    _n = 0;
  }

  static const char* typeName(uint8_t t) {
    switch (t) {
      case T_ON: return "ON";       case T_OFF: return "OFF";
      case T_FIX: return "FIX";     case T_FAIL: return "FAIL";
      case T_AWAKE: return "AWAKE"; case T_SLEEP: return "SLEEP";
      default: return "?";
    }
  }

  static void printRec(const Rec& r) {
    char line[96];
    snprintf(line, sizeof(line), "%lu,%lu,%s,%u.%u,%u.%u,%u,%.6f,%.6f,%u",
      (unsigned long)r.unix_time, (unsigned long)r.uptime_s, typeName(r.type),
      r.search_ds / 10, r.search_ds % 10, r.pred_ds / 10, r.pred_ds % 10, r.sats,
      r.lat / 1000000.0, r.lon / 1000000.0, r.batt_mv);
    Serial.println(line);
  }

  void dumpFile(uint8_t i) {
    if (!_fs->exists(fileName(i))) return;
    File f = _fs->open(fileName(i), FILE_O_READ);
    if (!f) return;
    Rec r;
    while (f.read((uint8_t*)&r, sizeof(r)) == sizeof(r)) printRec(r);
    f.close();
  }

  void handleCommand() {
    if (strcmp(_line, "gpslog") == 0) {
      flush();
      Serial.println("unix,uptime_s,event,search_s,pred_s,sats,lat,lon,batt_mv");
      if (_fs != NULL) {
        dumpFile(_cur ^ 1);   // older file first
        dumpFile(_cur);
      }
      Serial.println("# end");
    } else if (strcmp(_line, "gpslog clear") == 0) {
      _n = 0;
      if (_fs != NULL) { _fs->remove(fileName(0)); _fs->remove(fileName(1)); }
      _cur = 0;
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
