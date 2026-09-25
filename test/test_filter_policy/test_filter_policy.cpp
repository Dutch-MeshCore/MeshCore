#include <gtest/gtest.h>

// Header-only forwarding policies of the repeater packet filter that need no
// radio, filesystem or clock: the per-origin advert limiter and the path-prefix
// block list. Both are pure functions of their inputs so they run on `native`.
#include "../../examples/simple_repeater/AdvertLimiter.h"
#include "../../examples/simple_repeater/PathBlock.h"

static const uint32_t HOUR_MS = 3600UL * 1000UL;

static void origin(uint8_t* key, uint32_t n) {
  key[0] = (uint8_t)(n >> 24);
  key[1] = (uint8_t)(n >> 16);
  key[2] = (uint8_t)(n >> 8);
  key[3] = (uint8_t)n;
}

// ---- AdvertLimiter -----------------------------------------------------------

TEST(AdvertLimiter, IsOffByDefaultAndRecordsNothing) {
  AdvertLimiter lim;
  uint8_t key[4];
  origin(key, 1);

  EXPECT_EQ(0u, lim.getWindowHours());
  EXPECT_TRUE(lim.allow(key, 1000));
  EXPECT_TRUE(lim.allow(key, 1001));
  EXPECT_EQ(0, lim.getCount());
}

TEST(AdvertLimiter, FirstAdvertFromAnOriginPassesAndIsRemembered) {
  AdvertLimiter lim;
  lim.setWindowHours(1);
  uint8_t key[4];
  origin(key, 1);

  EXPECT_TRUE(lim.allow(key, 1000));
  EXPECT_EQ(1, lim.getCount());
}

TEST(AdvertLimiter, RepeatWithinWindowIsDropped) {
  AdvertLimiter lim;
  lim.setWindowHours(1);
  uint8_t key[4];
  origin(key, 1);
  ASSERT_TRUE(lim.allow(key, 1000));

  EXPECT_FALSE(lim.allow(key, 1000 + HOUR_MS - 1));
  EXPECT_EQ(1, lim.getCount()) << "a dropped repeat must not take a new cache slot";
}

TEST(AdvertLimiter, RepeatAfterWindowPassesAndRestartsTheWindow) {
  AdvertLimiter lim;
  lim.setWindowHours(1);
  uint8_t key[4];
  origin(key, 1);
  ASSERT_TRUE(lim.allow(key, 1000));

  EXPECT_TRUE(lim.allow(key, 1000 + HOUR_MS));
  EXPECT_FALSE(lim.allow(key, 1000 + HOUR_MS + 10)) << "the pass must start a fresh window";
  EXPECT_EQ(1, lim.getCount());
}

TEST(AdvertLimiter, OriginsAreIndependent) {
  AdvertLimiter lim;
  lim.setWindowHours(1);
  uint8_t a[4], b[4];
  origin(a, 1);
  origin(b, 2);
  ASSERT_TRUE(lim.allow(a, 1000));

  EXPECT_TRUE(lim.allow(b, 1001));
  EXPECT_FALSE(lim.allow(a, 1002));
  EXPECT_FALSE(lim.allow(b, 1003));
  EXPECT_EQ(2, lim.getCount());
}

TEST(AdvertLimiter, OverwritesTheOldestOriginWhenTheCacheIsFull) {
  AdvertLimiter lim;
  lim.setWindowHours(1);
  uint8_t key[4];

  for (uint32_t n = 0; n < ADVERT_CACHE_SIZE + 1; n++) {
    origin(key, n);
    ASSERT_TRUE(lim.allow(key, 1000 + n));
  }
  EXPECT_EQ(ADVERT_CACHE_SIZE, lim.getCount());

  origin(key, 0);
  EXPECT_TRUE(lim.allow(key, 2000)) << "origin 0 was evicted, so it passes again";
  origin(key, 2);
  EXPECT_FALSE(lim.allow(key, 2002)) << "origin 2 is still cached (re-adding 0 evicted only origin 1)";
}

TEST(AdvertLimiter, SurvivesMillisWrapAround) {
  AdvertLimiter lim;
  lim.setWindowHours(1);
  uint8_t key[4];
  origin(key, 7);
  ASSERT_TRUE(lim.allow(key, 0xFFFFFFFFu - 1000));

  EXPECT_FALSE(lim.allow(key, 500)) << "1500 ms later, across the wrap, is still inside the window";
  EXPECT_TRUE(lim.allow(key, HOUR_MS));
}

TEST(AdvertLimiter, ClearForgetsEveryOrigin) {
  AdvertLimiter lim;
  lim.setWindowHours(1);
  uint8_t key[4];
  origin(key, 1);
  ASSERT_TRUE(lim.allow(key, 1000));

  lim.clear();

  EXPECT_EQ(0, lim.getCount());
  EXPECT_TRUE(lim.allow(key, 1001));
}

