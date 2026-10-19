#include <gtest/gtest.h>
#include "helpers/RegionMap.h"
#include <string.h>

// Unit tests for duty-cycle region gating (issue #2747).
// These exercise the pure hierarchy/level logic; no crypto or filesystem is touched.
// Each test covers one gating scenario (one level, one transition, or one invariant)
// so a failure points directly at the behaviour that broke.

static bool gated(RegionMap& rm, const char* name) {
  RegionEntry* r = rm.findByName(name);   // findByName("*") returns the wildcard
  return r && (r->rt_flags & REGION_DENY_FLOOD);
}

// Fixture:  * > eu > nl > nl-ge > nl-ge-nij   (a single deep chain)
class NlChainGating : public ::testing::Test {
protected:
  TransportKeyStore ks;
  RegionMap rm{ks};

  void SetUp() override {
    RegionEntry* eu   = rm.putRegion("eu", 0);
    RegionEntry* nl   = rm.putRegion("nl", eu->id);
    RegionEntry* nlge = rm.putRegion("nl-ge", nl->id);
    rm.putRegion("nl-ge-nij", nlge->id);
  }
};

// Fixture:  * > cz > { cz-ulk, cz-stc, cz-lbk }   (three regions on the same level)
class CzFlatGating : public ::testing::Test {
protected:
  TransportKeyStore ks;
  RegionMap rm{ks};

  void SetUp() override {
    RegionEntry* cz = rm.putRegion("cz", 0);
    rm.putRegion("cz-ulk", cz->id);
    rm.putRegion("cz-stc", cz->id);
    rm.putRegion("cz-lbk", cz->id);
  }
};

// ---------- depth model ----------

TEST_F(NlChainGating, MaxDepthCountsChainDepth) {
  EXPECT_EQ(rm.getMaxDepth(), 4);
}

TEST_F(NlChainGating, MaxGateLevelEqualsMaxDepth) {
  EXPECT_EQ(rm.getMaxGateLevel(), 4);
}

// ---------- gating, level by level ----------

TEST_F(NlChainGating, LevelZeroGatesNothing) {
  rm.applyDutyGate(0);
  EXPECT_FALSE(gated(rm, "*"));
  EXPECT_FALSE(gated(rm, "eu"));
  EXPECT_FALSE(gated(rm, "nl"));
  EXPECT_FALSE(gated(rm, "nl-ge"));
  EXPECT_FALSE(gated(rm, "nl-ge-nij"));
}

TEST_F(NlChainGating, LevelOneGatesOnlyWildcard) {
  rm.applyDutyGate(1);
  EXPECT_TRUE(gated(rm, "*"));
  EXPECT_FALSE(gated(rm, "eu"));
  EXPECT_FALSE(gated(rm, "nl"));
  EXPECT_FALSE(gated(rm, "nl-ge"));
  EXPECT_FALSE(gated(rm, "nl-ge-nij"));
}

TEST_F(NlChainGating, LevelTwoAddsBroadestNamedRegion) {
  rm.applyDutyGate(2);
  EXPECT_TRUE(gated(rm, "*"));
  EXPECT_TRUE(gated(rm, "eu"));
  EXPECT_FALSE(gated(rm, "nl"));
  EXPECT_FALSE(gated(rm, "nl-ge"));
  EXPECT_FALSE(gated(rm, "nl-ge-nij"));
}

TEST_F(NlChainGating, LevelThreeGatesDownToSecondDeepest) {
  rm.applyDutyGate(3);
  EXPECT_TRUE(gated(rm, "*"));
  EXPECT_TRUE(gated(rm, "eu"));
  EXPECT_TRUE(gated(rm, "nl"));
  EXPECT_FALSE(gated(rm, "nl-ge"));
  EXPECT_FALSE(gated(rm, "nl-ge-nij"));
}

TEST_F(NlChainGating, MaxLevelProtectsOnlyInnermostCluster) {
  rm.applyDutyGate(4);
  EXPECT_TRUE(gated(rm, "*"));
  EXPECT_TRUE(gated(rm, "eu"));
  EXPECT_TRUE(gated(rm, "nl"));
  EXPECT_TRUE(gated(rm, "nl-ge"));
  EXPECT_FALSE(gated(rm, "nl-ge-nij"));
}

TEST_F(NlChainGating, LevelAboveMaxStillProtectsInnermostCluster) {
  rm.applyDutyGate(99);
  EXPECT_TRUE(gated(rm, "nl-ge"));
  EXPECT_FALSE(gated(rm, "nl-ge-nij"));
}

