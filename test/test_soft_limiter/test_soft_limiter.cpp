#include <gtest/gtest.h>

// Header-only rate limiter with the probabilistic soft cutoff for the repeater
// packet filter. Depends on nothing but <stdint.h>, so it runs on `native`.
#include "../../examples/simple_repeater/Limiter.h"

// Drive `calls` consecutive allow() decisions within a single time window,
// all using rnd for the random byte. Returns the decision of the final call.
static bool decideAt(Limiter& lim, int calls, uint8_t rnd) {
  bool last = false;
  for (int i = 0; i < calls; i++) {
    last = lim.allow(/*now=*/0, rnd);
  }
  return last;
}

// A limit of 0 means "no limit configured" -> always forward.
TEST(SoftLimiter, DisabledLimitAlwaysAllows) {
  Limiter lim;
  lim.init(/*limit=*/0, /*secs=*/60, /*soft=*/0);
  for (int i = 0; i < 100; i++) EXPECT_TRUE(lim.allow(0, 0));
}

// soft == 0 preserves the classic hard cutoff (backwards compatible default).
TEST(SoftLimiter, HardCutoffWhenSoftZero) {
  Limiter lim;
  lim.init(/*limit=*/3, /*secs=*/60, /*soft=*/0);
  EXPECT_TRUE(decideAt(lim, 1, 0));   // count 1
  Limiter lim2; lim2.init(3, 60, 0);
  EXPECT_TRUE(decideAt(lim2, 3, 0));  // count 3 == limit -> allow
  Limiter lim3; lim3.init(3, 60, 0);
  EXPECT_FALSE(decideAt(lim3, 4, 0)); // count 4 > limit -> deny (rnd irrelevant)
}

// soft >= limit leaves no room to ramp -> behaves as a hard cutoff.
TEST(SoftLimiter, SoftAtOrAboveLimitActsAsHardCutoff) {
  Limiter lim; lim.init(/*limit=*/3, /*secs=*/60, /*soft=*/3);
  EXPECT_TRUE(decideAt(lim, 3, 0));
  Limiter lim2; lim2.init(3, 60, 5);
  EXPECT_FALSE(decideAt(lim2, 4, 0));
}

// At or below the soft threshold everything forwards regardless of randomness.
TEST(SoftLimiter, BelowSoftAlwaysAllows) {
  for (int n = 1; n <= 5; n++) {
    Limiter lim; lim.init(/*limit=*/10, /*secs=*/60, /*soft=*/5);
    EXPECT_TRUE(decideAt(lim, n, 0)) << "count=" << n; // even with rnd=0
  }
}

// In the ramp band (soft < count <= limit) forwarding is probabilistic:
// allow when the random byte is below the linear threshold.
// limit=10, soft=5, count=6 -> threshold = 256*(10-6)/(10-5) = 204.
TEST(SoftLimiter, RampAllowsWhenRandomBelowThreshold) {
  Limiter lim; lim.init(10, 60, 5);
  EXPECT_TRUE(decideAt(lim, 6, 203));
}

TEST(SoftLimiter, RampDeniesWhenRandomAtOrAboveThreshold) {
  Limiter a; a.init(10, 60, 5);
  EXPECT_FALSE(decideAt(a, 6, 204));
  Limiter b; b.init(10, 60, 5);
  EXPECT_FALSE(decideAt(b, 6, 255));
}

// At the hard limit the threshold is 0 -> always deny, even with rnd=0.
TEST(SoftLimiter, AtHardLimitAlwaysDenies) {
  Limiter lim; lim.init(10, 60, 5);
  EXPECT_FALSE(decideAt(lim, 10, 0));
}

// Beyond the hard limit is an unconditional deny.
TEST(SoftLimiter, AboveHardLimitAlwaysDenies) {
  Limiter lim; lim.init(10, 60, 5);
  EXPECT_FALSE(decideAt(lim, 11, 0));
}

// Once the moving window rolls over, the budget is restored.
TEST(SoftLimiter, WindowResetRestoresAllowance) {
  Limiter lim; lim.init(/*limit=*/2, /*secs=*/60, /*soft=*/0);
  EXPECT_TRUE(lim.allow(0, 0));    // count 1
  EXPECT_TRUE(lim.allow(0, 0));    // count 2
  EXPECT_FALSE(lim.allow(0, 0));   // count 3 > limit
  EXPECT_TRUE(lim.allow(100, 0));  // now beyond window -> reset to count 1
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