TEST(AdvertLimiter, TurningTheWindowOffStopsDropping) {
  AdvertLimiter lim;
  lim.setWindowHours(1);
  uint8_t key[4];
  origin(key, 1);
  ASSERT_TRUE(lim.allow(key, 1000));

  lim.setWindowHours(0);

  EXPECT_TRUE(lim.allow(key, 1001));
}

// ---- PathBlock: parsing -------------------------------------------------------

TEST(PathBlockParse, AcceptsTwoToEightHexDigits) {
  PathPrefix p;

  ASSERT_TRUE(FilterPath::parse("A1", &p));
  EXPECT_EQ(1, p.len);
  EXPECT_EQ(0xA1, p.bytes[0]);

  ASSERT_TRUE(FilterPath::parse("a1b2c3d4", &p));
  EXPECT_EQ(4, p.len);
  EXPECT_EQ(0xA1, p.bytes[0]);
  EXPECT_EQ(0xB2, p.bytes[1]);
  EXPECT_EQ(0xC3, p.bytes[2]);
  EXPECT_EQ(0xD4, p.bytes[3]);
}

TEST(PathBlockParse, RejectsMalformedInput) {
  PathPrefix p;

  EXPECT_FALSE(FilterPath::parse("", &p));
  EXPECT_FALSE(FilterPath::parse("A", &p)) << "odd digit count";
  EXPECT_FALSE(FilterPath::parse("A1B", &p)) << "odd digit count";
  EXPECT_FALSE(FilterPath::parse("A1B2C3D4E5", &p)) << "more than 4 bytes";
  EXPECT_FALSE(FilterPath::parse("G1", &p)) << "not hex";
  EXPECT_FALSE(FilterPath::parse("0x", &p)) << "not hex";
  EXPECT_FALSE(FilterPath::parse(nullptr, &p));
}

TEST(PathBlockParse, FormatsBackToUppercaseHex) {
  PathPrefix p;
  ASSERT_TRUE(FilterPath::parse("a1b2", &p));

  char out[12];
  FilterPath::format(out, sizeof(out), p);

  EXPECT_STREQ("A1B2", out);
}

// ---- PathBlock: matching ------------------------------------------------------

static int slots(PathPrefix* list, int n, const char* const* hex) {
  for (int i = 0; i < n; i++) {
    if (hex[i] == nullptr) { list[i].len = 0; continue; }
    if (!FilterPath::parse(hex[i], &list[i])) return -1;
  }
  return n;
}

TEST(PathBlockMatch, FindsAOneBytePrefixAnywhereInTheFloodPath) {
  const uint8_t path[] = { 0x10, 0xA1, 0xB2 };
  PathPrefix list[2];
  const char* hex[] = { "C3", "A1" };
  ASSERT_EQ(2, slots(list, 2, hex));

  EXPECT_EQ(1, FilterPath::findMatch(path, 1, 3, list, 2));
}

TEST(PathBlockMatch, ReturnsMinusOneWhenNothingMatches) {
  const uint8_t path[] = { 0x10, 0xA1, 0xB2 };
  PathPrefix list[1];
  const char* hex[] = { "C3" };
  ASSERT_EQ(1, slots(list, 1, hex));

  EXPECT_EQ(-1, FilterPath::findMatch(path, 1, 3, list, 1));
  EXPECT_EQ(-1, FilterPath::findMatch(path, 1, 0, list, 1)) << "empty path";
}

TEST(PathBlockMatch, NeverMatchesAPrefixLongerThanThePathHashes) {
  const uint8_t path[] = { 0xA1, 0xB2 };   // two 1-byte hashes: A1, B2
  PathPrefix list[1];
  const char* hex[] = { "A1B2" };
  ASSERT_EQ(1, slots(list, 1, hex));

  EXPECT_EQ(-1, FilterPath::findMatch(path, 1, 2, list, 1))
      << "A1B2 is one 2-byte ID, not the two 1-byte IDs A1 then B2";
}

TEST(PathBlockMatch, ComparesAlignedToTheHashSize) {
  const uint8_t path[] = { 0xA1, 0xB2, 0xC3, 0xD4 };   // two 2-byte hashes: A1B2, C3D4
  PathPrefix list[3];
  const char* hex[] = { "B2C3", "C3", "A1B2" };
  ASSERT_EQ(3, slots(list, 3, hex));

  EXPECT_EQ(1, FilterPath::findMatch(path, 2, 2, list, 3))
      << "B2C3 straddles two IDs and must not match; C3 is the start of the second ID";
}

TEST(PathBlockMatch, SkipsEmptySlotsAndReportsTheFirstHit) {
  const uint8_t path[] = { 0xA1, 0xB2 };
  PathPrefix list[3];
  const char* hex[] = { nullptr, "B2", "A1" };
  ASSERT_EQ(3, slots(list, 3, hex));

  EXPECT_EQ(1, FilterPath::findMatch(path, 1, 2, list, 3));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
