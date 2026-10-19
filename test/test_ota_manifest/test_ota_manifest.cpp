#include <gtest/gtest.h>

#include <cstring>
#include <string>

// Manifest pull OTA decisions (version compare, partition signature, URL rules).
// Pure string logic, so it runs on `native`; the network and flash side lives in
// examples/companion_radio/CompanionOta.cpp.
#include "helpers/OtaManifest.h"

static const char* BASE = "https://ota.dutchmeshcore.nl/meshcomod/v";

// --- version parsing --------------------------------------------------------

TEST(OtaVersion, ParsesBuildScriptString) {
  OtaVersion v;
  ASSERT_TRUE(ota_version_parse("v1.18.0.1-ab12cd3", &v));
  EXPECT_EQ(v.count, 4);
  EXPECT_EQ(v.part[0], 1);
  EXPECT_EQ(v.part[1], 18);
  EXPECT_EQ(v.part[2], 0);
  EXPECT_EQ(v.part[3], 1);
  EXPECT_STREQ(v.hash, "ab12cd3");
}

TEST(OtaVersion, PlainVersionHasNoHash) {
  OtaVersion v;
  ASSERT_TRUE(ota_version_parse("v1.18.0.1", &v));
  EXPECT_EQ(v.count, 4);
  EXPECT_STREQ(v.hash, "");
}

TEST(OtaVersion, HashIsTheLastDashToken) {
  OtaVersion v;
  ASSERT_TRUE(ota_version_parse("v1.17.0.4-DMC-EV-abc1234", &v));
  EXPECT_EQ(v.part[3], 4);
  EXPECT_STREQ(v.hash, "abc1234");
}

TEST(OtaVersion, RejectsNonVersionPrefix) {
  OtaVersion v;
  EXPECT_FALSE(ota_version_parse("meshcomod-r1.14.1.0-repeater-tcp-abc1234", &v));
  EXPECT_FALSE(ota_version_parse("", &v));
  EXPECT_FALSE(ota_version_parse(nullptr, &v));
  EXPECT_FALSE(ota_version_parse("v1..2", &v));
}

// --- compare ---------------------------------------------------------------

TEST(OtaCompare, NewerBuildNumber) {
  EXPECT_EQ(ota_compare("v1.18.0.1-aaaaaaa", "v1.18.0.2", "bbbbbbb"), OTA_VERDICT_NEWER);
}

TEST(OtaCompare, NewerBaseVersion) {
  EXPECT_EQ(ota_compare("v1.18.0.9-aaaaaaa", "v1.19.0.0", "bbbbbbb"), OTA_VERDICT_NEWER);
}

TEST(OtaCompare, OlderIsNotAnUpdate) {
  EXPECT_EQ(ota_compare("v1.18.0.3-aaaaaaa", "v1.18.0.2", "bbbbbbb"), OTA_VERDICT_OLDER);
  EXPECT_EQ(ota_compare("v1.19.0.0-aaaaaaa", "v1.18.9.9", "bbbbbbb"), OTA_VERDICT_OLDER);
}

TEST(OtaCompare, SameVersionSameHashIsCurrent) {
  EXPECT_EQ(ota_compare("v1.18.0.1-ab12cd3", "v1.18.0.1", "ab12cd3"), OTA_VERDICT_CURRENT);
}

TEST(OtaCompare, HashPrefixAbsorbsAbbreviationLength) {
  EXPECT_EQ(ota_compare("v1.18.0.1-ab12cd3", "v1.18.0.1", "ab12cd34"), OTA_VERDICT_CURRENT);
}

TEST(OtaCompare, SameVersionOtherHashIsRebuild) {
  EXPECT_EQ(ota_compare("v1.18.0.1-ab12cd3", "v1.18.0.1", "ffffff0"), OTA_VERDICT_REBUILD);
}

TEST(OtaCompare, SameVersionWithoutRunningHashIsCurrent) {
  // A local build without build.sh embeds the bare MyMesh.h version.
  EXPECT_EQ(ota_compare("v1.18.0.1", "v1.18.0.1", "ab12cd3"), OTA_VERDICT_CURRENT);
}

TEST(OtaCompare, MissingFourthPartCountsAsZero) {
  EXPECT_EQ(ota_compare("v1.18.0-aaaaaaa", "v1.18.0.0", "aaaaaaa"), OTA_VERDICT_CURRENT);
  EXPECT_EQ(ota_compare("v1.18.0-aaaaaaa", "v1.18.0.1", "bbbbbbb"), OTA_VERDICT_NEWER);
}

TEST(OtaCompare, UnparseableFallsBackToHash) {
  EXPECT_EQ(ota_compare("custom-ab12cd3", "", "ab12cd3"), OTA_VERDICT_CURRENT);
  EXPECT_EQ(ota_compare("custom-ab12cd3", "", "ffffff0"), OTA_VERDICT_REBUILD);
}

