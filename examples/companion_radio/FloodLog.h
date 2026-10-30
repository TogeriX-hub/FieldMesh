#pragma once

// FieldMesh v6.2 diagnostics: flooding events stored in flash, read out afterwards over USB.
// Only compiled with -D FM_FLOOD_LOG=1 (local test builds, never in releases); shared with the
// FieldMesh repeater. Used to check the Off-Grid managed flooding on real hardware: every device
// logs on its own during the test, the logs are read out one after the other and matched by
// packet hash. Do not combine with FM_GPS_LOG (both read commands from USB serial).
//
// Events (one 20-byte record each):
//   RX   packet received (every copy, before the duplicate check)   extra = length
//   TX   packet sent                                               extra = length
//   TXF  send failed                                               extra = length
//   Q    flood relay queued                                        extra = delay ms
//   SKIP queued advert relay dropped, another node relayed first (companion)
//   LATE queued relay moved to the late window (repeater)          extra = new delay ms
//   NET  network change, queued relays dropped                     extra = count
//   MODE Off-Grid state and radio params: at boot, after a network change, every 10 min
// Packet fields: hash = first 4 bytes of Packet::calculatePacketHash() (same on every device,
// ignores the path), payload type, route (F flood / D direct), path hash count, SNR.
//
// Storage: SEG_COUNT segment files /fm_fl<n>.seg of 64 records (1.25 KB) each, only appended
// to; when the current segment is full the oldest one is deleted and reused (same scheme as
// GpsLog.h, see there why). nRF52: 8 segments on the secondary FS (last 448..512 records),
// ESP32 (Station G2): 32 segments on SPIFFS (last 1984..2048 records). Records are buffered in
// RAM (16) and written when the buffer is full or after 30 s.
//
// USB serial (115200 baud):
//   floodlog        print all records as CSV, ends with "# end"
//   floodlog clear  delete the log
// Companion: read by FloodLog::pollSerial(). Repeater: through its normal serial CLI
// (MyMesh::fmHandleCommand), only from USB.
//
// Companions log packet events only while Off-Grid (begin(fs, true)): in normal mode the
// public mesh around would fill the small log and overwrite the test. MODE and NET are always
// stored. The repeater logs always (large log; in normal mode it shows that nothing heard
// Off-Grid reached the normal mesh).
//
// fmDiagLogCleanup() (always compiled): firmware updates keep the file system, so log files of a
// diagnostic build would stay on the device forever. Every build deletes the files of the
// diagnostic logs it does not include (exact names only), at boot.

#include <Arduino.h>
#include <Packet.h>
#include <helpers/IdentityStore.h>   // FILESYSTEM

inline void fmDiagLogCleanup(FILESYSTEM* fs) {
  if (fs == NULL) return;
  char name[20];
#ifndef FM_FLOOD_LOG
  for (unsigned i = 0; i < 32; i++) {   // flood log segments (FloodLog.h)
    snprintf(name, sizeof(name), "/fm_fl%u.seg", i);
    if (fs->exists(name)) fs->remove(name);
  }
  if (fs->exists("/fm_fl.idx")) fs->remove("/fm_fl.idx");
#endif
#ifndef FM_GPS_LOG
  for (unsigned i = 0; i < 8; i++) {    // GPS log segments (GpsLog.h)
    snprintf(name, sizeof(name), "/fm_gps%u.seg", i);
    if (fs->exists(name)) fs->remove(name);
  }
  static const char* const gps_files[] = { "/fm_gps.idx", "/fm_gps0.log", "/fm_gps1.log", "/fm_gpsb0.log",
                                           "/fm_gpsb1.log", "/fm_gpsr.log", "/fm_gpsr.idx" };
  for (unsigned i = 0; i < sizeof(gps_files) / sizeof(gps_files[0]); i++) {
    if (fs->exists(gps_files[i])) fs->remove(gps_files[i]);
  }
#endif
  (void)name;
}

#ifdef FM_FLOOD_LOG

class FloodLog {
public:
  enum Ev : uint8_t { RX = 1, TX, TXF, Q, SKIP, LATE, NET, MODE };

  struct __attribute__((packed)) Rec {
    uint32_t ms;       // uptime in ms
    uint32_t rtc;      // unix time (RTC)
    uint32_t hash;     // first 4 bytes of the packet hash; MODE: frequency in kHz
    uint16_t extra;    // see events; MODE: bandwidth in 0.1 kHz
    int8_t   snr4;     // SNR * 4
    uint8_t  ev;
    uint8_t  type;     // payload type, bit 7 = flood route; MODE: spreading factor
    uint8_t  path;     // path hash count; MODE: 1 = Off-Grid
    uint8_t  pad[2];
  };   // 20 bytes

#if defined(ESP32)
  static constexpr uint8_t SEG_COUNT = 32;
#else
  static constexpr uint8_t SEG_COUNT = 8;
#endif
  static constexpr uint16_t SEG_RECS = 64;
  static constexpr int BUF_RECS = 16;

