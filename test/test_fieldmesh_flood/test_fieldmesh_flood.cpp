// FieldMesh: tests for the Off-Grid managed flooding rules (examples/companion_radio/FloodPolicy.h).
// Timing follows Meshtastic (slot, contention window from the SNR, late window of ROUTER_LATE);
// only adverts are dropped when someone else relayed them first, never messages or SOS. A wrong
// value here means relays that talk over each other, delays that overflow the dispatcher, or
// lost messages - so the formulas and the queue helpers are pinned down here.

#include <gtest/gtest.h>

#include <vector>

#include "../../examples/companion_radio/FloodPolicy.h"

using namespace mesh;

namespace {

// Minimal outbound queue with the PacketManager interface (only the outbound part is used).
class FakePacketManager : public PacketManager {
  static const int POOL = 8;
  Packet _pool[POOL];
  std::vector<Packet*> _free_list;
  std::vector<Packet*> _queue;

public:
  FakePacketManager() { for (int i = 0; i < POOL; i++) _free_list.push_back(&_pool[i]); }

  Packet* allocNew() override {
    if (_free_list.empty()) return NULL;
    Packet* p = _free_list.back();
    _free_list.pop_back();
    return p;
  }
  void free(Packet* packet) override { if (packet) _free_list.push_back(packet); }
  void queueOutbound(Packet* packet, uint8_t, uint32_t) override { _queue.push_back(packet); }
  Packet* getNextOutbound(uint32_t) override { return NULL; }
  int getOutboundCount(uint32_t) const override { return (int)_queue.size(); }
  int getOutboundTotal() const override { return (int)_queue.size(); }
  int getFreeCount() const override { return (int)_free_list.size(); }
  Packet* getOutboundByIdx(int i) override { return (i >= 0 && i < (int)_queue.size()) ? _queue[i] : NULL; }
  Packet* removeOutboundByIdx(int i) override {
    if (i < 0 || i >= (int)_queue.size()) return NULL;
    Packet* p = _queue[i];
    _queue.erase(_queue.begin() + i);
    return p;
  }
  void queueInbound(Packet*, uint32_t) override { }
  Packet* getNextInbound(uint32_t) override { return NULL; }

  static int poolSize() { return POOL; }
};

// Packet with route type, payload type, a payload seed (decides the hash) and path hash count.
void fill(Packet* p, uint8_t route, uint8_t type, uint8_t seed, uint8_t path_hashes) {
  p->header = route | (type << PH_TYPE_SHIFT);
  for (int i = 0; i < 12; i++) p->payload[i] = (uint8_t)(seed * 31 + i * 7);
  p->payload_len = 12;
  p->path_len = path_hashes;   // hash size 1
  for (int i = 0; i < path_hashes; i++) p->path[i] = (uint8_t)(0xA0 + i);
}

Packet* queue(FakePacketManager& mgr, uint8_t route, uint8_t type, uint8_t seed, uint8_t path_hashes) {
  Packet* p = mgr.allocNew();
  fill(p, route, type, seed, path_hashes);
  mgr.queueOutbound(p, path_hashes, 0);
  return p;
}

void hashOf(uint8_t route, uint8_t type, uint8_t seed, uint8_t path_hashes, uint8_t* hash) {
  Packet p;
  fill(&p, route, type, seed, path_hashes);
  p.calculatePacketHash(hash);
}

const uint32_t SLOT_SF11 = 81;

}  // namespace

// ── slot time ───────────────────────────────────────────────────────────────────

TEST(FieldMeshFlood, SlotTimeMatchesMeshtastic) {
  // (4 + 0.5) * 2^SF / BW + 7.6 ms, truncated
  EXPECT_EQ(81u, fmFloodSlotMs(11, 125.0f));    // FieldMesh Off-Grid preset
  EXPECT_EQ(44u, fmFloodSlotMs(10, 125.0f));
  EXPECT_EQ(26u, fmFloodSlotMs(8, 62.5f));
  EXPECT_EQ(155u, fmFloodSlotMs(12, 125.0f));
  EXPECT_EQ(12u, fmFloodSlotMs(7, 125.0f));
}

// ── contention window ───────────────────────────────────────────────────────────

TEST(FieldMeshFlood, ContentionWindowEnds) {
  EXPECT_EQ(3, fmFloodCwSize(-20.0f));
  EXPECT_EQ(8, fmFloodCwSize(10.0f));
}

