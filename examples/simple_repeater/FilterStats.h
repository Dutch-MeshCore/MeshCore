#pragma once

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// Only Packet.h is pulled in, for the PAYLOAD_TYPE_* constants. It depends on
// nothing beyond MeshCore.h, so this header stays compilable on the `native`
// env and the counting and formatting logic can be unit-tested off-device.
#include <Packet.h>

#define FILTER_CHANNEL_COUNT 16

// Widest path hash the wire format can encode: (path_len >> 6) + 1.
#define FILTER_HASH_SIZE_COUNT 4

// A node identity only ever appears in a payload as a 1-byte prefix of its
// public key, so one bucket per possible value covers every source.
#define FILTER_SRC_COUNT 256

// How many offenders `filter stats top` reports.
#define FILTER_TOP_COUNT 8

static const uint8_t PAYLOAD_TYPE_COUNT = 0x0C;

// Why validMessageContent() rejected a public-channel text.
enum MalformedReason {
  MALFORMED_SHORT = 0,      // payload too short to hold timestamp + type
  MALFORMED_TIMESTAMP = 1,  // zero, or outside the accepted window
  MALFORMED_EMPTY = 2,      // plain text message with no text
  MALFORMED_UTF8 = 3,       // text is not valid UTF-8
  MALFORMED_REASON_COUNT = 4
};

struct PayloadPrefs {
  uint8_t hops_max;
  uint16_t rate_limit;
  uint32_t rate_secs;
};

struct Counters {
  uint32_t hops[PAYLOAD_TYPE_COUNT] = {};
  uint32_t rate[PAYLOAD_TYPE_COUNT] = {};
  uint32_t channel = 0;
  uint32_t hash = 0;
  uint32_t malformed = 0;

  // breakdowns
  uint32_t hash_size[FILTER_HASH_SIZE_COUNT] = {};       // path hash size of hash-blocked packets
  uint32_t hash_type[PAYLOAD_TYPE_COUNT] = {};           // payload type of hash-blocked packets
  uint32_t channel_slot[FILTER_CHANNEL_COUNT] = {};      // drops per blocked channel
  uint32_t malformed_reason[MALFORMED_REASON_COUNT] = {};
  uint16_t src[FILTER_SRC_COUNT] = {};                   // drops per source hash byte

  void reset() { *this = Counters(); }
};

namespace FilterStat {

  // Counters saturate rather than wrap: a repeater under attack is exactly when
  // the numbers matter, and a wrapped total reads as "barely anything happened".
  inline void bump(uint32_t& counter) {
    if (counter < 0xFFFFFFFFu) counter++;
  }

  inline void bump(uint16_t& counter) {
    if (counter < 0xFFFFu) counter++;
  }

  // Every record*() adds to its total unconditionally and to the breakdown only
  // when the bucket is in range, so an unexpected value can never lose a drop.

  inline void recordHops(Counters& c, uint8_t type) {
    if (type < PAYLOAD_TYPE_COUNT) bump(c.hops[type]);
  }

  inline void recordRate(Counters& c, uint8_t type) {
    if (type < PAYLOAD_TYPE_COUNT) bump(c.rate[type]);
  }

  inline void recordHash(Counters& c, uint8_t hash_size, uint8_t type) {
    bump(c.hash);
    if (hash_size >= 1 && hash_size <= FILTER_HASH_SIZE_COUNT) bump(c.hash_size[hash_size - 1]);
    if (type < PAYLOAD_TYPE_COUNT) bump(c.hash_type[type]);
  }

  inline void recordChannel(Counters& c, int slot) {
    bump(c.channel);
    if (slot >= 0 && slot < FILTER_CHANNEL_COUNT) bump(c.channel_slot[slot]);
  }

  inline void recordMalformed(Counters& c, uint8_t reason) {
    bump(c.malformed);
    if (reason < MALFORMED_REASON_COUNT) bump(c.malformed_reason[reason]);
  }

  inline void recordSrc(Counters& c, uint8_t src_hash) {
    bump(c.src[src_hash]);
  }

  // Bounded, always-NUL-terminated text appender.
  class Buf {
    char* _buf;
    size_t _cap;
    size_t _len;
    bool _truncated;

  public:
    Buf(char* buf, size_t cap) : _buf(buf), _cap(cap), _len(0), _truncated(false) {
      if (_cap > 0) _buf[0] = '\0';
    }

