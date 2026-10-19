#include <gtest/gtest.h>

#include <cstring>
#include <string>

// Receive-side filter of the companion firmware, plus the repeater-filter
// helpers it shares (malformed scan, rule and path-list editing, age parsing).
// Everything here is free of radio, filesystem and clock, so it runs on `native`.
#include "../../examples/companion_radio/CompanionFilter.h"

// A clock that has been set (after MessageAge::CLOCK_SET_AFTER): Sep 2026.
static const uint32_t NOW = 1790000000UL;

// Decrypted GRP_TXT plaintext: 4-byte timestamp, flags byte (txt_type << 2), body.
static int plaintext(uint8_t* buf, uint32_t ts, const char* body, uint8_t txt_type = 0) {
  memcpy(buf, &ts, 4);
  buf[4] = (uint8_t)(txt_type << 2);
  size_t n = strlen(body);
  memcpy(buf + 5, body, n);
  return (int)(5 + n);
}

static std::string cmd(CompanionFilter& f, const char* command, bool* changed = nullptr) {
  char reply[CFILTER_REPLY_SIZE];
  memset(reply, 'X', sizeof(reply));
  bool c = f.handleCommand(command, reply, NOW);
  if (changed) *changed = c;
  return std::string(reply);
}

static mesh::Packet floodPacket(uint8_t type, const uint8_t* path, uint8_t hash_size, uint8_t hash_count) {
  mesh::Packet p;
  memset(&p, 0, sizeof(p));
  p.header = (uint8_t)((type << PH_TYPE_SHIFT) | ROUTE_TYPE_FLOOD);
  p.setPathHashSizeAndCount(hash_size, hash_count);
  memcpy(p.path, path, (size_t)hash_size * hash_count);
  p.payload_len = 20;
  return p;
}

// ---- MessageContent (shared malformed scan) -------------------------------------

TEST(MessageContent, AcceptsPlainTextWithAPlausibleStamp) {
  uint8_t buf[64];
  uint8_t reason = 0xFF;
  EXPECT_TRUE(MessageContent::validate(buf, plaintext(buf, NOW, "Alice: hi"), NOW, &reason));
}

TEST(MessageContent, RejectsShortZeroStampEmptyAndBadUtf8) {
  uint8_t buf[64];
  uint8_t reason;

  EXPECT_FALSE(MessageContent::validate(buf, 5, NOW, &reason));
  EXPECT_EQ(MALFORMED_SHORT, reason);

  EXPECT_FALSE(MessageContent::validate(buf, plaintext(buf, 0, "Alice: hi"), NOW, &reason));
  EXPECT_EQ(MALFORMED_TIMESTAMP, reason);

  int len = plaintext(buf, NOW, "");
  buf[len++] = 0;   // a NUL right after the flags byte: no text at all
  EXPECT_FALSE(MessageContent::validate(buf, len, NOW, &reason));
  EXPECT_EQ(MALFORMED_EMPTY, reason);

  len = plaintext(buf, NOW, "Alice: ");
  buf[len++] = 0xC0;   // overlong lead byte
  buf[len++] = 0xAF;
  EXPECT_FALSE(MessageContent::validate(buf, len, NOW, &reason));
  EXPECT_EQ(MALFORMED_UTF8, reason);
}

TEST(MessageContent, JudgesStampsOnlyOnceTheClockIsSet) {
  uint8_t buf[64];
  uint8_t reason;
  uint32_t two_weeks = 14UL * 24UL * 3600UL;

  EXPECT_FALSE(MessageContent::validate(buf, plaintext(buf, NOW - two_weeks, "A: b"), NOW, &reason));
  EXPECT_EQ(MALFORMED_TIMESTAMP, reason);
  EXPECT_TRUE(MessageContent::validate(buf, plaintext(buf, NOW - two_weeks, "A: b"), 1000, &reason))
      << "an unset clock judges no stamp";
}

TEST(MessageContent, LeavesNonPlainTextTypesAlone) {
  uint8_t buf[64];
  uint8_t reason;
  int len = plaintext(buf, NOW, "", 1);
  buf[len++] = 0xFF;
  EXPECT_TRUE(MessageContent::validate(buf, len, NOW, &reason));
}

