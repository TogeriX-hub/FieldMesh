#pragma once

// FieldMesh v6.2 diagnostics: flooding events as CSV lines on the USB serial port.
// Only compiled with -D FM_FLOOD_LOG=1 (local test builds, never in releases); shared with the
// FieldMesh repeater. Used to check the Off-Grid managed flooding on real hardware: the logs of
// all devices are recorded at the same time (host timestamp per line) and matched by packet hash.
//
// Line format:
//   FMF,<uptime ms>,<rtc>,<event>,<hash>,<type>,<route>,<path>,<snr>,<extra>
//     hash   first 4 bytes of Packet::calculatePacketHash() (same on every device, ignores the path)
//     type   payload type (4 = advert, 5 = channel message, 2 = direct message, 3 = ACK, 8 = path)
//     route  F = flood, D = direct
//     path   path hash count (0 = original from the sender)
//     snr    SNR of the received packet in dB (relays keep the SNR of the copy they relay)
//   events: RX   packet received (every copy, before the duplicate check)   extra = length
//           TX   packet sent                                               extra = length
//           TXF  send failed                                               extra = length
//           Q    flood relay queued                                        extra = delay ms
//           SKIP queued advert relay dropped, another node relayed first (companion)
//           LATE queued relay moved to the late window (repeater)          extra = new delay ms
//           NET  network change, queued relays dropped                     extra = count
//   FMF,<uptime ms>,<rtc>,MODE,<off-grid 0/1>,<freq>,<bw>,<sf>,<node name>
//     at boot and after every network change (Off-Grid on/off, radio params from the app)

#ifdef FM_FLOOD_LOG

#include <Arduino.h>
#include <Packet.h>

inline void fmFloodLog(const char* ev, const mesh::Packet* p, uint32_t rtc, long extra) {
  char h[9] = "-";
  unsigned type = 0;
  char route = '-';
  int path = -1;
  float snr = 0;
  if (p) {
    uint8_t hash[MAX_HASH_SIZE];
    p->calculatePacketHash(hash);
    snprintf(h, sizeof(h), "%02X%02X%02X%02X", hash[0], hash[1], hash[2], hash[3]);
    type = p->getPayloadType();
    route = p->isRouteFlood() ? 'F' : 'D';
    path = p->getPathHashCount();
    snr = p->getSNR();
  }
  Serial.printf("FMF,%lu,%lu,%s,%s,%u,%c,%d,%.2f,%ld\n", (unsigned long)millis(), (unsigned long)rtc,
                ev, h, type, route, path, snr, extra);
}

inline void fmFloodLogMode(uint32_t rtc, bool offgrid, float freq, float bw, uint8_t sf, const char* name) {
  Serial.printf("FMF,%lu,%lu,MODE,%d,%.4f,%.1f,%u,%s\n", (unsigned long)millis(), (unsigned long)rtc,
                offgrid ? 1 : 0, freq, bw, (unsigned)sf, name ? name : "");
}

#define FM_FLOOD_LOG_EV(ev, pkt, extra)  fmFloodLog(ev, pkt, getRTCClock()->getCurrentTime(), (long)(extra))
#define FM_FLOOD_LOG_MODE(offgrid, freq, bw, sf, name) \
  fmFloodLogMode(getRTCClock()->getCurrentTime(), offgrid, freq, bw, sf, name)

#else

#define FM_FLOOD_LOG_EV(ev, pkt, extra)  do { } while (0)
#define FM_FLOOD_LOG_MODE(offgrid, freq, bw, sf, name)  do { } while (0)

#endif