// ---------- recovery ----------

TEST_F(NlChainGating, RecoveryStepDownReenablesInsideOut) {
  rm.applyDutyGate(4);
  rm.applyDutyGate(2);   // step back down: nl and nl-ge come back first
  EXPECT_TRUE(gated(rm, "*"));
  EXPECT_TRUE(gated(rm, "eu"));
  EXPECT_FALSE(gated(rm, "nl"));
  EXPECT_FALSE(gated(rm, "nl-ge"));
  EXPECT_FALSE(gated(rm, "nl-ge-nij"));
}

TEST_F(NlChainGating, RecoveryToZeroClearsAllGates) {
  rm.applyDutyGate(4);
  rm.applyDutyGate(0);
  EXPECT_FALSE(gated(rm, "*"));
  EXPECT_FALSE(gated(rm, "eu"));
  EXPECT_FALSE(gated(rm, "nl"));
  EXPECT_FALSE(gated(rm, "nl-ge"));
  EXPECT_FALSE(gated(rm, "nl-ge-nij"));
}

// ---------- home region exemption ----------

TEST_F(NlChainGating, HomeRegionIsNeverGated) {
  rm.setHomeRegion(rm.findByName("nl-ge"));   // home NOT at the deepest level
  rm.applyDutyGate(4);
  EXPECT_TRUE(gated(rm, "*"));
  EXPECT_TRUE(gated(rm, "eu"));
  EXPECT_TRUE(gated(rm, "nl"));
  EXPECT_FALSE(gated(rm, "nl-ge"));       // exempt: it's the home region
  EXPECT_FALSE(gated(rm, "nl-ge-nij"));   // exempt: innermost layer
}

TEST_F(NlChainGating, BroadHomeRegionIsAlsoExempt) {
  rm.setHomeRegion(rm.findByName("eu"));   // operator chose a broad home region
  rm.applyDutyGate(4);
  EXPECT_TRUE(gated(rm, "*"));
  EXPECT_FALSE(gated(rm, "eu"));          // exempt, even though it is depth 1
  EXPECT_TRUE(gated(rm, "nl"));
  EXPECT_TRUE(gated(rm, "nl-ge"));
  EXPECT_FALSE(gated(rm, "nl-ge-nij"));
}

TEST_F(NlChainGating, ClearingHomeRegionRestoresDepthOnlyGating) {
  rm.setHomeRegion(rm.findByName("nl-ge"));
  rm.applyDutyGate(4);
  rm.setHomeRegion(NULL);
  rm.applyDutyGate(4);   // gate state is recomputed on every application
  EXPECT_TRUE(gated(rm, "nl-ge"));
  EXPECT_FALSE(gated(rm, "nl-ge-nij"));
}

// ---------- multiple regions on the same level ----------

TEST_F(CzFlatGating, MaxGateLevelForTwoLevelTree) {
  EXPECT_EQ(rm.getMaxDepth(), 2);
  EXPECT_EQ(rm.getMaxGateLevel(), 2);
}

TEST_F(CzFlatGating, LevelOneGatesOnlyWildcard) {
  rm.applyDutyGate(1);
  EXPECT_TRUE(gated(rm, "*"));
  EXPECT_FALSE(gated(rm, "cz"));
  EXPECT_FALSE(gated(rm, "cz-ulk"));
  EXPECT_FALSE(gated(rm, "cz-stc"));
  EXPECT_FALSE(gated(rm, "cz-lbk"));
}

TEST_F(CzFlatGating, MaxLevelKeepsAllLeavesOnSameLevel) {
  rm.applyDutyGate(2);
  EXPECT_TRUE(gated(rm, "*"));
  EXPECT_TRUE(gated(rm, "cz"));
  EXPECT_FALSE(gated(rm, "cz-ulk"));
  EXPECT_FALSE(gated(rm, "cz-stc"));
  EXPECT_FALSE(gated(rm, "cz-lbk"));
}

// ---------- degenerate configurations ----------

TEST(RegionGatingEdgeCases, LoneWildcardIsNeverGated) {
  TransportKeyStore ks;
  RegionMap rm(ks);   // no named regions: the wildcard IS the local cluster
  EXPECT_EQ(rm.getMaxDepth(), 0);
  EXPECT_EQ(rm.getMaxGateLevel(), 0);
  rm.applyDutyGate(1);
  EXPECT_FALSE(gated(rm, "*"));
}

