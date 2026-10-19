#include <gtest/gtest.h>

#include <cstring>

// Reading the binary /new_prefs a meshcomod companion leaves behind, for the
// fields upstream's /new_prefs migration does not cover (or misreads).
#include "../../examples/companion_radio/MeshcomodPrefs.h"

// Upstream /new_prefs layout up to and including multi_acks is 78 bytes; the
// tests only care about what comes after it, so the head is left zero.
static size_t upstreamFile(uint8_t* buf, bool with_cad, uint8_t cad = 1) {
  memset(buf, 0, 160);
  buf[78] = 1;                     // path_hash_mode
  buf[85] = 1;                     // gps_enabled
  uint32_t gps_interval = 300;
  memcpy(&buf[86], &gps_interval, 4);
  buf[90] = 0x0E;                  // autoadd_config
  buf[91] = 3;                     // autoadd_max_hops
  buf[92] = 1;                     // rx_boosted_gain
  strcpy((char*)&buf[93], "nl");   // default_scope_name[31]
  for (int i = 0; i < 16; i++) buf[124 + i] = (uint8_t)(0xA0 + i);   // default_scope_key[16]
  if (!with_cad) return 140;
  buf[140] = cad;
  return 141;
}

// meshcomod before June 2026 (v1.16 and older) never wrote gps_interval, so
// everything after gps_enabled sits 4 bytes early and the file is 136 bytes.
static size_t shiftedFile(uint8_t* buf) {
  memset(buf, 0, 160);
  buf[85] = 1;
  buf[86] = 0x0E;
  buf[87] = 3;
  buf[88] = 1;
  strcpy((char*)&buf[89], "nl");
  for (int i = 0; i < 16; i++) buf[120 + i] = (uint8_t)(0xA0 + i);
  return 136;
}

TEST(MeshcomodPrefs, CurrentFileCarriesTheCadByte) {
  uint8_t buf[160];
  MeshcomodPrefs::Fix fix;

  ASSERT_TRUE(MeshcomodPrefs::parse(buf, upstreamFile(buf, true, 0), &fix));
  EXPECT_TRUE(fix.has_cad);
  EXPECT_EQ(0, fix.cad);
  EXPECT_FALSE(fix.shifted) << "the rest is upstream's layout, which the stock migration reads";

  ASSERT_TRUE(MeshcomodPrefs::parse(buf, upstreamFile(buf, true, 7), &fix));
  EXPECT_EQ(1, fix.cad) << "any non-zero byte is on";
}

TEST(MeshcomodPrefs, UpstreamSizedFileNeedsNoFix) {
  uint8_t buf[160];
  MeshcomodPrefs::Fix fix;

  EXPECT_FALSE(MeshcomodPrefs::parse(buf, upstreamFile(buf, false), &fix));
  EXPECT_FALSE(fix.has_cad);
  EXPECT_FALSE(fix.shifted);
}

TEST(MeshcomodPrefs, ShiftedFileIsReadAtItsOwnOffsets) {
  uint8_t buf[160];
  MeshcomodPrefs::Fix fix;

  ASSERT_TRUE(MeshcomodPrefs::parse(buf, shiftedFile(buf), &fix));
  EXPECT_TRUE(fix.shifted);
  EXPECT_FALSE(fix.has_cad);
  EXPECT_EQ(0x0E, fix.autoadd_config);
  EXPECT_EQ(3, fix.autoadd_max_hops);
  EXPECT_EQ(1, fix.rx_boosted_gain);
  EXPECT_STREQ("nl", fix.scope_name);
  EXPECT_EQ(0xA0, fix.scope_key[0]);
  EXPECT_EQ(0xAF, fix.scope_key[15]);
}

TEST(MeshcomodPrefs, ScopeNameIsAlwaysTerminated) {
  uint8_t buf[160];
  MeshcomodPrefs::Fix fix;
  size_t len = shiftedFile(buf);
  memset(&buf[89], 'x', 31);   // no NUL inside the 31-byte field

  ASSERT_TRUE(MeshcomodPrefs::parse(buf, len, &fix));
  EXPECT_EQ(30u, strlen(fix.scope_name));
}

TEST(MeshcomodPrefs, RejectsOtherSizesAndNull) {
  uint8_t buf[160] = {};
  MeshcomodPrefs::Fix fix;

  EXPECT_FALSE(MeshcomodPrefs::parse(buf, 91, &fix));
  EXPECT_FALSE(MeshcomodPrefs::parse(buf, 0, &fix));
  EXPECT_FALSE(MeshcomodPrefs::parse(nullptr, 141, &fix));
}

TEST(MeshcomodPrefs, WifiIsImportedOnlyIntoAnEmptySsid) {
  EXPECT_TRUE(MeshcomodPrefs::shouldImportWifi("", "HomeNet"));
  EXPECT_FALSE(MeshcomodPrefs::shouldImportWifi("Already", "HomeNet"));
  EXPECT_FALSE(MeshcomodPrefs::shouldImportWifi("", ""));
  EXPECT_FALSE(MeshcomodPrefs::shouldImportWifi("", nullptr));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
