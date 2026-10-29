// FieldMesh: tests for the Off-Grid radio parameters (examples/companion_radio/OffGrid.h).
// Since v6.1 the Off-Grid frequency follows the region of the normal frequency, so one
// firmware serves EU and US. A wrong split or a channel outside the legal band would send
// on an illegal frequency, so the limits are checked here against the band edges.

#include <gtest/gtest.h>

#include "../../examples/companion_radio/OffGrid.h"

namespace {

// legal band edges in MHz (README "Off-Grid Mode" / repeat_freq_ranges in MyMesh.cpp)
constexpr float EU_G3_LOW = 869.400f, EU_G3_HIGH = 869.650f;   // SRD sub-band g3, 500 mW ERP, 10 %
constexpr float US_ISM_LOW = 915.000f, US_ISM_HIGH = 928.000f;  // US/AU 915 MHz ISM

}  // namespace

TEST(FieldMeshOffGrid, EuropeanFrequenciesPickTheEuChannel) {
  const float eu[] = { 433.175f, 433.650f, 868.100f, 869.525f, 869.618f, 899.999f };
  for (float f : eu) {
    EXPECT_FALSE(offGridIsUS(f)) << f;
    EXPECT_FLOAT_EQ(OFFGRID_FREQ_EU, offGridFreqFor(f)) << f;
  }
}

TEST(FieldMeshOffGrid, UsAndAustralianFrequenciesPickTheUsChannel) {
  const float us[] = { 900.0f, 906.875f, 910.525f, 915.8f, 916.575f, 927.875f };
  for (float f : us) {
    EXPECT_TRUE(offGridIsUS(f)) << f;
    EXPECT_FLOAT_EQ(OFFGRID_FREQ_US, offGridFreqFor(f)) << f;
  }
}

TEST(FieldMeshOffGrid, UnknownNormalFrequencyFallsBackToEu) {
  // normal_freq is 0 in prefs that never saved Off-Grid parameters
  EXPECT_FLOAT_EQ(OFFGRID_FREQ_EU, offGridFreqFor(0.0f));
}

TEST(FieldMeshOffGrid, OffGridChannelsBelongToTheirOwnRegion) {
  // while Off-Grid is active the label and a second toggle are derived from the
  // Off-Grid frequency itself
  EXPECT_FALSE(offGridIsUS(OFFGRID_FREQ_EU));
  EXPECT_TRUE(offGridIsUS(OFFGRID_FREQ_US));
  EXPECT_FLOAT_EQ(OFFGRID_FREQ_EU, offGridFreqFor(OFFGRID_FREQ_EU));
  EXPECT_FLOAT_EQ(OFFGRID_FREQ_US, offGridFreqFor(OFFGRID_FREQ_US));
  EXPECT_STREQ("EU", offGridRegionLabel(OFFGRID_FREQ_EU));
  EXPECT_STREQ("US", offGridRegionLabel(OFFGRID_FREQ_US));
}

TEST(FieldMeshOffGrid, EuChannelFitsInsideSubBandG3) {
  const float half_bw = OFFGRID_BW / 2000.0f;   // kHz -> MHz
  EXPECT_GE(OFFGRID_FREQ_EU - half_bw, EU_G3_LOW);
  EXPECT_LE(OFFGRID_FREQ_EU + half_bw, EU_G3_HIGH);
}

TEST(FieldMeshOffGrid, UsChannelFitsInsideTheIsmBand) {
  const float half_bw = OFFGRID_BW / 2000.0f;
  EXPECT_GE(OFFGRID_FREQ_US - half_bw, US_ISM_LOW);
  EXPECT_LE(OFFGRID_FREQ_US + half_bw, US_ISM_HIGH);
}

TEST(FieldMeshOffGrid, EuChannelDoesNotOverlapTheEuNarrowPreset) {
  // EU/UK Narrow: 869.618 MHz / BW 62.5 kHz - Off-Grid must not disturb the normal network
  const float offgrid_high = OFFGRID_FREQ_EU + OFFGRID_BW / 2000.0f;
  const float narrow_low = 869.618f - 62.5f / 2000.0f;
  EXPECT_LT(offgrid_high, narrow_low);
}

TEST(FieldMeshOffGrid, ModemSettings) {
  EXPECT_FLOAT_EQ(125.0f, OFFGRID_BW);
  EXPECT_EQ(11, OFFGRID_SF);
  EXPECT_EQ(5, OFFGRID_CR);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