TEST(RegionGatingEdgeCases, SingleNamedRegionGatesWildcardButKeepsRegion) {
  TransportKeyStore ks;
  RegionMap rm(ks);
  rm.putRegion("cz", 0);
  EXPECT_EQ(rm.getMaxGateLevel(), 1);
  rm.applyDutyGate(1);
  EXPECT_TRUE(gated(rm, "*"));
  EXPECT_FALSE(gated(rm, "cz"));
}

// ---------- config flags vs runtime gate ----------

TEST_F(NlChainGating, RuntimeGateDoesNotTouchConfigFlags) {
  RegionEntry* eu = rm.findByName("eu");
  eu->flags &= (uint8_t)~REGION_DENY_FLOOD;   // admin: this region allows flood

  rm.applyDutyGate(2);                          // transiently gate 'eu'
  EXPECT_TRUE(eu->rt_flags & REGION_DENY_FLOOD);
  EXPECT_FALSE(eu->flags & REGION_DENY_FLOOD);  // config untouched
  EXPECT_TRUE(eu->effectiveFlags() & REGION_DENY_FLOOD);
}

TEST_F(NlChainGating, RecoveryRestoresConfiguredBehaviour) {
  RegionEntry* eu = rm.findByName("eu");
  eu->flags &= (uint8_t)~REGION_DENY_FLOOD;

  rm.applyDutyGate(2);
  rm.applyDutyGate(0);
  EXPECT_FALSE(eu->rt_flags & REGION_DENY_FLOOD);
  EXPECT_FALSE(eu->effectiveFlags() & REGION_DENY_FLOOD);   // flood allowed again
}

// ---------- transport code 0 (upstream #3568: 'treat code 0 the same as un-scoped') ----------
// Code 0 maps to the wildcard, so it must obey the wildcard's flags (config denyf and the
// runtime duty gate) exactly like a plain un-scoped flood, instead of bypassing them.

static mesh::Packet makeTransportFlood(uint16_t code0, uint16_t code1 = 0) {
  mesh::Packet p;
  p.header = ROUTE_TYPE_TRANSPORT_FLOOD | (PAYLOAD_TYPE_TXT_MSG << PH_TYPE_SHIFT);
  p.payload_len = 4;
  memcpy(p.payload, "abcd", 4);
  p.transport_codes[0] = code0;
  p.transport_codes[1] = code1;
  return p;
}

TEST_F(NlChainGating, ZeroCodeMatchesWildcardWhenWildcardAllowsFlood) {
  auto pkt = makeTransportFlood(0);
  EXPECT_EQ(rm.findMatch(&pkt, REGION_DENY_FLOOD), &rm.getWildcard());
}

TEST_F(NlChainGating, ZeroCodeIsDeniedWhenWildcardDeniesFlood) {
  rm.getWildcard().flags |= REGION_DENY_FLOOD;   // 'region denyf *'
  auto pkt = makeTransportFlood(0);
  EXPECT_EQ(rm.findMatch(&pkt, REGION_DENY_FLOOD), nullptr);
}

TEST_F(NlChainGating, ZeroCodeIsDeniedWhileDutyGateHoldsWildcard) {
  rm.applyDutyGate(1);   // runtime gate on '*', config still allows flood
  auto pkt = makeTransportFlood(0);
  EXPECT_EQ(rm.findMatch(&pkt, REGION_DENY_FLOOD), nullptr);
}

TEST_F(NlChainGating, ZeroCodeWithReturnRegionHintIsStillUnscoped) {
  rm.getWildcard().flags |= REGION_DENY_FLOOD;
  auto pkt = makeTransportFlood(0, 0x1234);   // codes[1] carries a hint, codes[0] is still 0
  EXPECT_EQ(rm.findMatch(&pkt, REGION_DENY_FLOOD), nullptr);
}

TEST_F(NlChainGating, NamedRegionStillMatchesWhenWildcardDeniesFlood) {
  rm.getWildcard().flags |= REGION_DENY_FLOOD;
  RegionEntry* nl = rm.findByName("nl");
  nl->flags &= (uint8_t)~REGION_DENY_FLOOD;      // 'region allowf nl'

  auto pkt = makeTransportFlood(0);
  TransportKey key;
  ASSERT_EQ(rm.getTransportKeysFor(*nl, &key, 1), 1);
  pkt.transport_codes[0] = key.calcTransportCode(&pkt);
  EXPECT_EQ(rm.findMatch(&pkt, REGION_DENY_FLOOD), nl);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
