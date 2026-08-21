#include <gtest/gtest.h>

// Header-only drop statistics for the repeater packet filter.
#include "../../examples/simple_repeater/FilterStats.h"

// ---- Buf: bounded, always-terminated appender -------------------------------

TEST(FilterStatsBuf, AppendsPlainText) {
  char out[16];
  FilterStat::Buf b(out, sizeof(out));

  EXPECT_TRUE(b.add("ab%d", 12));

  EXPECT_STREQ("ab12", out);
  EXPECT_EQ(4u, b.len());
  EXPECT_FALSE(b.truncated());
}

TEST(FilterStatsBuf, EmptyUntilFirstAppend) {
  char out[8];
  FilterStat::Buf b(out, sizeof(out));

  EXPECT_STREQ("", out);
  EXPECT_EQ(0u, b.len());
}

TEST(FilterStatsBuf, FillsExactlyToCapacity) {
  char out[8];
  FilterStat::Buf b(out, sizeof(out));

  EXPECT_TRUE(b.add("0123456"));

  EXPECT_STREQ("0123456", out);
  EXPECT_EQ(7u, b.len());
  EXPECT_FALSE(b.truncated());
}

TEST(FilterStatsBuf, RejectsOversizedAppendWholesale) {
  char out[8];
  FilterStat::Buf b(out, sizeof(out));

  EXPECT_FALSE(b.add("0123456789"));

  EXPECT_STREQ("", out) << "an append that does not fit must not leave a partial fragment";
  EXPECT_EQ(0u, b.len());
  EXPECT_TRUE(b.truncated());
}

TEST(FilterStatsBuf, RejectsAppendThatWouldNotFitWithoutPartialWrite) {
  char out[8];
  FilterStat::Buf b(out, sizeof(out));
  ASSERT_TRUE(b.add("abcde"));

  EXPECT_FALSE(b.add("XYZ"));

  EXPECT_STREQ("abcde", out) << "a rejected append must not leave a partial fragment";
  EXPECT_EQ(5u, b.len());
  EXPECT_TRUE(b.truncated());
}

TEST(FilterStatsBuf, IgnoresFurtherAppendsOnceTruncated) {
  char out[8];
  FilterStat::Buf b(out, sizeof(out));
  ASSERT_TRUE(b.add("abcde"));
  ASSERT_FALSE(b.add("XYZ"));

  EXPECT_FALSE(b.add("!")) << "a short append must not sneak in after truncation";

  EXPECT_STREQ("abcde", out);
}

TEST(FilterStatsBuf, HandlesZeroCapacityWithoutWriting) {
  char canary[4] = {'\x7f', '\x7f', '\x7f', '\x7f'};
  FilterStat::Buf b(canary, 0);

  EXPECT_FALSE(b.add("x"));

  EXPECT_EQ('\x7f', canary[0]);
  EXPECT_EQ(0u, b.len());
}

// ---- Counters ---------------------------------------------------------------

TEST(FilterStatsCounters, StartsAtZero) {
  Counters c;

  EXPECT_EQ(0u, c.hash);
  EXPECT_EQ(0u, c.channel);
  EXPECT_EQ(0u, c.malformed);
  EXPECT_EQ(0u, c.hops[PAYLOAD_TYPE_ADVERT]);
  EXPECT_EQ(0u, c.rate[PAYLOAD_TYPE_ADVERT]);
  EXPECT_EQ(0u, c.src[0xa3]);
}

TEST(FilterStatsCounters, RecordHopsCountsOnlyItsOwnType) {
  Counters c;

  FilterStat::recordHops(c, PAYLOAD_TYPE_ADVERT);

  EXPECT_EQ(1u, c.hops[PAYLOAD_TYPE_ADVERT]);
  EXPECT_EQ(0u, c.hops[PAYLOAD_TYPE_GRP_TXT]);
  EXPECT_EQ(0u, c.rate[PAYLOAD_TYPE_ADVERT]);
}

