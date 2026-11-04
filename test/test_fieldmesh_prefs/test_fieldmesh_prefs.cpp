// FieldMesh: tests for the FieldMesh part of NodePrefs (JSON object "fm").
// The Off-Grid toggle saves the normal radio parameters in "fm" and restores them when
// Off-Grid is switched off, so they must survive a save/load cycle unchanged and must
// never end up in the upstream objects.

#include <gtest/gtest.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "../../examples/companion_radio/NodePrefs.h"

// CommonRadioPrefs.cpp needs the board's target.h and is not part of the native build.
// Its key/value access (FEM gains) is not under test here - stubs provide the vtable.
bool CommonRadioPrefs::getByKey(const char*, char*, size_t) { return false; }
bool CommonRadioPrefs::setByKey(const char*, const char*) { return false; }

namespace {

class ReplayStream : public Stream {
  std::string _text;
  size_t _pos = 0;

public:
  explicit ReplayStream(const std::string& text) : _text(text) { }

  int available() override { return (int)(_text.size() - _pos); }
  int read() override { return _pos < _text.size() ? (uint8_t)_text[_pos++] : -1; }
  int peek() override { return _pos < _text.size() ? (uint8_t)_text[_pos] : -1; }
};

class CaptureStream : public Stream {
  std::string _text;

  size_t emit(const char* text) { return write(reinterpret_cast<const uint8_t*>(text), strlen(text)); }
  size_t emitSigned(long long value) {
    char text[24];
    snprintf(text, sizeof(text), "%lld", value);
    return emit(text);
  }
  size_t emitUnsigned(unsigned long long value) {
    char text[24];
    snprintf(text, sizeof(text), "%llu", value);
    return emit(text);
  }

public:
  size_t write(uint8_t value) override {
    _text.push_back(static_cast<char>(value));
    return 1;
  }
  size_t write(const uint8_t* buffer, size_t size) override {
    _text.append(reinterpret_cast<const char*>(buffer), size);
    return size;
  }

  size_t print(unsigned char value, int = DEC) override { return emitUnsigned(value); }
  size_t print(int value, int = DEC) override { return emitSigned(value); }
  size_t print(unsigned int value, int = DEC) override { return emitUnsigned(value); }
  size_t print(long value, int = DEC) override { return emitSigned(value); }
  size_t print(unsigned long value, int = DEC) override { return emitUnsigned(value); }
  size_t print(long long value, int = DEC) override { return emitSigned(value); }
  size_t print(unsigned long long value, int = DEC) override { return emitUnsigned(value); }
  size_t print(double value, int digits = 2) override {
    char text[40];
    snprintf(text, sizeof(text), "%.*f", digits, value);
    return emit(text);
  }

  const std::string& text() const { return _text; }
};

// prefs as they look while Off-Grid is active: radio on the Off-Grid channel,
// the user's normal parameters (EU/UK Narrow preset) parked in "fm"
void fillOffGridPrefs(NodePrefs& p) {
  strcpy(p.node_name, "Tobi | M1");
  p.freq = 869.4625f;
  p.bw = 125.0f;
  p.sf = 11;
  p.cr = 5;
  p.normal_freq = 869.618f;
  p.normal_bw = 62.5f;
  p.normal_sf = 8;
  p.normal_cr = 8;
  p.setRepeatEn(true);
}

std::string save(NodePrefs& p) {
  CaptureStream out;
  EXPECT_TRUE(p.saveSerial(out));
  return out.text();
}

// removes the "fm" object (and its leading comma) - a file written by stock MeshCore
std::string withoutFm(const std::string& json) {
  size_t start = json.find(",fm:{");
  if (start == std::string::npos) return json;
  size_t end = json.find('}', start);
  return json.substr(0, start) + json.substr(end + 1);
}

}  // namespace

TEST(FieldMeshPrefs, NormalRadioParamsSurviveSaveAndLoad) {
  NodePrefs saved;
  fillOffGridPrefs(saved);

  NodePrefs loaded;
  ReplayStream in(save(saved));
  ASSERT_TRUE(loaded.loadSerial(in));

  EXPECT_FLOAT_EQ(869.618f, loaded.normal_freq);
  EXPECT_FLOAT_EQ(62.5f, loaded.normal_bw);
  EXPECT_EQ(8, loaded.normal_sf);
  EXPECT_EQ(8, loaded.normal_cr);
  // the active (Off-Grid) parameters are untouched by the FieldMesh fields
  EXPECT_FLOAT_EQ(869.4625f, loaded.freq);
  EXPECT_FLOAT_EQ(125.0f, loaded.bw);
  EXPECT_EQ(11, loaded.sf);
  EXPECT_EQ(5, loaded.cr);
  EXPECT_STREQ("Tobi | M1", loaded.node_name);
}