  void begin(FILESYSTEM* fs, bool only_offgrid) {
    _fs = fs;
    _only_offgrid = only_offgrid;
    if (_fs == NULL) return;
    _seg = 0;
    File f = openRead(IDX_FILE);
    if (f) {
      uint8_t v = 0;
      if (f.read(&v, 1) == 1 && v < SEG_COUNT) _seg = v;
      f.close();
    }
    char name[16];
    segName(_seg, name);
    size_t recs = fileSize(name) / sizeof(Rec);
    if (recs > SEG_RECS) { _fs->remove(name); recs = 0; }
    _seg_n = recs;
  }

  void add(uint8_t ev, const mesh::Packet* p, uint32_t rtc, uint32_t extra) {
    if (_only_offgrid && !_offgrid && ev != NET) return;   // companion in normal mode
    Rec& r = _buf[_n];
    memset(&r, 0, sizeof(r));
    r.ms = millis();
    r.rtc = rtc;
    r.ev = ev;
    r.extra = extra > 65535 ? 65535 : (uint16_t)extra;
    if (p) {
      uint8_t h[MAX_HASH_SIZE];
      p->calculatePacketHash(h);
      memcpy(&r.hash, h, 4);
      r.type = p->getPayloadType() | (p->isRouteFlood() ? 0x80 : 0);
      r.path = p->getPathHashCount();
      r.snr4 = p->_snr;
    }
    push();
  }

  void mode(uint32_t rtc, bool offgrid, float freq, float bw, uint8_t sf) {
    _offgrid = offgrid;
    Rec& r = _buf[_n];
    memset(&r, 0, sizeof(r));
    r.ms = millis();
    r.rtc = rtc;
    r.ev = MODE;
    r.hash = (uint32_t)(freq * 1000.0f + 0.5f);
    r.extra = (uint16_t)(bw * 10.0f + 0.5f);
    r.type = sf;
    r.path = offgrid ? 1 : 0;
    push();
  }

  // call from the main loop: writes buffered records after 30 s
  void loop() {
    if (_n > 0 && millis() - _first_buffered >= 30000UL) flush();
  }

  // companion: "floodlog" / "floodlog clear" from USB serial
  void pollSerial(const char* node_name) {
    while (Serial.available()) {
      char c = Serial.read();
      if (c == '\r' || c == '\n') {
        if (_line_len > 0) {
          _line[_line_len] = 0;
          handleCommand(_line, node_name);
        }
        _line_len = 0;
      } else if (_line_len < sizeof(_line) - 1) {
        _line[_line_len++] = c;
      }
    }
  }

  // true if the command was a floodlog command (output already printed to Serial)
  bool handleCommand(const char* cmd, const char* node_name) {
    if (strcmp(cmd, "floodlog") == 0) {
      dump(node_name);
      return true;
    }
    if (strcmp(cmd, "floodlog clear") == 0) {
      clear();
      Serial.println("# floodlog cleared");
      return true;
    }
    return false;
  }

private:
  FILESYSTEM* _fs = NULL;
  bool _only_offgrid = false;
  bool _offgrid = false;
  Rec  _buf[BUF_RECS];
  int  _n = 0;
  unsigned long _first_buffered = 0;
  uint8_t  _seg = 0;
  uint16_t _seg_n = 0;
  uint16_t _write_errors = 0;
  char _line[24];
  uint8_t _line_len = 0;

  static constexpr const char* IDX_FILE = "/fm_fl.idx";

  static void segName(uint8_t i, char* out) { snprintf(out, 16, "/fm_fl%u.seg", (unsigned)i); }

  File openRead(const char* name) {
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
    return _fs->open(name, FILE_O_READ);
#else
    return _fs->open(name, "r");
#endif
  }
  File openAppend(const char* name) {
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
    return _fs->open(name, FILE_O_WRITE);   // appends
#else
    return _fs->open(name, "a");
#endif
  }

  size_t fileSize(const char* name) {
    if (_fs == NULL || !_fs->exists(name)) return 0;
    File f = openRead(name);
    if (!f) return 0;
    size_t n = f.size();
    f.close();
    return n;
  }

  void push() {
    if (_n == 0) _first_buffered = millis();
    if (++_n >= BUF_RECS) flush();
  }

  void saveIndex() {
    _fs->remove(IDX_FILE);
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
    File f = _fs->open(IDX_FILE, FILE_O_WRITE);
#else
    File f = _fs->open(IDX_FILE, "w");
#endif
    if (!f) return;
    f.write(&_seg, 1);
    f.close();
  }