TEST(MessageContent, Utf8RejectsSurrogatesAndAcceptsEmoji) {
  const uint8_t surrogate[] = { 0xED, 0xA0, 0x80 };
  const uint8_t emoji[] = { 0xF0, 0x9F, 0x94, 0x8B };
  EXPECT_FALSE(MessageContent::isValidUTF8(surrogate, sizeof(surrogate)));
  EXPECT_TRUE(MessageContent::isValidUTF8(emoji, sizeof(emoji)));
}

// ---- shared list editing -------------------------------------------------------

TEST(FilterRulesEdit, AddsIntoTheFirstFreeSlotAndRejectsDuplicates) {
  SenderRule rules[FILTER_RULE_COUNT] = {};

  EXPECT_TRUE(FilterRules::addRule(rules, FILTER_RULE_COUNT, "Bot*", 0, 100));
  EXPECT_FALSE(FilterRules::addRule(rules, FILTER_RULE_COUNT, "Bot*", 0, 100));
  EXPECT_STREQ("Bot*", rules[0].name);

  EXPECT_EQ(0, FilterRules::removeRule(rules, FILTER_RULE_COUNT, "Bot*"));
  EXPECT_EQ(-1, FilterRules::removeRule(rules, FILTER_RULE_COUNT, "Bot*"));
  EXPECT_EQ('\0', rules[0].name[0]);
}

TEST(FilterRulesEdit, RejectsOverlongPatternsAndAFullList) {
  TextRule rules[FILTER_RULE_COUNT] = {};
  std::string longest(FILTER_TEXT_LEN - 1, 'x');
  std::string too_long(FILTER_TEXT_LEN, 'x');

  EXPECT_TRUE(FilterRules::addRule(rules, FILTER_RULE_COUNT, longest.c_str(), 30, 50));
  EXPECT_EQ(30, rules[0].secs);
  EXPECT_EQ(50, rules[0].prob);
  EXPECT_FALSE(FilterRules::addRule(rules, FILTER_RULE_COUNT, too_long.c_str(), 0, 100));
  EXPECT_FALSE(FilterRules::addRule(rules, FILTER_RULE_COUNT, "", 0, 100));

  for (int i = 1; i < FILTER_RULE_COUNT; i++) {
    std::string p = "p" + std::to_string(i);
    ASSERT_TRUE(FilterRules::addRule(rules, FILTER_RULE_COUNT, p.c_str(), 0, 100));
  }
  EXPECT_FALSE(FilterRules::addRule(rules, FILTER_RULE_COUNT, "one more", 0, 100));
  EXPECT_TRUE(FilterRules::anyRule(rules, FILTER_RULE_COUNT));
}

TEST(FilterPathEdit, AddRemoveReportTheCanonicalPrefix) {
  PathPrefix list[FILTER_PATH_COUNT] = {};
  char hex[2 * FILTER_PATH_MAX_LEN + 1];

  EXPECT_TRUE(FilterPath::add(list, FILTER_PATH_COUNT, "a1b2", hex));
  EXPECT_STREQ("A1B2", hex);
  EXPECT_FALSE(FilterPath::add(list, FILTER_PATH_COUNT, "A1B2", hex)) << "duplicate";
  EXPECT_STREQ("A1B2", hex) << "a duplicate still parsed, so it is not a syntax error";

  EXPECT_FALSE(FilterPath::add(list, FILTER_PATH_COUNT, "xyz", hex));
  EXPECT_STREQ("", hex) << "empty marks a bad prefix";

  EXPECT_TRUE(FilterPath::remove(list, FILTER_PATH_COUNT, "a1b2", hex));
  EXPECT_FALSE(FilterPath::remove(list, FILTER_PATH_COUNT, "a1b2", hex));
}

