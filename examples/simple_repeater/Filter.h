#pragma once

#include "Mesh.h"

#include <helpers/AdvertDataHelpers.h>
#include <helpers/ClientACL.h>
#include <helpers/TxtDataHelpers.h>

#include "FilterStats.h"
#include "Limiter.h"

#define FILTER_PREFS_FILE    "/filter_prefs"

// Size of the `reply` buffer every CLI command writes into, both over serial
// (main.cpp) and over the mesh (MyMesh.cpp).
#define FILTER_REPLY_SIZE    160

static const uint8_t PUBLIC_CHANNEL_SECRET[PUB_KEY_SIZE] = { 0x8B, 0x33, 0x87, 0xE9, 0xC5, 0xCD, 0xEA, 0x6A,
                                                             0xC9, 0xE5, 0xED, 0xBA, 0xA1, 0x15, 0xCD, 0x72,
                                                             0,    0,    0,    0,    0,    0,    0,    0,
                                                             0,    0,    0,    0,    0,    0,    0,    0 };
static const uint8_t PUBLIC_CHANNEL_HASH = 0x11;
static const uint32_t INVALID_TIMESTAMP_WINDOW = (7 * 24 * 60 * 60); // 1 week

struct ChannelDetails {
  mesh::GroupChannel channel;
  char name[32];

  ChannelDetails() {
    name[0] = '\0';
    memset(channel.hash, 0, sizeof(channel.hash));
    memset(channel.secret, 0, sizeof(channel.secret));
  }
};

struct FilterPrefs {
  uint8_t filter_enabled = false;
  PayloadPrefs payload_prefs[PAYLOAD_TYPE_COUNT] = {
    [PAYLOAD_TYPE_REQ] = { .hops_max = 8, .rate_limit = 5, .rate_secs = 60 },
    [PAYLOAD_TYPE_RESPONSE] = { .hops_max = 8, .rate_limit = 5, .rate_secs = 60 },
    [PAYLOAD_TYPE_TXT_MSG] = { .hops_max = 8, .rate_limit = 20, .rate_secs = 60 },
    [PAYLOAD_TYPE_ACK] = { .hops_max = 8, .rate_limit = 5, .rate_secs = 60 },
    [PAYLOAD_TYPE_ADVERT] = { .hops_max = 8, .rate_limit = 10, .rate_secs = 60 },
    [PAYLOAD_TYPE_GRP_TXT] = { .hops_max = 32, .rate_limit = 20, .rate_secs = 60 },
    [PAYLOAD_TYPE_GRP_DATA] = { .hops_max = 8, .rate_limit = 5, .rate_secs = 60 },
    [PAYLOAD_TYPE_ANON_REQ] = { .hops_max = 8, .rate_limit = 5, .rate_secs = 60 },
    [PAYLOAD_TYPE_PATH] = { .hops_max = 8, .rate_limit = 5, .rate_secs = 60 },
    [PAYLOAD_TYPE_TRACE] = { .hops_max = 8, .rate_limit = 5, .rate_secs = 60 },
    [PAYLOAD_TYPE_MULTIPART] = { .hops_max = 8, .rate_limit = 5, .rate_secs = 60 },
    [PAYLOAD_TYPE_CONTROL] = { .hops_max = 8, .rate_limit = 5, .rate_secs = 60 },
  };
  ChannelDetails filter_channels[FILTER_CHANNEL_COUNT];
  uint8_t minimal_hash_bytes = 1;
  uint8_t filter_malformed = false;
  // Appended at the end of the struct so older /filter_prefs files (which lack
  // these bytes) still load: a short read leaves them at 0 = soft cutoff off.
  uint16_t soft_limit[PAYLOAD_TYPE_COUNT] = {};
};

class Filter {
  ClientACL *_acl;
  mesh::RTCClock *_rtc;
  FilterPrefs _prefs;
  Limiter _limiters[PAYLOAD_TYPE_COUNT];
  Counters _cnt;
  uint32_t _rng_state;

  // xorshift32: cheap, dependency-free source of a random byte for the soft
  // cutoff. State is never allowed to reach 0 (that would freeze the sequence).
  uint8_t nextRandom() {
    uint32_t x = _rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    _rng_state = x ? x : 0xA5A5F00D;
    return (uint8_t)(_rng_state >> 24);
  }

public:
  enum ResponseType {
      HOPS,
      RATE
  };

  Filter(ClientACL &acl, mesh::RTCClock &rtc) : _acl(&acl), _rtc(&rtc), _rng_state(0xA5A5F00D) {}
  void resetPrefs(void) { _prefs = FilterPrefs(); }
  void resetStats(void) { _cnt.reset(); }
  bool allowPacketForward(const mesh::Packet *packet);
  bool hasPriority(const mesh::Packet *packet);
  static bool srcHash(const mesh::Packet *packet, uint8_t *out);
  void handleCommand(FILESYSTEM *fs, char *command, char *reply);
  void formatResponse(char *reply, ResponseType rtype);
  bool addChannel(const char *name);
  bool removeChannel(const char *name);
  static bool getChannelHash(const char *name, mesh::GroupChannel *gc);
  void listChannelNames(char *out_buf, size_t out_size, bool with_counts = false);
  bool validMessageContent(const uint8_t *data, uint8_t len, uint8_t *reason);
  static bool isValidUTF8(const uint8_t *data, uint8_t len);
  bool load(FILESYSTEM *fs);
  bool save(FILESYSTEM *fs) const;
};