TEST(FieldMeshPrefs, CommonPresetFrequenciesKeepTheirPrecision) {
  // the file stores floats with 4 decimals - every preset must come back exactly
  const float freqs[] = { 433.650f, 869.525f, 869.618f, 906.875f, 910.525f, 915.8f, 916.575f, 927.875f };
  for (float f : freqs) {
    NodePrefs saved;
    saved.normal_freq = f;
    NodePrefs loaded;
    ReplayStream in(save(saved));
    ASSERT_TRUE(loaded.loadSerial(in));
    EXPECT_FLOAT_EQ(f, loaded.normal_freq) << "frequency " << f;
  }
}

TEST(FieldMeshPrefs, FieldMeshKeysLiveOnlyInTheFmObject) {
  NodePrefs p;
  fillOffGridPrefs(p);
  std::string json = save(p);

  size_t fm = json.find("fm:{");
  ASSERT_NE(std::string::npos, fm) << json;
  size_t fm_end = json.find('}', fm);
  ASSERT_NE(std::string::npos, fm_end);

  const char* keys[] = { "n_freq:", "n_bw:", "n_sf:", "n_cr:" };
  for (const char* key : keys) {
    size_t pos = json.find(key);
    ASSERT_NE(std::string::npos, pos) << key << " missing in " << json;
    EXPECT_GT(pos, fm) << key << " written outside \"fm\": " << json;
    EXPECT_LT(pos, fm_end) << key << " written outside \"fm\": " << json;
    EXPECT_EQ(pos, json.rfind(key)) << key << " written twice: " << json;
  }
}

TEST(FieldMeshPrefs, StockMeshCoreFileWithoutFmLoadsWithDefaults) {
  // e.g. first start of FieldMesh on a device that ran stock MeshCore before
  NodePrefs stock;
  fillOffGridPrefs(stock);
  std::string json = withoutFm(save(stock));
  ASSERT_EQ(std::string::npos, json.find("fm:{")) << json;

  NodePrefs loaded;
  ReplayStream in(json);
  ASSERT_TRUE(loaded.loadSerial(in));

  EXPECT_STREQ("Tobi | M1", loaded.node_name);
  EXPECT_FLOAT_EQ(869.4625f, loaded.freq);
  EXPECT_EQ(0.0f, loaded.normal_freq);   // 0 = "nothing saved", Off-Grid saves fresh values
  EXPECT_EQ(0.0f, loaded.normal_bw);
  EXPECT_EQ(0, loaded.normal_sf);
  EXPECT_EQ(0, loaded.normal_cr);
}

TEST(FieldMeshPrefs, UnknownKeysInFmAreIgnored) {
  // a file written by a newer FieldMesh (downgrade) may carry keys this version doesn't know
  NodePrefs newer;
  fillOffGridPrefs(newer);
  std::string json = save(newer);
  size_t fm = json.find("fm:{");
  ASSERT_NE(std::string::npos, fm);
  json.insert(fm + 4, "x_future:7,x_text:\"abc\",");

  NodePrefs loaded;
  ReplayStream in(json);
  ASSERT_TRUE(loaded.loadSerial(in)) << json;
  EXPECT_FLOAT_EQ(869.618f, loaded.normal_freq);
  EXPECT_FLOAT_EQ(62.5f, loaded.normal_bw);
  EXPECT_EQ(8, loaded.normal_sf);
  EXPECT_EQ(8, loaded.normal_cr);
}

TEST(FieldMeshPrefs, RepeatFlagRoundTrips) {
  // Off-Grid = Client Repeat on; must be restored after a reboot in Off-Grid mode
  for (bool en : { true, false }) {
    NodePrefs saved;
    saved.setRepeatEn(en);
    EXPECT_EQ(en, saved.isRepeatEn());

    NodePrefs loaded;
    loaded.setRepeatEn(!en);
    ReplayStream in(save(saved));
    ASSERT_TRUE(loaded.loadSerial(in));
    EXPECT_EQ(en, loaded.isRepeatEn());
  }
}

TEST(FieldMeshPrefs, RepeatIsOffByDefault) {
  NodePrefs p;
  EXPECT_FALSE(p.isRepeatEn());
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