TEST(FieldMeshFlood, ContentionWindowIsClampedOutsideTheSnrRange) {
  EXPECT_EQ(3, fmFloodCwSize(-30.0f));
  EXPECT_EQ(3, fmFloodCwSize(-26.0f));   // Arduino map() would give 2 here
  EXPECT_EQ(8, fmFloodCwSize(16.0f));    // Arduino map() would give 9 here
  EXPECT_EQ(8, fmFloodCwSize(40.0f));
}

TEST(FieldMeshFlood, ContentionWindowUsesArduinoMapArithmetic) {
  // SNR truncated toward zero, integer division - same steps as Meshtastic's map(snr, -20, 10, 3, 8)
  EXPECT_EQ(3, fmFloodCwSize(-15.0f));
  EXPECT_EQ(4, fmFloodCwSize(-14.9f));   // truncated to -14
  EXPECT_EQ(4, fmFloodCwSize(-14.0f));
  EXPECT_EQ(6, fmFloodCwSize(0.0f));
  EXPECT_EQ(6, fmFloodCwSize(3.9f));     // truncated to 3
  EXPECT_EQ(7, fmFloodCwSize(4.0f));
  EXPECT_EQ(7, fmFloodCwSize(9.9f));     // truncated to 9: 29 * 5 / 30 = 4
  EXPECT_EQ(8, fmFloodCwSize(10.0f));
}

TEST(FieldMeshFlood, ContentionWindowGrowsWithSnr) {
  uint8_t last = 0;
  for (float snr = -30.0f; snr <= 20.0f; snr += 0.25f) {
    uint8_t cw = fmFloodCwSize(snr);
    EXPECT_GE(cw, FM_FLOOD_CW_MIN) << snr;
    EXPECT_LE(cw, FM_FLOOD_CW_MAX) << snr;
    EXPECT_GE(cw, last) << snr;
    last = cw;
  }
}

// ── relay delay (client) ────────────────────────────────────────────────────────

TEST(FieldMeshFlood, RelayDelayStartsAfterTheRouterWindow) {
  // rnd = 0 -> exactly 2 * CWmax = 16 slots
  EXPECT_EQ(16u * SLOT_SF11, fmFloodRelayDelay(-20.0f, SLOT_SF11, 0));
  EXPECT_EQ(16u * SLOT_SF11, fmFloodRelayDelay(10.0f, SLOT_SF11, 0));
}

TEST(FieldMeshFlood, RelayDelayStaysInsideTheContentionWindow) {
  const float snrs[] = { -25.0f, -20.0f, -10.0f, 0.0f, 5.0f, 10.0f, 15.0f };
  for (float snr : snrs) {
    uint32_t cw = 1UL << fmFloodCwSize(snr);
    for (uint32_t rnd = 0; rnd < 70000; rnd += 997) {
      uint32_t d = fmFloodRelayDelay(snr, SLOT_SF11, rnd);
      EXPECT_EQ(0u, d % SLOT_SF11) << snr << " " << rnd;   // whole slots
      EXPECT_GE(d, 16u * SLOT_SF11) << snr << " " << rnd;
      EXPECT_LT(d, (16u + cw) * SLOT_SF11) << snr << " " << rnd;
    }
  }
}

TEST(FieldMeshFlood, WeakSignalRelaysEarlierThanStrongSignal) {
  // largest possible delay: weak 16 + 7 slots, strong 16 + 255 slots
  uint32_t weak_max = 0, strong_max = 0;
  for (uint32_t rnd = 0; rnd < 512; rnd++) {
    uint32_t w = fmFloodRelayDelay(-20.0f, SLOT_SF11, rnd);
    uint32_t s = fmFloodRelayDelay(10.0f, SLOT_SF11, rnd);
    if (w > weak_max) weak_max = w;
    if (s > strong_max) strong_max = s;
  }
  EXPECT_EQ((16u + 7u) * SLOT_SF11, weak_max);
  EXPECT_EQ((16u + 255u) * SLOT_SF11, strong_max);
}

