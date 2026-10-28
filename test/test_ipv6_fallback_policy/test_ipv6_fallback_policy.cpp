#include "helpers/Ipv6FallbackPolicy.h"

#include <gtest/gtest.h>

using namespace Ipv6FallbackPolicy;

namespace {
constexpr int kConnectFail = MQTTErrorLabels::kTlsFailedConnectToHost;
constexpr int kTimeout = MQTTErrorLabels::kTlsConnectionTimeout;

State afterIpv6Lookup() {
  State s;
  noteLookup(s, true);
  return s;
}
}  // namespace

TEST(Ipv6FallbackPolicy, PrefersIpv6ByDefault) {
  State s;
  EXPECT_TRUE(preferIpv6(s, 0));
  EXPECT_TRUE(preferIpv6(s, 123456));
  EXPECT_STREQ("preferred", stateName(s, 0));
}

TEST(Ipv6FallbackPolicy, ConsecutiveIpv6ConnectFailuresSuspend) {
  State s = afterIpv6Lookup();
  EXPECT_FALSE(noteConnectFailure(s, kConnectFail, 1000));
  EXPECT_TRUE(preferIpv6(s, 1000));
  EXPECT_TRUE(noteConnectFailure(s, kTimeout, 2000));
  EXPECT_FALSE(preferIpv6(s, 2000));
  EXPECT_STREQ("suspended", stateName(s, 2000));
}

TEST(Ipv6FallbackPolicy, SuspensionExpires) {
  State s = afterIpv6Lookup();
  noteConnectFailure(s, kConnectFail, 1000);
  noteConnectFailure(s, kConnectFail, 1000);
  EXPECT_FALSE(preferIpv6(s, 1000 + kSuspendMs - 1));
  EXPECT_TRUE(preferIpv6(s, 1000 + kSuspendMs));
}

// millis() wraps after ~49 days; the window is measured, not compared.
TEST(Ipv6FallbackPolicy, SuspensionSurvivesMillisWrap) {
  State s = afterIpv6Lookup();
  const uint32_t start = UINT32_MAX - 1000;
  noteConnectFailure(s, kConnectFail, start);
  noteConnectFailure(s, kConnectFail, start);
  EXPECT_FALSE(preferIpv6(s, start + 5000));  // wrapped past zero
  EXPECT_TRUE(preferIpv6(s, start + kSuspendMs));
}

// Handshake, certificate and auth failures reached the broker over this family.
TEST(Ipv6FallbackPolicy, NonConnectErrorsDoNotCount) {
  State s = afterIpv6Lookup();
  for (int i = 0; i < 10; i++) {
    EXPECT_FALSE(noteConnectFailure(s, MQTTErrorLabels::kTlsMbedtlsSslSetupFailed, 1000));
    EXPECT_FALSE(noteConnectFailure(s, 0, 1000));
  }
  EXPECT_TRUE(preferIpv6(s, 1000));
}

// A broker with no AAAA record was reached over IPv4; its failures say nothing
// about the IPv6 path.
TEST(Ipv6FallbackPolicy, Ipv4AttemptFailuresDoNotCount) {
  State s;
  noteLookup(s, false);
  for (int i = 0; i < 10; i++) EXPECT_FALSE(noteConnectFailure(s, kConnectFail, 1000));
  EXPECT_TRUE(preferIpv6(s, 1000));
}

TEST(Ipv6FallbackPolicy, Ipv6ConnectResetsFailureCount) {
  State s = afterIpv6Lookup();
  noteConnectFailure(s, kConnectFail, 1000);
  noteConnected(s);
  EXPECT_FALSE(noteConnectFailure(s, kConnectFail, 2000));
  EXPECT_TRUE(preferIpv6(s, 2000));
}

// Connecting over IPv4 while suspended must not lift the suspension, or the
// next reconnect goes straight back to the broken path.
TEST(Ipv6FallbackPolicy, Ipv4ConnectKeepsSuspension) {
  State s = afterIpv6Lookup();
  noteConnectFailure(s, kConnectFail, 1000);
  noteConnectFailure(s, kConnectFail, 1000);
  noteLookup(s, false);
  noteConnected(s);
  EXPECT_FALSE(preferIpv6(s, 2000));
}

TEST(Ipv6FallbackPolicy, RetryAfterExpiryCountsAfresh) {
  State s = afterIpv6Lookup();
  noteConnectFailure(s, kConnectFail, 0);
  noteConnectFailure(s, kConnectFail, 0);
  const uint32_t later = kSuspendMs + 10;
  EXPECT_FALSE(noteConnectFailure(s, kConnectFail, later));
  EXPECT_TRUE(preferIpv6(s, later));
  EXPECT_TRUE(noteConnectFailure(s, kConnectFail, later + 1));
  EXPECT_FALSE(preferIpv6(s, later + 1));
}

TEST(Ipv6FallbackPolicy, LinkChangeClearsSuspension) {
  State s = afterIpv6Lookup();
  noteConnectFailure(s, kConnectFail, 1000);
  noteConnectFailure(s, kConnectFail, 1000);
  noteLinkChange(s);
  EXPECT_TRUE(preferIpv6(s, 1001));
  EXPECT_FALSE(s.last_lookup_ipv6);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
