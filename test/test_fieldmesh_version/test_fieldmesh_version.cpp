// FieldMesh: tests for the version text (examples/companion_radio/FieldMeshVersion.h, #17).
// The app shows this text as the firmware version; its field holds 19 characters.

#include <gtest/gtest.h>

#include "../../examples/companion_radio/FieldMeshVersion.h"

namespace {

std::string app(const char* meshcore, const char* fm, size_t sz = FM_APP_VERSION_FIELD) {
  char out[64];
  memset(out, 'x', sizeof(out));
  fmAppVersionText(out, sz, meshcore, fm);
  return out;
}

std::string mc(const char* meshcore, size_t sz = 16) {
  char out[64];
  fmMeshCoreVersion(out, sz, meshcore);
  return out;
}

}  // namespace

TEST(FieldMeshVersion, ReleaseVersion) {
  EXPECT_EQ("v1.18.0-fm2.2.0", app("v1.18.0", "v2.2.0"));
  EXPECT_EQ("v1.18.0-fm2.10.0", app("v1.18.0", "v2.10.0"));
}

TEST(FieldMeshVersion, LeadingVIsOptional) {
  EXPECT_EQ("v1.18.0-fm2.2.0", app("v1.18.0", "2.2.0"));
}

TEST(FieldMeshVersion, CommitSuffixOfMeshCoreIsDropped) {
  // upstream build.sh builds "v1.18.0-<commit>"
  EXPECT_EQ("v1.18.0", mc("v1.18.0-abc1234"));
  EXPECT_EQ("v1.18.0-fm2.2.0", app("v1.18.0-abc1234", "v2.2.0"));
}

TEST(FieldMeshVersion, LongVersionsAreCutToTheAppField) {
  std::string t = app("v1.18.0", "v2.2.0-test1");
  EXPECT_EQ(FM_APP_VERSION_FIELD - 1, (int)t.size());
  EXPECT_EQ("v1.18.0-fm2.2.0-tes", t);
  EXPECT_EQ("v1.18.0-fmdev-eff17", app("v1.18.0", "dev-eff17b3"));
}

TEST(FieldMeshVersion, SmallBufferStaysTerminated) {
  EXPECT_EQ("v1.18", app("v1.18.0", "v2.2.0", 6));
  EXPECT_EQ("", app("v1.18.0", "v2.2.0", 1));
  EXPECT_EQ("v1", mc("v1.18.0", 3));
}

TEST(FieldMeshVersion, DevDefault) {
  EXPECT_STREQ("dev", FIELDMESH_VERSION);
  EXPECT_EQ("v1.18.0-fmdev", app("v1.18.0", FIELDMESH_VERSION));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