TEST(FieldMeshFlood, LongestDelayFitsTheDispatcherDelayField) {
  // ACTION_RETRANSMIT_DELAYED keeps the delay in 24 bits
  EXPECT_LE(fmFloodRelayDelay(40.0f, fmFloodSlotMs(12, 125.0f), 255), 0xFFFFFFu);
  EXPECT_LE(fmFloodLateDelay(40.0f, fmFloodSlotMs(12, 125.0f)), 0xFFFFFFu);
  EXPECT_EQ(272u * 81u, fmFloodLateDelay(40.0f, SLOT_SF11));   // 22 s at SF11/125
}

// ── late window (repeater) ──────────────────────────────────────────────────────

TEST(FieldMeshFlood, LateWindowIsTheWorstClientDelay) {
  EXPECT_EQ((16u + 8u) * SLOT_SF11, fmFloodLateDelay(-20.0f, SLOT_SF11));
  EXPECT_EQ((16u + 256u) * SLOT_SF11, fmFloodLateDelay(10.0f, SLOT_SF11));
}

TEST(FieldMeshFlood, LateWindowIsAfterEveryClientRelay) {
  for (float snr = -25.0f; snr <= 15.0f; snr += 0.5f) {
    uint32_t late = fmFloodLateDelay(snr, SLOT_SF11);
    for (uint32_t rnd = 0; rnd < 300; rnd++) {
      EXPECT_GT(late, fmFloodRelayDelay(snr, SLOT_SF11, rnd)) << snr << " " << rnd;
    }
  }
}

// ── what may be cancelled ───────────────────────────────────────────────────────

TEST(FieldMeshFlood, OnlyAdvertsAreCancelled) {
  for (uint8_t type = 0; type < 16; type++) {
    EXPECT_EQ(type == PAYLOAD_TYPE_ADVERT, fmFloodCancelOnDuplicate(type)) << (int)type;
  }
  // explicitly: channel messages (incl. SOS), direct messages and ACKs are always relayed
  EXPECT_FALSE(fmFloodCancelOnDuplicate(PAYLOAD_TYPE_GRP_TXT));
  EXPECT_FALSE(fmFloodCancelOnDuplicate(PAYLOAD_TYPE_TXT_MSG));
  EXPECT_FALSE(fmFloodCancelOnDuplicate(PAYLOAD_TYPE_ACK));
}

// ── outbound queue ──────────────────────────────────────────────────────────────

TEST(FieldMeshFlood, FindsOurQueuedRelayDespiteTheLongerPath) {
  FakePacketManager mgr;
  queue(mgr, ROUTE_TYPE_FLOOD, PAYLOAD_TYPE_ADVERT, 1, 0);   // own advert
  queue(mgr, ROUTE_TYPE_FLOOD, PAYLOAD_TYPE_ADVERT, 2, 3);   // our relay of advert 2 (path +1)

  uint8_t heard[MAX_HASH_SIZE];
  hashOf(ROUTE_TYPE_FLOOD, PAYLOAD_TYPE_ADVERT, 2, 3, heard);   // someone else's relay, path 3
  EXPECT_EQ(1, fmFloodFindQueuedRelay(mgr, heard));
  hashOf(ROUTE_TYPE_FLOOD, PAYLOAD_TYPE_ADVERT, 2, 5, heard);   // even longer path, same packet
  EXPECT_EQ(1, fmFloodFindQueuedRelay(mgr, heard));
}

TEST(FieldMeshFlood, NeverFindsAnOwnOriginal) {
  FakePacketManager mgr;
  queue(mgr, ROUTE_TYPE_FLOOD, PAYLOAD_TYPE_ADVERT, 7, 0);   // own original, path 0

  uint8_t heard[MAX_HASH_SIZE];
  hashOf(ROUTE_TYPE_FLOOD, PAYLOAD_TYPE_ADVERT, 7, 1, heard);   // somebody relays our advert
  EXPECT_EQ(-1, fmFloodFindQueuedRelay(mgr, heard));
}

TEST(FieldMeshFlood, DoesNotFindOtherPackets) {
  FakePacketManager mgr;
  queue(mgr, ROUTE_TYPE_FLOOD, PAYLOAD_TYPE_ADVERT, 3, 2);
  queue(mgr, ROUTE_TYPE_DIRECT, PAYLOAD_TYPE_ADVERT, 4, 2);   // direct route: not a flood relay

  uint8_t heard[MAX_HASH_SIZE];
  hashOf(ROUTE_TYPE_FLOOD, PAYLOAD_TYPE_ADVERT, 9, 2, heard);   // different advert
  EXPECT_EQ(-1, fmFloodFindQueuedRelay(mgr, heard));
  hashOf(ROUTE_TYPE_FLOOD, PAYLOAD_TYPE_GRP_TXT, 3, 2, heard);  // same payload, other type
  EXPECT_EQ(-1, fmFloodFindQueuedRelay(mgr, heard));
  hashOf(ROUTE_TYPE_FLOOD, PAYLOAD_TYPE_ADVERT, 4, 2, heard);   // matches only the direct packet
  EXPECT_EQ(-1, fmFloodFindQueuedRelay(mgr, heard));
}