TEST(MessageAgeParse, AcceptsMinutesOrOff) {
  uint16_t mins = 99;
  EXPECT_TRUE(MessageAge::parseMins("off", &mins));
  EXPECT_EQ(0, mins);
  EXPECT_TRUE(MessageAge::parseMins("60", &mins));
  EXPECT_EQ(60, mins);
  EXPECT_TRUE(MessageAge::parseMins("10080", &mins));
  EXPECT_FALSE(MessageAge::parseMins("10081", &mins));
  EXPECT_FALSE(MessageAge::parseMins("-1", &mins));
  EXPECT_FALSE(MessageAge::parseMins("1h", &mins));
  EXPECT_FALSE(MessageAge::parseMins("", &mins));
}

// ---- CompanionFilter: verdicts -------------------------------------------------

TEST(CompanionFilter, IsOffByDefaultAndPassesEverything) {
  CompanionFilter f;
  uint8_t buf[64];
  int len = plaintext(buf, 0, "Bot: junk");
  f.prefs().malformed = true;
  ASSERT_TRUE(FilterRules::addRule(f.prefs().sender_rules, FILTER_RULE_COUNT, "Bot", 0, 100));

  EXPECT_FALSE(f.wantsGroupText());
  EXPECT_EQ(CFilterVerdict::PASS, f.checkGroupText(buf, len, NOW, 1000, 0));
}

TEST(CompanionFilter, DropsMalformedText) {
  CompanionFilter f;
  f.prefs().enabled = true;
  f.prefs().malformed = true;
  uint8_t buf[64];

  EXPECT_TRUE(f.wantsGroupText());
  EXPECT_EQ(CFilterVerdict::DROP, f.checkGroupText(buf, plaintext(buf, 0, "A: b"), NOW, 1000, 0));
  EXPECT_EQ(CFilterVerdict::PASS, f.checkGroupText(buf, plaintext(buf, NOW, "A: b"), NOW, 1000, 0));
  EXPECT_EQ(1u, f.counters().malformed);
  EXPECT_EQ(1u, f.counters().malformed_reason[MALFORMED_TIMESTAMP]);
}

TEST(CompanionFilter, DropsTextOlderThanTheAgeLimit) {
  CompanionFilter f;
  f.prefs().enabled = true;
  f.prefs().age_mins = 30;
  uint8_t buf[64];

  EXPECT_EQ(CFilterVerdict::DROP, f.checkGroupText(buf, plaintext(buf, NOW - 31 * 60, "A: b"), NOW, 1000, 0));
  EXPECT_EQ(CFilterVerdict::PASS, f.checkGroupText(buf, plaintext(buf, NOW - 29 * 60, "A: b"), NOW, 1000, 0));
  EXPECT_EQ(1u, f.counters().age);
}

TEST(CompanionFilter, HidesBySenderAndTextRules) {
  CompanionFilter f;
  f.prefs().enabled = true;
  ASSERT_TRUE(FilterRules::addRule(f.prefs().sender_rules, FILTER_RULE_COUNT, "Bot*", 0, 100));
  ASSERT_TRUE(FilterRules::addRule(f.prefs().text_rules, FILTER_RULE_COUNT, "^!ping", 0, 100));
  uint8_t buf[64];

  EXPECT_EQ(CFilterVerdict::HIDE, f.checkGroupText(buf, plaintext(buf, NOW, "BotNL: weather"), NOW, 1000, 0));
  EXPECT_EQ(CFilterVerdict::HIDE, f.checkGroupText(buf, plaintext(buf, NOW, "Alice: !ping"), NOW, 1000, 0));
  EXPECT_EQ(CFilterVerdict::PASS, f.checkGroupText(buf, plaintext(buf, NOW, "Alice: hi !ping"), NOW, 1000, 0));
  EXPECT_EQ(1u, f.counters().sender);
  EXPECT_EQ(1u, f.counters().text);
}

TEST(CompanionFilter, ThrottleRuleLetsOneMessagePerWindowThrough) {
  CompanionFilter f;
  f.prefs().enabled = true;
  ASSERT_TRUE(FilterRules::addRule(f.prefs().sender_rules, FILTER_RULE_COUNT, "Beacon", 60, 100));
  uint8_t buf[64];
  int len = plaintext(buf, NOW, "Beacon: tick");

  EXPECT_EQ(CFilterVerdict::PASS, f.checkGroupText(buf, len, NOW, 1000, 0));
  EXPECT_EQ(CFilterVerdict::HIDE, f.checkGroupText(buf, len, NOW, 30000, 0));
  EXPECT_EQ(CFilterVerdict::PASS, f.checkGroupText(buf, len, NOW, 61000, 0));
}

