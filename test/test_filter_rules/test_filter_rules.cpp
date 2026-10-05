#include <gtest/gtest.h>

#include <cstring>
#include <string>

// Header-only sender/text rule engine of the repeater packet filter: group
// text parsing, pattern matching and the ordered first-match evaluation with
// per-rule throttle budgets and probabilistic dosing. Pure functions, so they
// run on `native`.
#include "../../examples/simple_repeater/SenderRules.h"

// Builds a decrypted GRP_TXT plaintext: 4-byte timestamp, 1 flags byte
// (txt_type << 2), then "Sender: text".
static int plaintext(uint8_t* buf, const char* body, uint8_t txt_type = 0) {
  buf[0] = 0x78; buf[1] = 0x56; buf[2] = 0x34; buf[3] = 0x12;
  buf[4] = (uint8_t)(txt_type << 2);
  size_t n = strlen(body);
  memcpy(buf + 5, body, n);
  return (int)(5 + n);
}

static std::string str(const char* s, size_t n) { return std::string(s, n); }

// ---- parsing -------------------------------------------------------------------

TEST(FilterRulesParse, SplitsSenderAndTextAtTheFirstColonSpace) {
  uint8_t buf[80];
  int len = plaintext(buf, "SpamBot: BEACON 123: go");
  FilterRules::GroupText gt;

  ASSERT_TRUE(FilterRules::parseGroupText(buf, len, &gt));

  EXPECT_EQ("SpamBot", str(gt.sender, gt.sender_len));
  EXPECT_EQ("BEACON 123: go", str(gt.text, gt.text_len));
}

TEST(FilterRulesParse, RejectsNonPlainTextAndMalformedBodies) {
  uint8_t buf[80];
  FilterRules::GroupText gt;

  EXPECT_FALSE(FilterRules::parseGroupText(buf, plaintext(buf, "SpamBot: hi", 1), &gt)) << "not plain text";
  EXPECT_FALSE(FilterRules::parseGroupText(buf, plaintext(buf, "no separator"), &gt));
  EXPECT_FALSE(FilterRules::parseGroupText(buf, plaintext(buf, ": empty sender"), &gt));
  EXPECT_FALSE(FilterRules::parseGroupText(buf, 4, &gt)) << "too short";
  EXPECT_FALSE(FilterRules::parseGroupText(nullptr, 20, &gt));
}

TEST(FilterRulesParse, AcceptsAnEmptyTextAfterTheSeparator) {
  uint8_t buf[80];
  int len = plaintext(buf, "Alice: ");
  FilterRules::GroupText gt;

  ASSERT_TRUE(FilterRules::parseGroupText(buf, len, &gt));

  EXPECT_EQ("Alice", str(gt.sender, gt.sender_len));
  EXPECT_EQ(0u, gt.text_len);
}

// ---- matching ------------------------------------------------------------------

TEST(FilterRulesMatch, NameIsExactUnlessThePatternEndsWithAStar) {
  EXPECT_TRUE(FilterRules::matchName("SpamBot", "SpamBot", 7));
  EXPECT_FALSE(FilterRules::matchName("SpamBot", "SpamBot2", 8));
  EXPECT_FALSE(FilterRules::matchName("SpamBot", "spambot", 7)) << "case-sensitive";
  EXPECT_TRUE(FilterRules::matchName("Spam*", "SpamBot2", 8));
  EXPECT_FALSE(FilterRules::matchName("Spam*", "MySpam", 6));
  EXPECT_TRUE(FilterRules::matchName("*", "Anyone", 6)) << "lone star matches every sender";
  EXPECT_FALSE(FilterRules::matchName("", "Anyone", 6)) << "empty pattern never matches";
}

TEST(FilterRulesMatch, TextIsSubstringUnlessAnchoredWithACaret) {
  EXPECT_TRUE(FilterRules::matchText("BEACON", "hello BEACON 123", 16));
  EXPECT_FALSE(FilterRules::matchText("beacon", "hello BEACON 123", 16)) << "case-sensitive";
  EXPECT_TRUE(FilterRules::matchText("^BEACON", "BEACON 123", 10));
  EXPECT_FALSE(FilterRules::matchText("^BEACON", "hello BEACON", 12));
  EXPECT_FALSE(FilterRules::matchText("BEACON", "BEACO", 5)) << "pattern longer than text";
  EXPECT_FALSE(FilterRules::matchText("", "anything", 8));
}

// ---- evaluation ------------------------------------------------------------------

static SenderRule rule(const char* name, uint16_t secs = 0, uint8_t prob = 100) {
  SenderRule r;
  memset(&r, 0, sizeof(r));
  strncpy(r.name, name, sizeof(r.name) - 1);
  r.secs = secs;
  r.prob = prob;
  return r;
}

struct Eval {
  uint32_t last_pass[FILTER_RULE_COUNT] = {};
  uint32_t passes[FILTER_RULE_COUNT] = {};

  int run(const SenderRule* rules, int count, const char* sender, uint32_t now_ms, uint8_t rnd100 = 0) {
    size_t n = strlen(sender);
    return FilterRules::evaluate(rules, count, now_ms, rnd100, last_pass, passes,
                                 [&](const SenderRule& r) { return FilterRules::matchName(r.name, sender, n); });
  }
};

TEST(FilterRulesEvaluate, FirstMatchingBlockRuleDropsAndReportsItsSlot) {
  SenderRule rules[3] = { rule("Alice"), rule("Bot*"), rule("Bot1") };
  Eval e;

  EXPECT_EQ(1, e.run(rules, 3, "Bot1", 1000));
  EXPECT_EQ(-1, e.run(rules, 3, "Carol", 1000));
}