    // Appends a formatted fragment. An append that does not fit is rejected in
    // full rather than clipped, so a partial number can never be read as a whole
    // one. Once rejected the buffer is sealed: later, shorter appends would
    // otherwise reorder the output.
    bool add(const char* fmt, ...) {
      if (_truncated || _cap == 0) {
        _truncated = true;
        return false;
      }

      size_t room = _cap - _len;   // includes space for the terminator

      va_list args;
      va_start(args, fmt);
      int n = vsnprintf(_buf + _len, room, fmt, args);
      va_end(args);

      if (n < 0 || (size_t)n >= room) {
        _buf[_len] = '\0';
        _truncated = true;
        return false;
      }

      _len += (size_t)n;
      return true;
    }

    // Forces `marker` onto the end when output was clipped, trimming the tail if
    // that is what it takes. Without it a clipped list reads as a complete one.
    void markTruncated(const char* marker) {
      if (!_truncated || _cap == 0) return;

      size_t m = strlen(marker);
      if (m + 1 > _cap) return;

      size_t at = (_len + m + 1 <= _cap) ? _len : _cap - m - 1;
      memcpy(_buf + at, marker, m);
      _buf[at + m] = '\0';
      _len = at + m;
    }

    size_t len() const { return _len; }
    bool truncated() const { return _truncated; }
  };

  // Picks the highest-valued entries of `values`, ordered by value descending
  // and index ascending so repeated calls agree. Zero entries are never picked.
  template <typename T>
  inline int topIndices(const T* values, int count, int max, uint8_t* idx_out, T* val_out) {
    if (values == nullptr || idx_out == nullptr || max <= 0) return 0;

    int n = 0;
    uint32_t prev_val = 0xFFFFFFFFu;
    int prev_idx = -1;

    while (n < max) {
      int best = -1;
      for (int i = 0; i < count; i++) {
        uint32_t v = (uint32_t)values[i];
        if (v == 0) continue;
        // must fall after the previous pick in the (value, index) ordering
        if (v > prev_val) continue;
        if (v == prev_val && i <= prev_idx) continue;
        if (best < 0 || v > (uint32_t)values[best]) best = i;
      }
      if (best < 0) break;

      idx_out[n] = (uint8_t)best;
      if (val_out != nullptr) val_out[n] = values[best];
      prev_val = (uint32_t)values[best];
      prev_idx = best;
      n++;
    }
    return n;
  }

  struct TopEntry {
    uint8_t hash;
    uint16_t count;
  };

  inline int topSrc(const Counters& c, TopEntry* out, int max) {
    if (out == nullptr || max <= 0) return 0;
    if (max > FILTER_TOP_COUNT) max = FILTER_TOP_COUNT;

    uint8_t idx[FILTER_TOP_COUNT];
    uint16_t val[FILTER_TOP_COUNT];
    int n = topIndices(c.src, FILTER_SRC_COUNT, max, idx, val);

    for (int i = 0; i < n; i++) {
      out[i].hash = idx[i];
      out[i].count = val[i];
    }
    return n;
  }

  inline uint32_t addSat(uint32_t a, uint32_t b) {
    return (a > 0xFFFFFFFFu - b) ? 0xFFFFFFFFu : a + b;
  }

  // ---- `filter` and `filter count`: unchanged shapes, now bounded ------------

  inline void formatSummary(char* out, size_t cap, const Counters& c, bool enabled) {
    uint32_t hops_total = 0;
    uint32_t rate_total = 0;
    for (uint8_t i = 0; i < PAYLOAD_TYPE_COUNT; i++) {
      hops_total = addSat(hops_total, c.hops[i]);
      rate_total = addSat(rate_total, c.rate[i]);
    }

    Buf b(out, cap);
    b.add("> Filter %s: Blocked [ Hops: %lu | Rate: %lu | Channel: %lu | Hash: %lu | Malformed: %lu ]",
          enabled ? "on" : "off",
          (unsigned long)hops_total, (unsigned long)rate_total,
          (unsigned long)c.channel, (unsigned long)c.hash, (unsigned long)c.malformed);
    b.markTruncated("..");
  }

  inline void formatCount(char* out, size_t cap, const Counters& c) {
    Buf b(out, cap);

    b.add("[TYPE: HOPS,RATE]");
    for (uint8_t i = 0; i < PAYLOAD_TYPE_COUNT; i++) {
      b.add("\n%02u: %lu,%lu", (unsigned)i, (unsigned long)c.hops[i], (unsigned long)c.rate[i]);
    }
    b.markTruncated("..");
  }