TEST(CompanionFilter, JunkChecksRunBeforePersonalRules) {
  CompanionFilter f;
  f.prefs().enabled = true;
  f.prefs().malformed = true;
  ASSERT_TRUE(FilterRules::addRule(f.prefs().sender_rules, FILTER_RULE_COUNT, "Bot", 0, 100));
  uint8_t buf[64];

  EXPECT_EQ(CFilterVerdict::DROP, f.checkGroupText(buf, plaintext(buf, 0, "Bot: x"), NOW, 1000, 0))
      << "junk is dropped (not forwarded), not merely hidden";
}

TEST(CompanionFilter, PathBlockDropsGroupAndAdvertFloodsOnly) {
  CompanionFilter f;
  f.prefs().enabled = true;
  char hex[2 * FILTER_PATH_MAX_LEN + 1];
  ASSERT_TRUE(FilterPath::add(f.prefs().path_block, FILTER_PATH_COUNT, "A1", hex));
  const uint8_t path[] = { 0x10, 0xA1 };

  mesh::Packet grp = floodPacket(PAYLOAD_TYPE_GRP_TXT, path, 1, 2);
  mesh::Packet adv = floodPacket(PAYLOAD_TYPE_ADVERT, path, 1, 2);
  mesh::Packet dat = floodPacket(PAYLOAD_TYPE_GRP_DATA, path, 1, 2);
  mesh::Packet dm = floodPacket(PAYLOAD_TYPE_TXT_MSG, path, 1, 2);

  EXPECT_EQ(CFilterVerdict::DROP, f.checkPath(&grp));
  EXPECT_EQ(CFilterVerdict::DROP, f.checkPath(&adv));
  EXPECT_EQ(CFilterVerdict::DROP, f.checkPath(&dat));
  EXPECT_EQ(CFilterVerdict::PASS, f.checkPath(&dm)) << "traffic addressed to us is never path-blocked";
  EXPECT_EQ(3u, f.counters().path);

  const uint8_t clean[] = { 0x10, 0x20 };
  mesh::Packet ok = floodPacket(PAYLOAD_TYPE_GRP_TXT, clean, 1, 2);
  EXPECT_EQ(CFilterVerdict::PASS, f.checkPath(&ok));

  f.prefs().enabled = false;
  EXPECT_EQ(CFilterVerdict::PASS, f.checkPath(&grp));
}

TEST(CompanionFilter, RemembersRecentlyJudgedPacketsSoRelayedCopiesAreNotRecounted) {
  CompanionFilter f;
  uint8_t h[MAX_HASH_SIZE] = { 1 };

  EXPECT_FALSE(f.alreadyJudged(h));
  EXPECT_TRUE(f.alreadyJudged(h));

  for (int i = 0; i < CFILTER_JUDGED_COUNT; i++) {
    uint8_t other[MAX_HASH_SIZE] = { 2, (uint8_t)i };
    EXPECT_FALSE(f.alreadyJudged(other));
  }
  EXPECT_FALSE(f.alreadyJudged(h)) << "the oldest entry is evicted";
}

// ---- CompanionFilter: CLI ------------------------------------------------------

TEST(CompanionFilterCli, RecognisesOnlyTheFilterCommand) {
  EXPECT_TRUE(CompanionFilter::isCommand("filter"));
  EXPECT_TRUE(CompanionFilter::isCommand("filter on"));
  EXPECT_FALSE(CompanionFilter::isCommand("filters"));
  EXPECT_FALSE(CompanionFilter::isCommand("get name"));
}

