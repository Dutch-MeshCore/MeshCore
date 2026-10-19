#include <gtest/gtest.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "helpers/GpsDefault.h"
#include "../../examples/companion_radio/NodePrefs.h"

// CommonRadioPrefs.cpp needs target.h, so it is not in the native build. These tests use no radio keys.
bool CommonRadioPrefs::setByKey(const char*, const char*) { return false; }
bool CommonRadioPrefs::getByKey(const char*, char*, size_t) { return false; }

class ReplayStream : public Stream {
  const char* _text;
  int _pos = 0;
  int _len;

public:
  explicit ReplayStream(const char* text) : _text(text), _len(strlen(text)) { }

  int available() override { return _len - _pos; }
  int read() override { return _pos < _len ? _text[_pos++] : -1; }
  int peek() override { return _pos < _len ? _text[_pos] : -1; }
};

class CaptureStream : public Stream {
  std::string _text;

  size_t emit(long long value) {
    char text[24];
    int length = snprintf(text, sizeof(text), "%lld", value);
    return write(reinterpret_cast<const uint8_t*>(text), length);
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

  size_t print(unsigned char value, int = DEC) override { return emit(value); }
  size_t print(int value, int = DEC) override { return emit(value); }
  size_t print(unsigned int value, int = DEC) override { return emit(value); }
  size_t print(long value, int = DEC) override { return emit(value); }
  size_t print(unsigned long value, int = DEC) override { return emit(value); }
  size_t print(long long value, int = DEC) override { return emit(value); }
  size_t print(unsigned long long value, int = DEC) override { return emit(value); }

  const std::string& text() const { return _text; }
};

TEST(GpsDefault, TurnsGpsOnOnce) {
  uint8_t enabled = 0, applied = 0;
  EXPECT_TRUE(applyGpsDefaultOn(enabled, applied));
  EXPECT_EQ(1, enabled);
  EXPECT_EQ(1, applied);
}

TEST(GpsDefault, KeepsGpsOffAfterTheDefaultWasApplied) {
  uint8_t enabled = 0, applied = 1;
  EXPECT_FALSE(applyGpsDefaultOn(enabled, applied));
  EXPECT_EQ(0, enabled);
  EXPECT_EQ(1, applied);
}

TEST(GpsDefault, PrefsWrittenBeforeTheMarkerExistedGetGpsOn) {
  ReplayStream input("{gps:{en:0,int:0}}");
  NodePrefs prefs;
  ASSERT_TRUE(prefs.loadSerial(input));
  EXPECT_EQ(0, prefs.gps_default_applied);

  EXPECT_TRUE(applyGpsDefaultOn(prefs.gps_enabled, prefs.gps_default_applied));
  EXPECT_EQ(1, prefs.gps_enabled);
}

TEST(GpsDefault, GpsOffChosenAfterTheMarkerSurvivesAReload) {
  NodePrefs saved;
  saved.gps_enabled = 0;
  saved.gps_default_applied = 1;

  CaptureStream output;
  ASSERT_TRUE(saved.saveSerial(output));

  ReplayStream input(output.text().c_str());
  NodePrefs loaded;
  ASSERT_TRUE(loaded.loadSerial(input));
  EXPECT_EQ(1, loaded.gps_default_applied);

  EXPECT_FALSE(applyGpsDefaultOn(loaded.gps_enabled, loaded.gps_default_applied));
  EXPECT_EQ(0, loaded.gps_enabled);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