TEST(FilterStatsCounters, RecordRateCountsOnlyItsOwnType) {
  Counters c;

  FilterStat::recordRate(c, PAYLOAD_TYPE_GRP_TXT);

  EXPECT_EQ(1u, c.rate[PAYLOAD_TYPE_GRP_TXT]);
  EXPECT_EQ(0u, c.hops[PAYLOAD_TYPE_GRP_TXT]);
}

TEST(FilterStatsCounters, RecordHashSplitsBySizeAndType) {
  Counters c;

  FilterStat::recordHash(c, 1, PAYLOAD_TYPE_ADVERT);
  FilterStat::recordHash(c, 1, PAYLOAD_TYPE_TXT_MSG);
  FilterStat::recordHash(c, 3, PAYLOAD_TYPE_ADVERT);

  EXPECT_EQ(3u, c.hash);
  EXPECT_EQ(2u, c.hash_size[0]) << "hash size 1 lives at index 0";
  EXPECT_EQ(1u, c.hash_size[2]);
  EXPECT_EQ(2u, c.hash_type[PAYLOAD_TYPE_ADVERT]);
  EXPECT_EQ(1u, c.hash_type[PAYLOAD_TYPE_TXT_MSG]);
}

TEST(FilterStatsCounters, RecordHashKeepsTotalWhenBreakdownIsOutOfRange) {
  Counters c;

  FilterStat::recordHash(c, 0, 0xFF);

  EXPECT_EQ(1u, c.hash) << "a drop must never be lost from the total";
  EXPECT_EQ(0u, c.hash_size[0]);
}

TEST(FilterStatsCounters, RecordChannelSplitsBySlot) {
  Counters c;

  FilterStat::recordChannel(c, 5);
  FilterStat::recordChannel(c, 5);

  EXPECT_EQ(2u, c.channel);
  EXPECT_EQ(2u, c.channel_slot[5]);
  EXPECT_EQ(0u, c.channel_slot[4]);
}

TEST(FilterStatsCounters, RecordChannelKeepsTotalWhenSlotIsOutOfRange) {
  Counters c;

  FilterStat::recordChannel(c, FILTER_CHANNEL_COUNT);

  EXPECT_EQ(1u, c.channel);
}

TEST(FilterStatsCounters, RecordMalformedSplitsByReason) {
  Counters c;

  FilterStat::recordMalformed(c, MALFORMED_UTF8);

  EXPECT_EQ(1u, c.malformed);
  EXPECT_EQ(1u, c.malformed_reason[MALFORMED_UTF8]);
  EXPECT_EQ(0u, c.malformed_reason[MALFORMED_TIMESTAMP]);
}

TEST(FilterStatsCounters, RecordSrcCountsPerHashByte) {
  Counters c;

  FilterStat::recordSrc(c, 0xa3);
  FilterStat::recordSrc(c, 0xa3);
  FilterStat::recordSrc(c, 0x00);

  EXPECT_EQ(2u, c.src[0xa3]);
  EXPECT_EQ(1u, c.src[0x00]);
  EXPECT_EQ(0u, c.src[0xa4]);
}

TEST(FilterStatsCounters, SaturatesInsteadOfWrappingTo32BitZero) {
  Counters c;
  c.hash = 0xFFFFFFFFu;
  c.hash_size[0] = 0xFFFFFFFFu;

  FilterStat::recordHash(c, 1, PAYLOAD_TYPE_ADVERT);

  EXPECT_EQ(0xFFFFFFFFu, c.hash);
  EXPECT_EQ(0xFFFFFFFFu, c.hash_size[0]);
}

TEST(FilterStatsCounters, SaturatesInsteadOfWrappingTo16BitZero) {
  Counters c;
  c.src[0xa3] = 0xFFFF;

  FilterStat::recordSrc(c, 0xa3);

  EXPECT_EQ(0xFFFFu, c.src[0xa3]);
}

