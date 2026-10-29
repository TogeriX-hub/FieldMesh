// FieldMesh: tests for the channel message text rules (examples/companion_radio/ChannelText.h).
// The sender name decides the favourite check and what the history shows; the SOS channel
// rule decides whether an "!SOS" triggers the alarm (companion) or goes onto the board
// (FieldMesh repeater).

#include <gtest/gtest.h>

#include "../../examples/companion_radio/ChannelText.h"

namespace {

std::string sender(const char* text, int max_len = 33) {
  char out[64];
  strcpy(out, "untouched");
  if (!fmExtractSenderName(text, out, max_len)) return "<none>";
  return out;
}

}  // namespace

TEST(FieldMeshChannelText, SenderIsEverythingBeforeColonSpace) {
  EXPECT_EQ("Tobi", sender("Tobi: Hello"));
  EXPECT_EQ("Tobi M1", sender("Tobi M1: on my way"));
}

TEST(FieldMeshChannelText, PipeIsPartOfTheName) {
  EXPECT_EQ("Juno48 | Region", sender("Juno48 | Region: Hello"));
  EXPECT_EQ("A | B | C", sender("A | B | C: x"));
}

TEST(FieldMeshChannelText, FirstColonSpaceWins) {
  // the message itself may contain ": "
  EXPECT_EQ("Tobi", sender("Tobi: Time: 12:30"));
  EXPECT_EQ("Tobi", sender("Tobi: !SOS 48.7636,9.1621 bat78%"));
}

TEST(FieldMeshChannelText, ColonWithoutSpaceIsNotASeparator) {
  EXPECT_EQ("Node", sender("Node: 12:30 meet at 10:00"));
  EXPECT_EQ("<none>", sender("12:30 no sender"));
}

TEST(FieldMeshChannelText, NoSenderRecognised) {
  EXPECT_EQ("<none>", sender("plain text without separator"));
  EXPECT_EQ("<none>", sender(": empty name"));
  EXPECT_EQ("<none>", sender(""));
  EXPECT_EQ("<none>", sender(NULL));
}

TEST(FieldMeshChannelText, NameMustFitIncludingTerminator) {
  // node names are at most 31 characters, buffers are 32/33 bytes
  const char* text31 = "abcdefghijklmnopqrstuvwxyz01234: hi";   // 31-char name
  EXPECT_EQ("abcdefghijklmnopqrstuvwxyz01234", sender(text31, 32));
  EXPECT_EQ("<none>", sender(text31, 31));   // no truncation - better no name than a wrong one
}

TEST(FieldMeshChannelText, OutputUntouchedWhenNotRecognised) {
  char out[16] = "keep";
  EXPECT_FALSE(fmExtractSenderName("no separator", out, sizeof(out)));
  EXPECT_STREQ("keep", out);
}

TEST(FieldMeshChannelText, SosChannelNames) {
  const char* sos[] = { "SOS", "sos", "#sos", "#SOS", "TeamSOS", "sos-alpine", "Sos" };
  for (const char* n : sos) EXPECT_TRUE(fmIsSosChannelName(n)) << n;
}

TEST(FieldMeshChannelText, NonSosChannelNames) {
  const char* other[] = { "Public", "#test", "so s", "SO", "", "s0s", "Team" };
  for (const char* n : other) EXPECT_FALSE(fmIsSosChannelName(n)) << n;
  EXPECT_FALSE(fmIsSosChannelName(NULL));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
