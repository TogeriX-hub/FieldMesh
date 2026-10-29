// FieldMesh: tests for the tracking send cadence (examples/companion_radio/TrackScheduler.h).
// Values decided in PLAN_v6.1.md: first fix right away, >= 100 m and >= 2 min (60 s while
// moving fast) for a movement send, heartbeat every 10 min, fixes with < 4 satellites ignored.

#include <gtest/gtest.h>

#include "../../examples/companion_radio/TrackScheduler.h"

namespace {

constexpr uint32_t SEC = 1000;
constexpr uint32_t MIN = 60 * SEC;

// Stuttgart, degrees * 1e6
constexpr int32_t LAT = 48775800;
constexpr int32_t LON = 9182900;
// about 111 m per 0.001 degree latitude
constexpr int32_t NORTH_45M = 400;
constexpr int32_t NORTH_50M = 450;
constexpr int32_t NORTH_150M = 1350;

constexpr uint8_t SATS = 8;

using R = TrackScheduler;

// reference distance (haversine, double precision)
double haversineM(int32_t lat1, int32_t lon1, int32_t lat2, int32_t lon2) {
  const double k = M_PI / 180.0 / 1e6;
  double dlat = (lat2 - lat1) * k, dlon = (lon2 - lon1) * k;
  double a = sin(dlat / 2) * sin(dlat / 2) + cos(lat1 * k) * cos(lat2 * k) * sin(dlon / 2) * sin(dlon / 2);
  return 2 * 6371000.0 * atan2(sqrt(a), sqrt(1 - a));
}

// tracking switched on at t0, first fix sent at t0
TrackScheduler startedAt(uint32_t t0) {
  TrackScheduler t;
  t.reset(t0);
  EXPECT_EQ(R::FIRST, t.poll(t0, true, LAT, LON, SATS, true));
  t.sent(t0, true, LAT, LON);
  return t;
}

}  // namespace

TEST(FieldMeshTrack, FirstUsableFixIsSentRightAway) {
  TrackScheduler t;
  t.reset(1000);
  EXPECT_EQ(R::NONE, t.poll(2000, false, 0, 0, 0, false));
  EXPECT_EQ(R::FIRST, t.poll(3000, true, LAT, LON, SATS, true));
}

TEST(FieldMeshTrack, FixWithTooFewSatellitesIsIgnored) {
  TrackScheduler t;
  t.reset(0);
  EXPECT_EQ(R::NONE, t.poll(10 * SEC, true, LAT, LON, R::MIN_SATS - 1, true));
  EXPECT_EQ(R::FIRST, t.poll(20 * SEC, true, LAT, LON, R::MIN_SATS, true));
}

TEST(FieldMeshTrack, SmallMovementSendsNothingBeforeTheHeartbeat) {
  TrackScheduler t = startedAt(0);
  EXPECT_EQ(R::NONE, t.poll(2 * MIN, true, LAT + NORTH_50M, LON, SATS, true));
  EXPECT_EQ(R::NONE, t.poll(8 * MIN, true, LAT + NORTH_50M, LON, SATS, true));
}

TEST(FieldMeshTrack, MovementSendsAfterTheMinimumInterval) {
  TrackScheduler t = startedAt(0);
  // moved 150 m but only 60 s later - too early when walking
  EXPECT_EQ(R::NONE, t.poll(60 * SEC, true, LAT + NORTH_150M, LON, SATS, true));
  // next fix (~115 s) is accepted thanks to the slack
  EXPECT_EQ(R::MOVED, t.poll(115 * SEC, true, LAT + NORTH_150M, LON, SATS, true));
  EXPECT_GE(t.lastDistanceM(), 140);
  EXPECT_LE(t.lastDistanceM(), 160);
}

TEST(FieldMeshTrack, MovingFastSendsEveryMinute) {
  TrackScheduler t = startedAt(0);
  EXPECT_EQ(R::MOVED, t.poll(60 * SEC, true, LAT + NORTH_150M, LON, SATS, true, true));
  t.sent(60 * SEC, true, LAT + NORTH_150M, LON);
  EXPECT_EQ(R::NONE, t.poll(80 * SEC, true, LAT + 2 * NORTH_150M, LON, SATS, true, true));
  EXPECT_EQ(R::MOVED, t.poll(120 * SEC, true, LAT + 2 * NORTH_150M, LON, SATS, true, true));
}