TEST(CompanionFilterCli, OnOffAndSummary) {
  CompanionFilter f;
  bool changed;

  EXPECT_EQ("> Filter: on", cmd(f, "filter on", &changed));
  EXPECT_TRUE(changed);
  EXPECT_TRUE(f.prefs().enabled);
  EXPECT_EQ("> Filter on: Hidden [ Malformed: 0 | Age: 0 | Path: 0 | Sender: 0 | Text: 0 ]", cmd(f, "filter", &changed));
  EXPECT_FALSE(changed);
  EXPECT_EQ("> Filter: off", cmd(f, "filter off"));
  EXPECT_FALSE(f.prefs().enabled);
}

TEST(CompanionFilterCli, MalformedAndAge) {
  CompanionFilter f;
  bool changed;

  EXPECT_EQ("> Filter: malformed scan on", cmd(f, "filter malformed on", &changed));
  EXPECT_TRUE(changed);
  EXPECT_EQ("> Filter: malformed text scan on", cmd(f, "filter malformed"));
  EXPECT_EQ("> Filter: syntax error 'filter malformed <on | off>'", cmd(f, "filter malformed maybe", &changed));
  EXPECT_FALSE(changed);

  EXPECT_EQ("> Filter: OK", cmd(f, "filter age 45"));
  EXPECT_EQ(45, f.prefs().age_mins);
  EXPECT_EQ("> Filter: error <minutes> range is 1-10080, or off", cmd(f, "filter age 99999", &changed));
  EXPECT_FALSE(changed);
  EXPECT_EQ("> Message age: max 45m, dropped 0", cmd(f, "filter age"));
  EXPECT_EQ("> Filter: OK", cmd(f, "filter age off"));
  EXPECT_EQ("> Filter: message age limit off", cmd(f, "filter age"));
}

TEST(CompanionFilterCli, PathList) {
  CompanionFilter f;

  EXPECT_EQ("None", cmd(f, "filter path list"));
  EXPECT_EQ("> Filter: path A1 added", cmd(f, "filter path add a1"));
  EXPECT_EQ("Failed", cmd(f, "filter path add A1"));
  EXPECT_EQ("> Filter: error path prefix is 2-8 hex digits", cmd(f, "filter path add q"));
  EXPECT_EQ("A1", cmd(f, "filter path list"));
  EXPECT_EQ("> Filter: path A1 removed", cmd(f, "filter path remove A1"));
}

TEST(CompanionFilterCli, SenderAndTextRules) {
  CompanionFilter f;

  EXPECT_EQ("> Filter: sender Bot* added", cmd(f, "filter sender add Bot*"));
  EXPECT_EQ("> Filter: text ^!ping added", cmd(f, "filter text add ^!ping 30 50"));
  EXPECT_EQ("Bot* block", cmd(f, "filter sender list"));
  EXPECT_EQ("^!ping throttle 30s 50%", cmd(f, "filter text list"));
  EXPECT_EQ("> Filter: error <secs> range is 0-65535, <prob> range is 1-100", cmd(f, "filter text add x 1 0"));
  EXPECT_EQ("> Filter: sender Bot* removed", cmd(f, "filter sender remove Bot*"));
  EXPECT_EQ("Failed", cmd(f, "filter sender remove Bot*"));
}

TEST(CompanionFilterCli, StatsTopics) {
  CompanionFilter f;
  f.prefs().enabled = true;
  f.prefs().malformed = true;
  uint8_t buf[64];
  f.checkGroupText(buf, plaintext(buf, 0, "A: b"), NOW, 1000, 0);

  EXPECT_EQ("> Blocked 1 [ short:0 time:1 empty:0 utf8:0 ]", cmd(f, "filter stats malformed"));
  EXPECT_EQ("> filter stats [ malformed | age | path | sender | text ]", cmd(f, "filter stats"));
  EXPECT_EQ("> Filter: command error", cmd(f, "filter bogus"));
}

TEST(CompanionFilterCli, ResetClearsEveryPreference) {
  CompanionFilter f;
  cmd(f, "filter on");
  cmd(f, "filter malformed on");
  cmd(f, "filter sender add Bot");

  EXPECT_EQ("> Filter: preferences reset", cmd(f, "filter reset"));
  EXPECT_FALSE(f.prefs().enabled);
  EXPECT_FALSE(f.prefs().malformed);
  EXPECT_FALSE(FilterRules::anyRule(f.prefs().sender_rules, FILTER_RULE_COUNT));
}