  void flush() {
    if (_n == 0 || _fs == NULL) { _n = 0; return; }
    int done = 0;
    char name[16];
    while (done < _n) {
      if (_seg_n >= SEG_RECS) {
        _seg = (_seg + 1) % SEG_COUNT;
        segName(_seg, name);
        _fs->remove(name);
        _seg_n = 0;
        saveIndex();
      }
      int part = SEG_RECS - _seg_n;
      if (part > _n - done) part = _n - done;
      segName(_seg, name);
      File f = openAppend(name);
      if (f) {
        f.write((const uint8_t*)&_buf[done], part * sizeof(Rec));
        f.close();
      }
      size_t recs = fileSize(name) / sizeof(Rec);   // a failed write only shows in the size
      if (recs != (size_t)(_seg_n + part) && _write_errors < 0xFFFF) _write_errors++;
      _seg_n = (recs > SEG_RECS) ? SEG_RECS : recs;
      done += part;
    }
    _n = 0;
  }

  void clear() {
    _n = 0;
    if (_fs != NULL) {
      char name[16];
      for (uint8_t i = 0; i < SEG_COUNT; i++) { segName(i, name); _fs->remove(name); }
      _fs->remove(IDX_FILE);
    }
    _seg = 0;
    _seg_n = 0;
    _write_errors = 0;
  }

  static const char* evName(uint8_t ev) {
    switch (ev) {
      case RX: return "RX";     case TX: return "TX";     case TXF: return "TXF";
      case Q: return "Q";       case SKIP: return "SKIP"; case LATE: return "LATE";
      case NET: return "NET";   case MODE: return "MODE";
      default: return "?";
    }
  }

  static void printRec(const Rec& r) {
    char line[96];
    if (r.ev == MODE) {
      snprintf(line, sizeof(line), "%lu,%lu,MODE,%u,%lu.%03lu,%u.%u,%u",
               (unsigned long)r.ms, (unsigned long)r.rtc, (unsigned)r.path,
               (unsigned long)(r.hash / 1000), (unsigned long)(r.hash % 1000),
               (unsigned)(r.extra / 10), (unsigned)(r.extra % 10), (unsigned)r.type);
    } else {
      const uint8_t* h = (const uint8_t*)&r.hash;
      int s = r.snr4;
      snprintf(line, sizeof(line), "%lu,%lu,%s,%02X%02X%02X%02X,%u,%c,%u,%s%d.%02d,%u",
               (unsigned long)r.ms, (unsigned long)r.rtc, evName(r.ev), h[0], h[1], h[2], h[3],
               (unsigned)(r.type & 0x0F), (r.type & 0x80) ? 'F' : 'D', (unsigned)r.path,
               s < 0 ? "-" : "", abs(s) / 4, (abs(s) % 4) * 25, (unsigned)r.extra);
    }
    Serial.println(line);
  }

  void dump(const char* node_name) {
    flush();
    Serial.printf("# FieldMesh flood log v1, node %s, uptime %lu ms\n", node_name ? node_name : "?",
                  (unsigned long)millis());
    Serial.println("# uptime_ms,rtc,event,hash,type,route,path,snr,extra  |  uptime_ms,rtc,MODE,offgrid,freq,bw,sf");
    uint32_t count = 0;
    if (_fs != NULL) {
      char name[16];
      Rec r;
      for (uint8_t i = 1; i <= SEG_COUNT; i++) {   // oldest segment first, current one last
        segName((_seg + i) % SEG_COUNT, name);
        if (!_fs->exists(name)) continue;
        File f = openRead(name);
        if (!f) continue;
        while (f.read((uint8_t*)&r, sizeof(r)) == sizeof(r)) {
          printRec(r);
          if ((++count & 31) == 0) delay(1);   // let USB / watchdog breathe
        }
        f.close();
      }
    }
    Serial.printf("# records %lu, write errors %u\n", (unsigned long)count, _write_errors);
    Serial.println("# end");
  }
};

#define FM_FLOOD_LOG_EV(ev, pkt, extra)  _fm_flog.add(FloodLog::ev, pkt, getRTCClock()->getCurrentTime(), (uint32_t)(extra))
#define FM_FLOOD_LOG_MODE(offgrid, freq, bw, sf)  _fm_flog.mode(getRTCClock()->getCurrentTime(), offgrid, freq, bw, sf)

#else

#define FM_FLOOD_LOG_EV(ev, pkt, extra)  do { } while (0)
#define FM_FLOOD_LOG_MODE(offgrid, freq, bw, sf)  do { } while (0)

#endif
