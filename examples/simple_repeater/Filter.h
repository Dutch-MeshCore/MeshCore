#pragma once

#include "Mesh.h"

#include <helpers/AdvertDataHelpers.h>
#include <helpers/ClientACL.h>
#include <helpers/TxtDataHelpers.h>

#define FILTER_PREFS_FILE    "/filter_prefs"
#define FILTER_CHANNEL_COUNT 16

static const uint8_t PUBLIC_CHANNEL_SECRET[PUB_KEY_SIZE] = { 0x8B, 0x33, 0x87, 0xE9, 0xC5, 0xCD, 0xEA, 0x6A,
                                                             0xC9, 0xE5, 0xED, 0xBA, 0xA1, 0x15, 0xCD, 0x72,
                                                             0,    0,    0,    0,    0,    0,    0,    0,
                                                             0,    0,    0,    0,    0,    0,    0,    0 };
static const uint8_t PUBLIC_CHANNEL_HASH = 0x11;
static const uint32_t INVALID_TIMESTAMP_WINDOW = (7 * 24 * 60 * 60); // 1 week
static const uint8_t PAYLOAD_TYPE_COUNT = 0x0C;

struct ChannelDetails {
  mesh::GroupChannel channel;
  char name[32];

  ChannelDetails() {
    name[0] = '\0';
    memset(channel.hash, 0, sizeof(channel.hash));
    memset(channel.secret, 0, sizeof(channel.secret));
  }
};

struct PayloadPrefs {
  uint8_t hops_max;
  uint16_t rate_limit;
  uint32_t rate_secs;
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
};

struct Counters {
  uint16_t hops[PAYLOAD_TYPE_COUNT] = {};
  uint16_t rate[PAYLOAD_TYPE_COUNT] = {};
  uint16_t channel = 0;
  uint16_t hash = 0;
  uint16_t malformed = 0;
};

class Limiter {
  uint32_t _start;
  uint32_t _secs;
  uint16_t _limit, _count;

public:
  Limiter() : _limit(0), _secs(0), _start(0), _count(0) {}

  void init(uint16_t limit, uint32_t secs) {
    _limit = limit;
    _secs = secs;
    _start = _count = 0;
  }

  bool allow(uint32_t now) {
    if (!_limit) return true;
    if (now < _start + _secs) {
      if (++_count > _limit) return false;
    } else {
      _start = now;
      _count = 1;
    }
    return true;
  }
};

class Filter {
  ClientACL *_acl;
  mesh::RTCClock *_rtc;
  FilterPrefs _prefs;
  Limiter _limiters[PAYLOAD_TYPE_COUNT];
  Counters _cnt;

public:
  enum ResponseType {
      HOPS,
      RATE,
      COUNT
  };

  Filter(ClientACL &acl, mesh::RTCClock &rtc) : _acl(&acl), _rtc(&rtc) {}
  void resetPrefs(void) { _prefs = FilterPrefs(); }
  void resetStats(void) { _cnt = Counters(); }
  bool allowPacketForward(const mesh::Packet *packet);
  bool hasPriority(const mesh::Packet *packet);
  void handleCommand(FILESYSTEM *fs, char *command, char *reply);
  void formatResponse(char *reply, ResponseType rtype);
  bool addChannel(const char *name);
  bool removeChannel(const char *name);
  static bool getChannelHash(const char *name, mesh::GroupChannel *gc);
  void listChannelNames(char *out_buf, size_t out_size);
  bool validMessageContent(const uint8_t *data, uint8_t len);
  static bool isValidUTF8(const uint8_t *data, uint8_t len);
  bool load(FILESYSTEM *fs);
  bool save(FILESYSTEM *fs) const;
};