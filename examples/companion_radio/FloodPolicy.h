#pragma once
// FieldMesh Off-Grid managed flooding (v6.2), only used while Off-Grid / repeat is on.
//
// Timing and late window follow Meshtastic (firmware commit 332b5b0, src/mesh/RadioInterface.cpp:
// computeSlotTimeMsec, getCWsize, getTxDelayMsecWeighted, getTxDelayMsecWeightedWorst):
//   slot          = (CAD symbols + 0.5) * symbol time + 7.6 ms     (SX126x/LR11x0: 4 CAD symbols)
//   CW            = map(snr, -20, 10, 3, 8)                        (weak signal -> small window)
//   client delay  = 2 * CWmax * slot + random(0, 2^CW) * slot
//   late window   = 2 * CWmax * slot + 2^CW * slot                 (ROUTER_LATE, used by the repeater)
// One deliberate difference: CW is clamped to 3..8 (Meshtastic does not clamp, SNR >= 16 dB would
// give CW 9). This keeps the longest relay delay at 272 slots (22 s at SF11/125).
//
// Header-only: unit-tested natively (test/test_fieldmesh_flood) and shared with the FieldMesh
// repeater. Background and simulation: claude/RECHERCHE_Flooding_OffGrid.md (project docs).

#include <stdint.h>
#include <string.h>
#include <Packet.h>
#include <Dispatcher.h>

#define FM_FLOOD_CW_MIN        3
#define FM_FLOOD_CW_MAX        8
#define FM_FLOOD_SNR_MIN     (-20)       // dB
#define FM_FLOOD_SNR_MAX       10        // dB
#define FM_FLOOD_CAD_SYMBOLS    4        // SX126x / LR11x0 CAD scan length
#define FM_FLOOD_CAD_MAX_MS  30000UL     // wait up to 30 s for a free channel instead of forcing after 4 s

// Slot time in ms. Truncated like Meshtastic (uint32_t return). SF11/125: 81, SF10/125: 44.
inline uint32_t fmFloodSlotMs(uint8_t sf, float bw_khz) {
  float sym = (float)(1UL << sf) / bw_khz;
  return (uint32_t)((FM_FLOOD_CAD_SYMBOLS + 0.5f) * sym + 0.2f + 0.4f + 7.0f);
}

// Contention window exponent from the SNR (dB) of the received packet.
// Same arithmetic as Arduino map() (SNR truncated to an integer, integer division), then clamped.
inline uint8_t fmFloodCwSize(float snr) {
  int32_t s = (int32_t)snr;
  int32_t cw = (s - FM_FLOOD_SNR_MIN) * (FM_FLOOD_CW_MAX - FM_FLOOD_CW_MIN)
               / (FM_FLOOD_SNR_MAX - FM_FLOOD_SNR_MIN) + FM_FLOOD_CW_MIN;
  if (cw < FM_FLOOD_CW_MIN) return FM_FLOOD_CW_MIN;
  if (cw > FM_FLOOD_CW_MAX) return FM_FLOOD_CW_MAX;
  return (uint8_t)cw;
}

// Relay delay of a client: 2*CWmax slots + random(0, 2^CW) slots. rnd = any random number.
inline uint32_t fmFloodRelayDelay(float snr, uint32_t slot_ms, uint32_t rnd) {
  uint32_t cw = 1UL << fmFloodCwSize(snr);
  return (2 * FM_FLOOD_CW_MAX + (rnd % cw)) * slot_ms;
}

// Late window (repeater, Meshtastic ROUTER_LATE): after the latest client relay for this SNR.
inline uint32_t fmFloodLateDelay(float snr, uint32_t slot_ms) {
  return (2 * FM_FLOOD_CW_MAX + (1UL << fmFloodCwSize(snr))) * slot_ms;
}

// Extra send timeout for flood-routed direct messages and requests while Off-Grid. Every relay
// on the way and back may now wait up to one late window; two worst-case windows are added on top
// of the upstream timeout (SF11/125: +44 s), so a delivered message is not reported as failed.
inline uint32_t fmFloodTimeoutExtraMs(uint32_t slot_ms) {
  return 2 * fmFloodLateDelay(FM_FLOOD_SNR_MAX, slot_ms);
}

// Only adverts are dropped when someone else already relayed them. Messages (incl. SOS), ACKs,
// paths and everything else are always relayed, as today.
inline bool fmFloodCancelOnDuplicate(uint8_t payload_type) {
  return payload_type == PAYLOAD_TYPE_ADVERT;
}

// Our own queued relays: flood packets with at least one path hash. Own originals have path 0.
inline bool fmFloodIsQueuedRelay(const mesh::Packet* q) {
  return q != NULL && q->isRouteFlood() && q->getPathHashCount() > 0;
}

// Index of our queued relay of the packet with this hash (calculatePacketHash ignores the path,
// so our longer path still matches), or -1. Own originals are never matched.
inline int fmFloodFindQueuedRelay(mesh::PacketManager& mgr, const uint8_t* hash) {
  uint8_t h[MAX_HASH_SIZE];
  int n = mgr.getOutboundTotal();
  for (int i = 0; i < n; i++) {
    mesh::Packet* q = mgr.getOutboundByIdx(i);
    if (!fmFloodIsQueuedRelay(q)) continue;
    q->calculatePacketHash(h);
    if (memcmp(h, hash, MAX_HASH_SIZE) == 0) return i;
  }
  return -1;
}

// Drop all queued relays (network change: nothing heard Off-Grid may be relayed into the normal
// mesh and vice versa). Own originals stay. Returns the number of dropped packets.
inline int fmFloodDropQueuedRelays(mesh::PacketManager& mgr) {
  int dropped = 0;
  for (int i = mgr.getOutboundTotal() - 1; i >= 0; i--) {
    if (fmFloodIsQueuedRelay(mgr.getOutboundByIdx(i))) {
      mgr.free(mgr.removeOutboundByIdx(i));
      dropped++;
    }
  }
  return dropped;
}

// Remembers which relays were already moved to the late window (moved only once, like Meshtastic).
// Stores the first 4 bytes of the packet hash in a ring of 8; a collision only means a relay is
// not moved (harmless).
struct FmFloodLateSet {
  uint32_t h[8] = {0};
  uint8_t next = 0;

  bool contains(const uint8_t* hash) const {
    uint32_t v;
    memcpy(&v, hash, 4);
    if (v == 0) return false;
    for (int i = 0; i < 8; i++) if (h[i] == v) return true;
    return false;
  }
  void add(const uint8_t* hash) {
    memcpy(&h[next], hash, 4);
    next = (next + 1) & 7;
  }
};