TEST(FilterStatsCounters, ResetClearsEveryBucket) {
  Counters c;
  FilterStat::recordHops(c, PAYLOAD_TYPE_ADVERT);
  FilterStat::recordHash(c, 2, PAYLOAD_TYPE_TXT_MSG);
  FilterStat::recordChannel(c, 0);
  FilterStat::recordMalformed(c, MALFORMED_SHORT);
  FilterStat::recordSrc(c, 0xa3);

  c.reset();

  EXPECT_EQ(0u, c.hops[PAYLOAD_TYPE_ADVERT]);
  EXPECT_EQ(0u, c.hash);
  EXPECT_EQ(0u, c.hash_size[1]);
  EXPECT_EQ(0u, c.channel);
  EXPECT_EQ(0u, c.channel_slot[0]);
  EXPECT_EQ(0u, c.malformed);
  EXPECT_EQ(0u, c.malformed_reason[MALFORMED_SHORT]);
  EXPECT_EQ(0u, c.src[0xa3]);
}

// ---- Buf::markTruncated -----------------------------------------------------

TEST(FilterStatsBuf, MarkTruncatedDoesNothingWhenEverythingFitted) {
  char out[16];
  FilterStat::Buf b(out, sizeof(out));
  ASSERT_TRUE(b.add("abc"));

  b.markTruncated("..");

  EXPECT_STREQ("abc", out);
}

TEST(FilterStatsBuf, MarkTruncatedAppendsMarkerWhenRoomIsLeft) {
  char out[8];
  FilterStat::Buf b(out, sizeof(out));
  ASSERT_TRUE(b.add("ab"));
  ASSERT_FALSE(b.add("cdefghij"));

  b.markTruncated("..");

  EXPECT_STREQ("ab..", out) << "a clipped list must not read as a complete one";
}

TEST(FilterStatsBuf, MarkTruncatedOverwritesTailWhenBufferIsFull) {
  char out[8];
  FilterStat::Buf b(out, sizeof(out));
  ASSERT_TRUE(b.add("0123456"));
  ASSERT_FALSE(b.add("x"));

  b.markTruncated("..");

  EXPECT_STREQ("01234..", out);
  EXPECT_EQ(7u, strlen(out));
}

// ---- topSrc -----------------------------------------------------------------

TEST(FilterStatsTopSrc, ReportsNothingWhenNoSourceWasRecorded) {
  Counters c;
  FilterStat::TopEntry top[8];

  EXPECT_EQ(0, FilterStat::topSrc(c, top, 8));
}

TEST(FilterStatsTopSrc, OrdersByCountDescending) {
  Counters c;
  c.src[0x11] = 190;
  c.src[0xa3] = 412;
  c.src[0x5c] = 288;
  FilterStat::TopEntry top[8];

  ASSERT_EQ(3, FilterStat::topSrc(c, top, 8));

  EXPECT_EQ(0xa3, top[0].hash);
  EXPECT_EQ(412, top[0].count);
  EXPECT_EQ(0x5c, top[1].hash);
  EXPECT_EQ(0x11, top[2].hash);
}

TEST(FilterStatsTopSrc, SkipsZeroCounts) {
  Counters c;
  c.src[0x00] = 0;
  c.src[0xff] = 1;
  FilterStat::TopEntry top[8];

  ASSERT_EQ(1, FilterStat::topSrc(c, top, 8));

  EXPECT_EQ(0xff, top[0].hash);
}

TEST(FilterStatsTopSrc, BreaksTiesByLowestHashByte) {
  Counters c;
  c.src[0x9f] = 7;
  c.src[0x02] = 7;
  FilterStat::TopEntry top[8];

  ASSERT_EQ(2, FilterStat::topSrc(c, top, 8));

  EXPECT_EQ(0x02, top[0].hash) << "ordering must be deterministic across calls";
  EXPECT_EQ(0x9f, top[1].hash);
}