  // ---- `filter stats <topic>`: one topic per reply, each with a full buffer --

  inline void formatStatsHops(char* out, size_t cap, const Counters& c, const PayloadPrefs* prefs) {
    Buf b(out, cap);

    bool any = false;
    for (uint8_t i = 0; i < PAYLOAD_TYPE_COUNT; i++) {
      if (c.hops[i] > 0) { any = true; break; }
    }
    if (!any) {
      b.add("> Filter: no hop drops recorded");
      return;
    }

    // Only the types that dropped something, so the limit that caused each drop
    // fits on the same line as the count.
    b.add("[TYPE: DROPS(MAX)]");
    for (uint8_t i = 0; i < PAYLOAD_TYPE_COUNT; i++) {
      if (c.hops[i] == 0) continue;
      b.add("\n%02u: %lu(%u)", (unsigned)i, (unsigned long)c.hops[i], (unsigned)prefs[i].hops_max);
    }
    b.markTruncated("..");
  }

  inline void formatStatsRate(char* out, size_t cap, const Counters& c, const PayloadPrefs* prefs) {
    Buf b(out, cap);

    bool any = false;
    for (uint8_t i = 0; i < PAYLOAD_TYPE_COUNT; i++) {
      if (c.rate[i] > 0) { any = true; break; }
    }
    if (!any) {
      b.add("> Filter: no rate drops recorded");
      return;
    }

    b.add("[TYPE: DROPS(LIMIT/SECS)]");
    for (uint8_t i = 0; i < PAYLOAD_TYPE_COUNT; i++) {
      if (c.rate[i] == 0) continue;
      b.add("\n%02u: %lu(%u/%lu)", (unsigned)i, (unsigned long)c.rate[i],
            (unsigned)prefs[i].rate_limit, (unsigned long)prefs[i].rate_secs);
    }
    b.markTruncated("..");
  }

  inline void formatStatsHash(char* out, size_t cap, const Counters& c) {
    Buf b(out, cap);

    if (c.hash == 0) {
      b.add("> Filter: no path hash drops recorded");
      return;
    }

    b.add("> Blocked %lu [1B:%lu 2B:%lu 3B:%lu", (unsigned long)c.hash,
          (unsigned long)c.hash_size[0], (unsigned long)c.hash_size[1], (unsigned long)c.hash_size[2]);
    if (c.hash_size[3] > 0) b.add(" 4B:%lu", (unsigned long)c.hash_size[3]);
    b.add("]");

    uint8_t idx[3];
    uint32_t val[3];
    int n = topIndices(c.hash_type, PAYLOAD_TYPE_COUNT, 3, idx, val);
    if (n > 0) {
      b.add("\n  Top types: ");
      for (int i = 0; i < n; i++) {
        b.add(i == 0 ? "%02u:%lu" : " %02u:%lu", (unsigned)idx[i], (unsigned long)val[i]);
      }
    }
    b.markTruncated("..");
  }

  inline void formatStatsMalformed(char* out, size_t cap, const Counters& c) {
    Buf b(out, cap);

    if (c.malformed == 0) {
      b.add("> Filter: no malformed drops recorded");
      return;
    }

    b.add("> Blocked %lu [ short:%lu time:%lu empty:%lu utf8:%lu ]",
          (unsigned long)c.malformed,
          (unsigned long)c.malformed_reason[MALFORMED_SHORT],
          (unsigned long)c.malformed_reason[MALFORMED_TIMESTAMP],
          (unsigned long)c.malformed_reason[MALFORMED_EMPTY],
          (unsigned long)c.malformed_reason[MALFORMED_UTF8]);
    b.markTruncated("..");
  }

  inline void formatStatsTop(char* out, size_t cap, const Counters& c) {
    TopEntry top[FILTER_TOP_COUNT];
    int n = topSrc(c, top, FILTER_TOP_COUNT);

    Buf b(out, cap);
    if (n == 0) {
      b.add("> Filter: no source drops recorded");
      return;
    }

    b.add("> Top drops:");
    for (int i = 0; i < n; i++) {
      b.add(" %02x:%u", (unsigned)top[i].hash, (unsigned)top[i].count);
    }
    b.markTruncated("..");
  }
}
