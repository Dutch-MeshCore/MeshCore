#pragma once

#include <stdint.h>
#include <string.h>

// Per-origin advert rate limiter: each node's flood advert is forwarded at
// most once per window, so a node that re-advertises every few minutes stops
// being re-flooded by this repeater while it still stays reachable through it
// once per window. Complements the per-type rate limiter, which caps the total
// advert budget and would otherwise drop legitimate nodes together with the
// chatty one.
//
// Origins are keyed on the first 4 bytes of the advert's public key. The cache
// is a ring of fixed size: once full, the oldest entry is overwritten. Time is
// millis-based and compared with unsigned subtraction, so it survives the
// 49-day wrap. Header-only and dependency-free so it runs on the `native` env.

#define ADVERT_CACHE_SIZE 256
#define ADVERT_KEY_LEN    4
#define ADVERT_MAX_HOURS  720   // 30 days; keeps hours*3600*1000 inside uint32

class AdvertLimiter {
  struct Entry {
    uint8_t key[ADVERT_KEY_LEN];
    uint32_t seen_ms;
  };

  Entry _cache[ADVERT_CACHE_SIZE];
  uint16_t _count;   // entries in use
  uint16_t _head;    // next entry to overwrite once the cache is full
  uint16_t _hours;   // 0 = off

public:
  AdvertLimiter() : _count(0), _head(0), _hours(0) { memset(_cache, 0, sizeof(_cache)); }

  void setWindowHours(uint16_t hours) { _hours = hours > ADVERT_MAX_HOURS ? ADVERT_MAX_HOURS : hours; }
  uint16_t getWindowHours() const { return _hours; }
  int getCount() const { return _count; }
  int getCapacity() const { return ADVERT_CACHE_SIZE; }

  void clear() {
    _count = 0;
    _head = 0;
  }

  // True when the advert from `key` may be forwarded now (and records it),
  // false when the same origin already passed inside the current window.
  bool allow(const uint8_t* key, uint32_t now_ms) {
    if (_hours == 0 || key == nullptr) return true;

    uint32_t window_ms = (uint32_t)_hours * 3600UL * 1000UL;

    for (int i = 0; i < _count; i++) {
      Entry& e = _cache[i];
      if (memcmp(e.key, key, ADVERT_KEY_LEN) != 0) continue;
      if (now_ms - e.seen_ms < window_ms) return false;   // unsigned: wrap-safe
      e.seen_ms = now_ms;   // window elapsed: pass, and start a new window
      return true;
    }

    Entry* e;
    if (_count < ADVERT_CACHE_SIZE) {
      e = &_cache[_count++];
    } else {
      e = &_cache[_head];   // overwrite the oldest entry
      _head = (uint16_t)((_head + 1) % ADVERT_CACHE_SIZE);
    }
    memcpy(e->key, key, ADVERT_KEY_LEN);
    e->seen_ms = now_ms;
    return true;
  }
};