TEST(FilterStatsTopSrc, StopsAtRequestedMaximum) {
  Counters c;
  for (int i = 0; i < 20; i++) c.src[i] = (uint16_t)(100 + i);
  FilterStat::TopEntry top[8];

  EXPECT_EQ(3, FilterStat::topSrc(c, top, 3));

  EXPECT_EQ(19, top[0].hash) << "highest count first";
}

TEST(FilterStatsTopSrc, ReturnsNothingForNonPositiveMaximum) {
  Counters c;
  c.src[0x01] = 5;
  FilterStat::TopEntry top[8];

  EXPECT_EQ(0, FilterStat::topSrc(c, top, 0));
}

// ---- formatters -------------------------------------------------------------

// The repeater CLI hands every reply through a 160-byte buffer.
static const size_t REPLY_CAP = 160;

// Defaults as shipped in FilterPrefs, enough to exercise the limit columns.
static void defaultPrefs(PayloadPrefs* prefs) {
  for (uint8_t i = 0; i < PAYLOAD_TYPE_COUNT; i++) {
    prefs[i].hops_max = 8;
    prefs[i].rate_limit = 5;
    prefs[i].rate_secs = 60;
  }
  prefs[PAYLOAD_TYPE_GRP_TXT].hops_max = 32;
  prefs[PAYLOAD_TYPE_GRP_TXT].rate_limit = 20;
}

// Saturates every counter, the worst case any formatter has to survive.
static void saturate(Counters& c) {
  for (uint8_t i = 0; i < PAYLOAD_TYPE_COUNT; i++) {
    c.hops[i] = c.rate[i] = c.hash_type[i] = 0xFFFFFFFFu;
  }
  for (int i = 0; i < FILTER_HASH_SIZE_COUNT; i++) c.hash_size[i] = 0xFFFFFFFFu;
  for (int i = 0; i < MALFORMED_REASON_COUNT; i++) c.malformed_reason[i] = 0xFFFFFFFFu;
  for (int i = 0; i < FILTER_SRC_COUNT; i++) c.src[i] = 0xFFFF;
  c.channel = c.hash = c.malformed = 0xFFFFFFFFu;
}

TEST(FilterStatsSummary, KeepsTheEstablishedLayout) {
  Counters c;
  c.hops[PAYLOAD_TYPE_ADVERT] = 300;
  c.hops[PAYLOAD_TYPE_GRP_TXT] = 26;
  c.rate[PAYLOAD_TYPE_TXT_MSG] = 4;
  c.hash = 8097;
  char out[REPLY_CAP];

  FilterStat::formatSummary(out, sizeof(out), c, true);

  EXPECT_STREQ("> Filter on: Blocked [ Hops: 326 | Rate: 4 | Channel: 0 | Hash: 8097 | Malformed: 0 ]", out);
}

TEST(FilterStatsSummary, ReportsDisabledFilter) {
  Counters c;
  char out[REPLY_CAP];

  FilterStat::formatSummary(out, sizeof(out), c, false);

  EXPECT_EQ(0u, strncmp(out, "> Filter off:", 13)) << "got: " << out;
}

TEST(FilterStatsSummary, SaturatesTheTotalsInsteadOfWrapping) {
  Counters c;
  saturate(c);
  char out[REPLY_CAP];

  FilterStat::formatSummary(out, sizeof(out), c, true);

  EXPECT_NE(nullptr, strstr(out, "Hops: 4294967295")) << "got: " << out;
  EXPECT_LT(strlen(out), REPLY_CAP);
}

TEST(FilterStatsCount, KeepsListingEveryPayloadType) {
  Counters c;
  c.hops[PAYLOAD_TYPE_ADVERT] = 300;
  c.rate[PAYLOAD_TYPE_TXT_MSG] = 4;
  char out[REPLY_CAP];

  FilterStat::formatCount(out, sizeof(out), c);

  EXPECT_EQ(0u, strncmp(out, "[TYPE: HOPS,RATE]\n00: 0,0\n01: 0,0\n02: 0,4\n03: 0,0\n04: 300,0", 59)) << "got: " << out;
}