// --- partition signature ---------------------------------------------------

TEST(OtaPartSig, MatchesScriptFormatSortedByOffset) {
  // Same definition as scripts/partition_signature.py: "%x:%x:%x:%x" joined by ','.
  OtaPartEntry e[] = {
    {0x00, 0x10, 0x10000, 0x330000},
    {0x01, 0x02, 0x9000, 0x5000},
    {0x01, 0x00, 0xe000, 0x2000},
  };
  char out[128];
  ota_partsig_format(e, 3, out, sizeof(out));
  EXPECT_STREQ(out, "1:2:9000:5000,1:0:e000:2000,0:10:10000:330000");
}

TEST(OtaPartSig, TruncatesWithoutOverflow) {
  OtaPartEntry e[] = {{0x01, 0x02, 0x9000, 0x5000}, {0x00, 0x10, 0x10000, 0x330000}};
  char out[8];
  ota_partsig_format(e, 2, out, sizeof(out));
  EXPECT_LT(strlen(out), sizeof(out));
}

// --- URLs -------------------------------------------------------------------

TEST(OtaUrl, ManifestCheckUsesPlainHttp) {
  char url[160];
  ASSERT_TRUE(ota_manifest_url(BASE, "heltec_v4_companion_radio_usb_tcp", url, sizeof(url)));
  EXPECT_STREQ(url, "http://ota.dutchmeshcore.nl/meshcomod/v/heltec_v4_companion_radio_usb_tcp.json");
}

TEST(OtaUrl, ManifestUrlTooLongFails) {
  char url[16];
  EXPECT_FALSE(ota_manifest_url(BASE, "heltec_v4_companion_radio_usb_tcp", url, sizeof(url)));
}

TEST(OtaUrl, FirmwareUnderSiblingFwPathIsAllowed) {
  EXPECT_TRUE(ota_firmware_url_allowed(
      "https://ota.dutchmeshcore.nl/meshcomod/fw/companion-v1.18.0.2/heltec_v4_companion_radio_usb_tcp-v1.18.0.2-abc1234.bin",
      BASE));
  EXPECT_TRUE(ota_firmware_url_allowed(
      "http://ota.dutchmeshcore.nl/meshcomod/fw/companion-v1.18.0.2/x-v1.18.0.2-abc1234.bin", BASE));
}

TEST(OtaUrl, FirmwareElsewhereIsRefused) {
  EXPECT_FALSE(ota_firmware_url_allowed("https://evil.example/meshcomod/fw/a.bin", BASE));
  EXPECT_FALSE(ota_firmware_url_allowed("https://ota.dutchmeshcore.nl/mqtt/fw/a/b.bin", BASE));
  EXPECT_FALSE(ota_firmware_url_allowed("https://ota.dutchmeshcore.nl/meshcomod/fw/../../x.bin", BASE));
  EXPECT_FALSE(ota_firmware_url_allowed("https://ota.dutchmeshcore.nl/meshcomod/fwx/a.bin", BASE));
  EXPECT_FALSE(ota_firmware_url_allowed("ftp://ota.dutchmeshcore.nl/meshcomod/fw/a.bin", BASE));
  EXPECT_FALSE(ota_firmware_url_allowed(nullptr, BASE));
}

TEST(OtaUrl, BaseWithoutTrailingVAllowsNothing) {
  EXPECT_FALSE(ota_firmware_url_allowed("https://ota.dutchmeshcore.nl/meshcomod/fw/a.bin",
                                        "https://ota.dutchmeshcore.nl/meshcomod"));
}

// --- DMC version strings ----------------------------------------------------

TEST(OtaCompare, DmcStableStringMatchesItsManifest) {
  EXPECT_EQ(ota_compare("v1.18.0-dutchmeshcore.nl-ab12cd3", "v1.18.0", "ab12cd3"), OTA_VERDICT_CURRENT);
}

TEST(OtaCompare, DmcDevBuildOfAnotherCommitIsRebuild) {
  EXPECT_EQ(ota_compare("v1.18.0-dev-dutchmeshcore.nl-ab12cd3", "v1.18.0", "ffffff0"), OTA_VERDICT_REBUILD);
}

TEST(OtaCompare, DmcNewerReleaseIsNewer) {
  EXPECT_EQ(ota_compare("v1.18.0-dutchmeshcore.nl-ab12cd3", "v1.18.1", "ffffff0"), OTA_VERDICT_NEWER);
}

// --- manifest fields ----------------------------------------------------------

