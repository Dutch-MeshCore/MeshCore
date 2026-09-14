#include <gtest/gtest.h>
#include "helpers/OtaChannel.h"

// The three base URLs are provided as -D macros by the test env (see platformio.ini).
TEST(OtaChannel, ResolvesNativeToBaseMacro) {
  EXPECT_STREQ(ota_resolve_base(OTA_CH_NATIVE), "https://stable.example/mqtt/v");
}
TEST(OtaChannel, ResolvesStable) {
  EXPECT_STREQ(ota_resolve_base(OTA_CH_STABLE), "https://stable.example/mqtt/v");
}
TEST(OtaChannel, ResolvesDev) {
  EXPECT_STREQ(ota_resolve_base(OTA_CH_DEV), "https://dev.example/mqtt/dev/v");
}
TEST(OtaChannel, ParseKnownKeywords) {
  uint8_t ch = 99;
  EXPECT_TRUE(ota_parse_channel("stable", &ch));  EXPECT_EQ(ch, OTA_CH_STABLE);
  EXPECT_TRUE(ota_parse_channel("dev", &ch));     EXPECT_EQ(ch, OTA_CH_DEV);
  EXPECT_TRUE(ota_parse_channel("default", &ch)); EXPECT_EQ(ch, OTA_CH_NATIVE);
}
TEST(OtaChannel, ParseRejectsUnknownAndLeavesOutputUntouched) {
  uint8_t ch = 7;
  EXPECT_FALSE(ota_parse_channel("beta", &ch));
  EXPECT_EQ(ch, 7);
}
TEST(OtaChannel, NameLabels) {
  EXPECT_STREQ(ota_channel_name(OTA_CH_NATIVE), "native");
  EXPECT_STREQ(ota_channel_name(OTA_CH_STABLE), "stable");
  EXPECT_STREQ(ota_channel_name(OTA_CH_DEV), "dev");
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