TEST(FieldMeshTrack, DistanceCountsFromTheLastSentPosition) {
  // 3 x 45 m in small steps: no single step reaches 100 m, the sum does
  TrackScheduler t = startedAt(0);
  EXPECT_EQ(R::NONE, t.poll(2 * MIN, true, LAT + NORTH_45M, LON, SATS, true));
  EXPECT_EQ(R::NONE, t.poll(4 * MIN, true, LAT + 2 * NORTH_45M, LON, SATS, true));
  EXPECT_EQ(R::MOVED, t.poll(6 * MIN, true, LAT + 3 * NORTH_45M, LON, SATS, true));
}

TEST(FieldMeshTrack, HeartbeatWithFreshPositionAfterTenMinutes) {
  TrackScheduler t = startedAt(0);
  EXPECT_EQ(R::NONE, t.poll(10 * MIN - 1, false, 0, 0, 0, true));
  EXPECT_EQ(R::HEARTBEAT, t.poll(10 * MIN, false, 0, 0, 0, true));
}

TEST(FieldMeshTrack, HeartbeatWaitsForTheNextFixThenGivesUp) {
  TrackScheduler t = startedAt(0);
  // no fresh position at the heartbeat: wait for the next fix ...
  EXPECT_EQ(R::NONE, t.poll(10 * MIN, false, 0, 0, 0, false));
  EXPECT_EQ(R::HEARTBEAT, t.poll(11 * MIN, true, LAT, LON, SATS, true));

  // ... but at most HEARTBEAT_WAIT_MS, then without position
  TrackScheduler u = startedAt(0);
  EXPECT_EQ(R::NONE, u.poll(10 * MIN + R::HEARTBEAT_WAIT_MS - 1, false, 0, 0, 0, false));
  EXPECT_EQ(R::HEARTBEAT, u.poll(10 * MIN + R::HEARTBEAT_WAIT_MS, false, 0, 0, 0, false));
}

TEST(FieldMeshTrack, HeartbeatCountsFromTheLastSend) {
  TrackScheduler t = startedAt(0);
  EXPECT_EQ(R::MOVED, t.poll(5 * MIN, true, LAT + NORTH_150M, LON, SATS, true));
  t.sent(5 * MIN, true, LAT + NORTH_150M, LON);
  EXPECT_EQ(R::NONE, t.poll(10 * MIN, false, 0, 0, 0, true));
  EXPECT_EQ(R::HEARTBEAT, t.poll(15 * MIN, false, 0, 0, 0, true));
}

TEST(FieldMeshTrack, PositionIsSentAsSoonAsAvailableAfterAHeartbeatWithout) {
  TrackScheduler t;
  t.reset(0);
  EXPECT_EQ(R::HEARTBEAT, t.poll(10 * MIN + R::HEARTBEAT_WAIT_MS, false, 0, 0, 0, false));
  t.sent(10 * MIN + R::HEARTBEAT_WAIT_MS, false, 0, 0);
  // first position ever: no interval, no distance needed
  EXPECT_EQ(R::FIRST, t.poll(13 * MIN, true, LAT, LON, SATS, true));
}

TEST(FieldMeshTrack, SurvivesTheMillisWrapAround) {
  // millis() wraps after ~49.7 days
  const uint32_t t0 = 0xFFFFFFFFu - 30 * SEC;
  TrackScheduler t = startedAt(t0);
  EXPECT_EQ(R::MOVED, t.poll(t0 + 3 * MIN, true, LAT + NORTH_150M, LON, SATS, true));
  EXPECT_EQ(R::NONE, t.poll(t0 + 5 * MIN, false, 0, 0, 0, true));
  EXPECT_EQ(R::HEARTBEAT, t.poll(t0 + 10 * MIN, false, 0, 0, 0, true));
}

TEST(FieldMeshTrack, DistanceApproximation) {
  EXPECT_EQ(0, TrackScheduler::distanceM(LAT, LON, LAT, LON));
  // 0.001 deg latitude = 111.2 m
  EXPECT_NEAR(111, TrackScheduler::distanceM(LAT, LON, LAT + 1000, LON), 1);
  // 0.001 deg longitude at 48.8 deg N = 73.3 m
  EXPECT_NEAR(73, TrackScheduler::distanceM(LAT, LON, LAT, LON + 1000), 1);
  // ~15 km diagonal: within 0.5 % of the haversine distance
  EXPECT_NEAR(haversineM(LAT, LON, 48710600, 9006100),
              TrackScheduler::distanceM(LAT, LON, 48710600, 9006100), 75);
  // far away: clamped to the uint16_t range
  EXPECT_EQ(65535, TrackScheduler::distanceM(LAT, LON, LAT + 1000000, LON));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