static const char* MANIFEST =
    "{\"file\": \"https:\/\/ota.dutchmeshcore.nl\/companion\/fw\/dmc-companion-v1.18.0\/a-v1.18.0-ab12cd3.bin\","
    " \"version\":\"v1.18.0\",\n  \"hash\" : \"ab12cd3\", \"partSig\": \"1:2:9000:5000,0:10:10000:330000\"}";

TEST(OtaJson, ReadsStringFields) {
  char out[160];
  ASSERT_TRUE(ota_json_string(MANIFEST, "file", out, sizeof(out)));
  EXPECT_STREQ(out, "https://ota.dutchmeshcore.nl/companion/fw/dmc-companion-v1.18.0/a-v1.18.0-ab12cd3.bin");
  ASSERT_TRUE(ota_json_string(MANIFEST, "version", out, sizeof(out)));
  EXPECT_STREQ(out, "v1.18.0");
  ASSERT_TRUE(ota_json_string(MANIFEST, "hash", out, sizeof(out)));
  EXPECT_STREQ(out, "ab12cd3");
  ASSERT_TRUE(ota_json_string(MANIFEST, "partSig", out, sizeof(out)));
  EXPECT_STREQ(out, "1:2:9000:5000,0:10:10000:330000");
}

TEST(OtaJson, MissingKeyIsEmpty) {
  char out[16] = "x";
  EXPECT_FALSE(ota_json_string("{\"file\":\"a\"}", "partSig", out, sizeof(out)));
  EXPECT_STREQ(out, "");
}

TEST(OtaJson, KeyTextInsideAValueDoesNotMatch) {
  char out[32];
  ASSERT_TRUE(ota_json_string("{\"note\":\"\\\"hash\\\": \\\"bad\\\"\",\"hash\":\"good\"}", "hash", out, sizeof(out)));
  EXPECT_STREQ(out, "good");
}

TEST(OtaJson, NonStringValueIsRefused) {
  char out[16];
  EXPECT_FALSE(ota_json_string("{\"hash\":123}", "hash", out, sizeof(out)));
}

TEST(OtaJson, TooLongValueIsRefused) {
  char out[4];
  EXPECT_FALSE(ota_json_string("{\"hash\":\"abcdef\"}", "hash", out, sizeof(out)));
}

TEST(OtaJson, UnterminatedStringIsRefused) {
  char out[16];
  EXPECT_FALSE(ota_json_string("{\"hash\":\"abc", "hash", out, sizeof(out)));
}

// --- image URL scheme ----------------------------------------------------------

TEST(OtaUrl, ImageMustBeHttps) {
  EXPECT_TRUE(ota_url_is_https("https://ota.dutchmeshcore.nl/companion/fw/t/a.bin"));
  EXPECT_FALSE(ota_url_is_https("http://ota.dutchmeshcore.nl/companion/fw/t/a.bin"));
  EXPECT_FALSE(ota_url_is_https(nullptr));
}

// --- channel ------------------------------------------------------------------

static const char* NATIVE = "https://ota.dutchmeshcore.nl/companion/dev/v";
static const char* STABLE = "https://ota.dutchmeshcore.nl/companion/v";
static const char* DEV = "https://ota.dutchmeshcore.nl/companion/dev/v";

TEST(OtaChannel, ParsesNames) {
  uint8_t ch = 99;
  ASSERT_TRUE(ota_channel_parse("stable", &ch));
  EXPECT_EQ(ch, OTA_CHANNEL_STABLE);
  ASSERT_TRUE(ota_channel_parse("dev", &ch));
  EXPECT_EQ(ch, OTA_CHANNEL_DEV);
  ASSERT_TRUE(ota_channel_parse("default", &ch));
  EXPECT_EQ(ch, OTA_CHANNEL_NATIVE);
  EXPECT_FALSE(ota_channel_parse("beta", &ch));
  EXPECT_FALSE(ota_channel_parse(nullptr, &ch));
}

TEST(OtaChannel, ResolvesBase) {
  EXPECT_STREQ(ota_channel_base(OTA_CHANNEL_NATIVE, NATIVE, STABLE, DEV), NATIVE);
  EXPECT_STREQ(ota_channel_base(OTA_CHANNEL_STABLE, NATIVE, STABLE, DEV), STABLE);
  EXPECT_STREQ(ota_channel_base(OTA_CHANNEL_DEV, NATIVE, STABLE, DEV), DEV);
  EXPECT_STREQ(ota_channel_base(7, NATIVE, STABLE, DEV), NATIVE);   // unknown: the build's own
}

TEST(OtaChannel, NamesTheBase) {
  EXPECT_STREQ(ota_channel_name(STABLE, STABLE, DEV), "stable");
  EXPECT_STREQ(ota_channel_name(DEV, STABLE, DEV), "dev");
  EXPECT_STREQ(ota_channel_name("https://x/v", STABLE, DEV), "custom");
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