TEST(FilterStatsCount, MarksClippedOutputInsteadOfDroppingRowsSilently) {
  Counters c;
  saturate(c);
  char out[REPLY_CAP];

  FilterStat::formatCount(out, sizeof(out), c);

  EXPECT_LT(strlen(out), REPLY_CAP);
  EXPECT_NE(nullptr, strstr(out, "..")) << "got: " << out;
}

TEST(FilterStatsHops, ListsOnlyTypesThatDroppedSomethingWithTheirLimit) {
  Counters c;
  c.hops[PAYLOAD_TYPE_ADVERT] = 326;
  c.hops[PAYLOAD_TYPE_GRP_TXT] = 8097;
  PayloadPrefs prefs[PAYLOAD_TYPE_COUNT];
  defaultPrefs(prefs);
  char out[REPLY_CAP];

  FilterStat::formatStatsHops(out, sizeof(out), c, prefs);

  EXPECT_STREQ("[TYPE: DROPS(MAX)]\n04: 326(8)\n05: 8097(32)", out);
}

TEST(FilterStatsHops, SaysSoWhenNothingWasDropped) {
  Counters c;
  c.rate[PAYLOAD_TYPE_ADVERT] = 5;   // a rate drop is not a hop drop
  PayloadPrefs prefs[PAYLOAD_TYPE_COUNT];
  defaultPrefs(prefs);
  char out[REPLY_CAP];

  FilterStat::formatStatsHops(out, sizeof(out), c, prefs);

  EXPECT_STREQ("> Filter: no hop drops recorded", out);
}

TEST(FilterStatsHops, StaysWithinTheReplyBufferWhenEveryTypeDrops) {
  Counters c;
  saturate(c);
  PayloadPrefs prefs[PAYLOAD_TYPE_COUNT];
  defaultPrefs(prefs);
  char out[REPLY_CAP];

  FilterStat::formatStatsHops(out, sizeof(out), c, prefs);

  EXPECT_LT(strlen(out), REPLY_CAP);
  EXPECT_NE(nullptr, strstr(out, "..")) << "got: " << out;
}

TEST(FilterStatsRate, ListsOnlyTypesThatDroppedSomethingWithTheirWindow) {
  Counters c;
  c.rate[PAYLOAD_TYPE_TXT_MSG] = 12;
  PayloadPrefs prefs[PAYLOAD_TYPE_COUNT];
  defaultPrefs(prefs);
  char out[REPLY_CAP];

  FilterStat::formatStatsRate(out, sizeof(out), c, prefs);

  EXPECT_STREQ("[TYPE: DROPS(LIMIT/SECS)]\n02: 12(5/60)", out);
}

TEST(FilterStatsRate, SaysSoWhenNothingWasDropped) {
  Counters c;
  c.hops[PAYLOAD_TYPE_ADVERT] = 5;   // a hop drop is not a rate drop
  PayloadPrefs prefs[PAYLOAD_TYPE_COUNT];
  defaultPrefs(prefs);
  char out[REPLY_CAP];

  FilterStat::formatStatsRate(out, sizeof(out), c, prefs);

  EXPECT_STREQ("> Filter: no rate drops recorded", out);
}

TEST(FilterStatsRate, StaysWithinTheReplyBufferWhenEveryTypeDrops) {
  Counters c;
  saturate(c);
  PayloadPrefs prefs[PAYLOAD_TYPE_COUNT];
  defaultPrefs(prefs);
  char out[REPLY_CAP];

  FilterStat::formatStatsRate(out, sizeof(out), c, prefs);

  EXPECT_LT(strlen(out), REPLY_CAP);
  EXPECT_NE(nullptr, strstr(out, "..")) << "got: " << out;
}