TEST(FilterRulesEvaluate, SkipsEmptySlots) {
  SenderRule rules[3] = { rule(""), rule(""), rule("Bob") };
  Eval e;

  EXPECT_EQ(2, e.run(rules, 3, "Bob", 1000));
}

TEST(FilterRulesEvaluate, ThrottleLetsOneMatchPerWindowThroughAndDropsTheExcess) {
  SenderRule rules[1] = { rule("Bob", 60) };
  Eval e;

  EXPECT_EQ(-1, e.run(rules, 1, "Bob", 1000)) << "first match after boot is a free pass";
  EXPECT_EQ(0, e.run(rules, 1, "Bob", 1000 + 30000)) << "inside the window: dropped";
  EXPECT_EQ(0, e.run(rules, 1, "Bob", 1000 + 59999));
  EXPECT_EQ(-1, e.run(rules, 1, "Bob", 1000 + 60000)) << "window elapsed: passes again";
  EXPECT_EQ(0, e.run(rules, 1, "Bob", 1000 + 60001)) << "the pass started a new window";

  EXPECT_EQ(2u, e.passes[0]);
}

TEST(FilterRulesEvaluate, OverRateFiringsDoNotExtendTheWindow) {
  SenderRule rules[1] = { rule("Bob", 10) };
  Eval e;
  ASSERT_EQ(-1, e.run(rules, 1, "Bob", 5000));
  for (uint32_t t = 5100; t < 15000; t += 100) ASSERT_EQ(0, e.run(rules, 1, "Bob", t));

  EXPECT_EQ(-1, e.run(rules, 1, "Bob", 15000)) << "exactly one pass per window however hard it is pushed";
}

TEST(FilterRulesEvaluate, AThrottledPassStepsAsideSoALaterRuleStillDecides) {
  SenderRule rules[2] = { rule("Bob", 60), rule("*") };
  Eval e;

  EXPECT_EQ(1, e.run(rules, 2, "Bob", 1000)) << "Bob's free pass falls through to the catch-all block";
  EXPECT_EQ(1u, e.passes[0]);
}

TEST(FilterRulesEvaluate, ProbabilityDecidesOnlyThatShareOfMatches) {
  SenderRule rules[1] = { rule("Bob", 0, 30) };
  Eval e;

  EXPECT_EQ(0, e.run(rules, 1, "Bob", 1000, 0)) << "roll 0 < 30: rule decides";
  EXPECT_EQ(0, e.run(rules, 1, "Bob", 1000, 29));
  EXPECT_EQ(-1, e.run(rules, 1, "Bob", 1000, 30)) << "roll 30 >= 30: rule steps aside";
  EXPECT_EQ(-1, e.run(rules, 1, "Bob", 1000, 99));
}

TEST(FilterRulesEvaluate, AFailedRollNeverTouchesTheThrottleBudget) {
  SenderRule rules[1] = { rule("Bob", 60, 50) };
  Eval e;

  EXPECT_EQ(-1, e.run(rules, 1, "Bob", 1000, 99)) << "failed roll: stepped aside";
  EXPECT_EQ(0u, e.passes[0]);
  EXPECT_EQ(-1, e.run(rules, 1, "Bob", 1001, 0)) << "passed roll: this is the free pass";
  EXPECT_EQ(1u, e.passes[0]);
  EXPECT_EQ(0, e.run(rules, 1, "Bob", 1002, 0));
}

TEST(FilterRulesEvaluate, ThrottleSurvivesMillisWrapAround) {
  SenderRule rules[1] = { rule("Bob", 60) };
  Eval e;
  ASSERT_EQ(-1, e.run(rules, 1, "Bob", 0xFFFFFFFFu - 1000));

  EXPECT_EQ(0, e.run(rules, 1, "Bob", 500)) << "1500 ms later, across the wrap, still inside the window";
  EXPECT_EQ(-1, e.run(rules, 1, "Bob", 60000));
}

// ---- rule parsing from CLI arguments ---------------------------------------------

TEST(FilterRulesArgs, ParsesOptionalSecondsAndProbability) {
  uint16_t secs = 99;
  uint8_t prob = 0;

  EXPECT_TRUE(FilterRules::parseArgs(nullptr, nullptr, &secs, &prob));
  EXPECT_EQ(0, secs);
  EXPECT_EQ(100, prob);

  EXPECT_TRUE(FilterRules::parseArgs("60", nullptr, &secs, &prob));
  EXPECT_EQ(60, secs);
  EXPECT_EQ(100, prob);

  EXPECT_TRUE(FilterRules::parseArgs("0", "35", &secs, &prob));
  EXPECT_EQ(0, secs);
  EXPECT_EQ(35, prob);
}

TEST(FilterRulesArgs, RejectsOutOfRangeValues) {
  uint16_t secs;
  uint8_t prob;

  EXPECT_FALSE(FilterRules::parseArgs("65536", nullptr, &secs, &prob));
  EXPECT_FALSE(FilterRules::parseArgs("-1", nullptr, &secs, &prob));
  EXPECT_FALSE(FilterRules::parseArgs("abc", nullptr, &secs, &prob));
  EXPECT_FALSE(FilterRules::parseArgs("60", "0", &secs, &prob)) << "0% is a disabled rule; use remove";
  EXPECT_FALSE(FilterRules::parseArgs("60", "101", &secs, &prob));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