TEST(CompanionFilterCli, EveryReplyFitsTheBuffer) {
  CompanionFilter f;
  for (int i = 0; i < FILTER_RULE_COUNT; i++) {
    std::string c = "filter text add " + std::string(FILTER_TEXT_LEN - 3, 'a' + i) + " 65535 99";
    cmd(f, c.c_str());
  }
  std::string r = cmd(f, "filter text list");
  EXPECT_LT(r.size(), (size_t)CFILTER_REPLY_SIZE);
  EXPECT_EQ("..", r.substr(r.size() - 2)) << "a clipped list must say so";
}

// ---- CompanionFilter: persisted preferences -------------------------------------

class MemStream : public Stream {
  std::string _data;
  size_t _pos = 0;

  size_t printNum(const char* fmt, long long v) {
    char t[24];
    snprintf(t, sizeof(t), fmt, v);
    return Print::print(t);
  }

public:
  size_t write(uint8_t b) override { _data.push_back((char)b); return 1; }
  size_t print(unsigned char v, int) override { return printNum("%lld", v); }
  size_t print(int v, int) override { return printNum("%lld", v); }
  size_t print(unsigned int v, int) override { return printNum("%lld", v); }
  size_t print(long v, int) override { return printNum("%lld", v); }
  size_t print(unsigned long v, int) override { return printNum("%lld", (long long)v); }
  int available() override { return (int)(_data.size() - _pos); }
  int read() override { return _pos < _data.size() ? (uint8_t)_data[_pos++] : -1; }
  int peek() override { return _pos < _data.size() ? (uint8_t)_data[_pos] : -1; }
  const std::string& str() const { return _data; }
};

TEST(CompanionFilterPrefs, RoundTripsThroughJson) {
  CompanionFilter a;
  cmd(a, "filter on");
  cmd(a, "filter malformed on");
  cmd(a, "filter age 120");
  cmd(a, "filter path add A1B2");
  cmd(a, "filter path add 3C");
  cmd(a, "filter sender add Bot*");
  cmd(a, "filter sender add Alice 30 50");
  cmd(a, "filter text add \"quoted\\");

  MemStream s;
  ASSERT_TRUE(a.prefs().saveSerial(s));

  CompanionFilter b;
  ASSERT_TRUE(b.prefs().loadSerial(s)) << s.str();
  b.prefs().sanitize();

  EXPECT_TRUE(b.prefs().enabled);
  EXPECT_TRUE(b.prefs().malformed);
  EXPECT_EQ(120, b.prefs().age_mins);
  EXPECT_EQ(cmd(a, "filter path list"), cmd(b, "filter path list"));
  EXPECT_EQ(cmd(a, "filter sender list"), cmd(b, "filter sender list"));
  EXPECT_EQ(cmd(a, "filter text list"), cmd(b, "filter text list"));
  EXPECT_STREQ("\"quoted\\", b.prefs().text_rules[0].text);
}

TEST(CompanionFilterPrefs, EmptyListsSaveAndLoad) {
  CompanionFilter a;
  MemStream s;
  ASSERT_TRUE(a.prefs().saveSerial(s));

  CompanionFilter b;
  cmd(b, "filter sender add Stale");
  b.prefs().reset();
  ASSERT_TRUE(b.prefs().loadSerial(s)) << s.str();
  EXPECT_FALSE(b.prefs().enabled);
  EXPECT_FALSE(FilterRules::anyRule(b.prefs().sender_rules, FILTER_RULE_COUNT));
}

TEST(CompanionFilterPrefs, SanitizeClampsHandEditedValues) {
  CompanionFilter f;
  f.prefs().age_mins = 60000;
  f.prefs().path_block[0].len = 9;
  f.prefs().sender_rules[0].name[0] = 'x';
  f.prefs().sender_rules[0].prob = 0;

  f.prefs().sanitize();

  EXPECT_EQ(FILTER_AGE_MAX_MINS, f.prefs().age_mins);
  EXPECT_EQ(0, f.prefs().path_block[0].len);
  EXPECT_EQ(100, f.prefs().sender_rules[0].prob);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
