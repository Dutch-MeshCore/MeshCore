#include <gtest/gtest.h>

// Drives the repeater Filter through its own interface (load, handleCommand,
// allowPacketForward) to check that the per-type rate limiters always match
// the active prefs, whichever way those prefs were arrived at.
#include "../../examples/simple_repeater/Filter.h"

// src/Identity.cpp needs the ed25519 library, which the native env does not
// build. ClientACL only needs the default constructor.
mesh::Identity::Identity() { memset(pub_key, 0, sizeof(pub_key)); }

namespace {

class FixedClock : public mesh::RTCClock {
  uint32_t _now = 1700000000;
public:
  uint32_t getCurrentTime() override { return _now; }
  void setCurrentTime(uint32_t time) override { _now = time; }
};

// A flood ACK: not a priority type, so it reaches the per-type rate limiter.
// The default limit for ACK is 5 per 60 s (FilterPrefs in Filter.h).
mesh::Packet floodAck() {
  mesh::Packet p;
  p.header = ROUTE_TYPE_FLOOD | (PAYLOAD_TYPE_ACK << PH_TYPE_SHIFT);
  p.setPathHashSizeAndCount(1, 0);
  p.payload_len = 4;
  memset(p.payload, 0, sizeof(p.payload));
  return p;
}

// Number of `sent` identical flood ACKs (same second) the filter forwards.
int forwarded(Filter& filter, int sent) {
  mesh::Packet p = floodAck();
  int n = 0;
  for (int i = 0; i < sent; i++) {
    if (filter.allowPacketForward(&p)) n++;
  }
  return n;
}

void command(Filter& filter, FILESYSTEM& fs, const char* cmd) {
  char buf[64];
  char reply[FILTER_REPLY_SIZE] = "";
  strncpy(buf, cmd, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = '\0';
  filter.handleCommand(&fs, buf, reply);
}

}  // namespace

// A repeater with no /filter_prefs yet (the mock FS has no files) must still
// enforce the default rate limits once the filter is switched on.
TEST(FilterLimits, FreshDeviceAppliesDefaultRateLimit) {
  ClientACL acl;
  FixedClock clock;
  FsMock fs;
  Filter filter(acl, clock);

  ASSERT_TRUE(filter.load(&fs));
  command(filter, fs, "filter on");

  EXPECT_EQ(forwarded(filter, 6), 5);
}

// `filter reset` restores the default prefs, so it must restore the default
// limiter too instead of keeping the last `filter rate` setting active.
TEST(FilterLimits, ResetRestoresDefaultRateLimit) {
  ClientACL acl;
  FixedClock clock;
  FsMock fs;
  Filter filter(acl, clock);

  ASSERT_TRUE(filter.load(&fs));
  command(filter, fs, "filter rate 3 100 60");
  command(filter, fs, "filter reset");
  command(filter, fs, "filter on");

  EXPECT_EQ(forwarded(filter, 6), 5);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