TEST(FieldMeshFlood, EmptyQueueFindsNothing) {
  FakePacketManager mgr;
  uint8_t heard[MAX_HASH_SIZE];
  hashOf(ROUTE_TYPE_FLOOD, PAYLOAD_TYPE_ADVERT, 1, 1, heard);
  EXPECT_EQ(-1, fmFloodFindQueuedRelay(mgr, heard));
  EXPECT_EQ(0, fmFloodDropQueuedRelays(mgr));
}

TEST(FieldMeshFlood, DropQueuedRelaysKeepsOwnPacketsAndReturnsMemory) {
  FakePacketManager mgr;
  Packet* own_advert = queue(mgr, ROUTE_TYPE_FLOOD, PAYLOAD_TYPE_ADVERT, 1, 0);
  queue(mgr, ROUTE_TYPE_FLOOD, PAYLOAD_TYPE_ADVERT, 2, 1);            // relay
  Packet* own_msg = queue(mgr, ROUTE_TYPE_FLOOD, PAYLOAD_TYPE_GRP_TXT, 3, 0);
  queue(mgr, ROUTE_TYPE_FLOOD, PAYLOAD_TYPE_GRP_TXT, 4, 2);           // relay of a message
  Packet* direct = queue(mgr, ROUTE_TYPE_DIRECT, PAYLOAD_TYPE_TXT_MSG, 5, 2);
  queue(mgr, ROUTE_TYPE_TRANSPORT_FLOOD, PAYLOAD_TYPE_ADVERT, 6, 1);  // scoped flood relay
  EXPECT_EQ(FakePacketManager::poolSize() - 6, mgr.getFreeCount());

  EXPECT_EQ(3, fmFloodDropQueuedRelays(mgr));
  ASSERT_EQ(3, mgr.getOutboundTotal());
  EXPECT_EQ(own_advert, mgr.getOutboundByIdx(0));
  EXPECT_EQ(own_msg, mgr.getOutboundByIdx(1));
  EXPECT_EQ(direct, mgr.getOutboundByIdx(2));
  EXPECT_EQ(FakePacketManager::poolSize() - 3, mgr.getFreeCount());   // dropped packets are freed
}

// ── late set (repeater: move a relay only once) ────────────────────────────────

TEST(FieldMeshFlood, LateSetRemembersHashes) {
  FmFloodLateSet set;
  uint8_t a[MAX_HASH_SIZE], b[MAX_HASH_SIZE];
  hashOf(ROUTE_TYPE_FLOOD, PAYLOAD_TYPE_ADVERT, 1, 1, a);
  hashOf(ROUTE_TYPE_FLOOD, PAYLOAD_TYPE_ADVERT, 2, 1, b);
  EXPECT_FALSE(set.contains(a));
  set.add(a);
  EXPECT_TRUE(set.contains(a));
  EXPECT_FALSE(set.contains(b));
}

TEST(FieldMeshFlood, LateSetForgetsTheOldestAfterEightEntries) {
  FmFloodLateSet set;
  uint8_t h[9][MAX_HASH_SIZE];
  for (int i = 0; i < 9; i++) {
    memset(h[i], 0, sizeof(h[i]));
    h[i][0] = (uint8_t)(i + 1);   // distinct, non-zero 4-byte prefix
    set.add(h[i]);
  }
  EXPECT_FALSE(set.contains(h[0]));   // overwritten by the 9th
  for (int i = 1; i < 9; i++) EXPECT_TRUE(set.contains(h[i])) << i;
}

TEST(FieldMeshFlood, LateSetIgnoresAZeroPrefix) {
  // empty slots are 0 - a hash starting with 4 zero bytes must not look "already moved"
  FmFloodLateSet set;
  uint8_t zero[MAX_HASH_SIZE] = {0};
  EXPECT_FALSE(set.contains(zero));
}