TEST(FilterStatsHash, SplitsTheTotalOverThePathHashSizes) {
  Counters c;
  c.hash = 8097;
  c.hash_size[0] = 8097;
  char out[REPLY_CAP];

  FilterStat::formatStatsHash(out, sizeof(out), c);

  EXPECT_STREQ("> Blocked 8097 [1B:8097 2B:0 3B:0]", out);
}

TEST(FilterStatsHash, ListsTopBlockedTypes) {
  Counters c;
  c.hash = 3;
  c.hash_type[PAYLOAD_TYPE_ADVERT] = 2;
  c.hash_type[PAYLOAD_TYPE_TXT_MSG] = 1;
  char out[REPLY_CAP];

  FilterStat::formatStatsHash(out, sizeof(out), c);

  EXPECT_NE(nullptr, strstr(out, "\n  Top types: 04:2 02:1")) << "got: " << out;
}

TEST(FilterStatsHash, ShowsTheFourByteBucketOnlyWhenItWasUsed) {
  Counters c;
  c.hash = 1;
  c.hash_size[3] = 1;
  char out[REPLY_CAP];

  FilterStat::formatStatsHash(out, sizeof(out), c);

  EXPECT_NE(nullptr, strstr(out, "4B:1")) << "got: " << out;
}

TEST(FilterStatsHash, SaysSoWhenNothingWasDropped) {
  Counters c;
  char out[REPLY_CAP];

  FilterStat::formatStatsHash(out, sizeof(out), c);

  EXPECT_STREQ("> Filter: no path hash drops recorded", out);
}

TEST(FilterStatsHash, StaysWithinTheReplyBufferAtSaturatedCounters) {
  Counters c;
  saturate(c);
  char out[REPLY_CAP];

  FilterStat::formatStatsHash(out, sizeof(out), c);

  EXPECT_LT(strlen(out), REPLY_CAP);
}

TEST(FilterStatsMalformed, SplitsTheTotalByRejectionReason) {
  Counters c;
  c.malformed = 12;
  c.malformed_reason[MALFORMED_SHORT] = 1;
  c.malformed_reason[MALFORMED_TIMESTAMP] = 8;
  c.malformed_reason[MALFORMED_UTF8] = 3;
  char out[REPLY_CAP];

  FilterStat::formatStatsMalformed(out, sizeof(out), c);

  EXPECT_STREQ("> Blocked 12 [ short:1 time:8 empty:0 utf8:3 ]", out);
}

TEST(FilterStatsMalformed, SaysSoWhenNothingWasDropped) {
  Counters c;
  char out[REPLY_CAP];

  FilterStat::formatStatsMalformed(out, sizeof(out), c);

  EXPECT_STREQ("> Filter: no malformed drops recorded", out);
}

TEST(FilterStatsMalformed, StaysWithinTheReplyBufferAtSaturatedCounters) {
  Counters c;
  saturate(c);
  char out[REPLY_CAP];

  FilterStat::formatStatsMalformed(out, sizeof(out), c);

  EXPECT_LT(strlen(out), REPLY_CAP);
}

TEST(FilterStatsTop, ListsTheWorstSourceHashes) {
  Counters c;
  c.src[0xa3] = 412;
  c.src[0x5c] = 288;
  char out[REPLY_CAP];

  FilterStat::formatStatsTop(out, sizeof(out), c);

  EXPECT_STREQ("> Top drops: a3:412 5c:288", out);
}

TEST(FilterStatsTop, SaysSoWhenNothingWasRecorded) {
  Counters c;
  char out[REPLY_CAP];

  FilterStat::formatStatsTop(out, sizeof(out), c);

  EXPECT_STREQ("> Filter: no source drops recorded", out);
}

TEST(FilterStatsTop, StaysWithinTheReplyBufferWhenEverySourceDrops) {
  Counters c;
  saturate(c);
  char out[REPLY_CAP];

  FilterStat::formatStatsTop(out, sizeof(out), c);

  EXPECT_LT(strlen(out), REPLY_CAP);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
