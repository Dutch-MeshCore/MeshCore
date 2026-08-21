#pragma once

#include <stdint.h>

// Per-payload-type moving-window rate limiter with an optional probabilistic
// "soft cutoff". Below `soft` every packet forwards; between `soft` and the
// hard `limit` the forward probability ramps linearly to zero; above `limit`
// nothing forwards. Randomness is injected (rnd, 0-255) so the decision is a
// pure function and can be unit-tested deterministically.
class Limiter {
  uint32_t _start;
  uint32_t _secs;
  uint16_t _limit, _soft, _count;

public:
  Limiter() : _start(0), _secs(0), _limit(0), _soft(0), _count(0) {}

  void init(uint16_t limit, uint32_t secs, uint16_t soft = 0) {
    _limit = limit;
    _soft = soft;
    _secs = secs;
    _start = _count = 0;
  }

  bool allow(uint32_t now, uint8_t rnd) {
    if (!_limit) return true;

    uint16_t count;
    if (now < _start + _secs) {
      count = ++_count;
    } else {
      _start = now;
      _count = 1;
      count = 1;
    }

    // No soft band configured -> classic hard cutoff.
    if (_soft == 0 || _soft >= _limit) {
      return count <= _limit;
    }

    // Soft cutoff: forward everything up to `soft`, drop everything past
    // `limit`, and ramp the forward probability linearly down to zero in
    // between so the "door closes gently" as the budget nears exhaustion.
    if (count <= _soft) return true;
    if (count > _limit) return false;

    // threshold in [0, 256): p(forward) = (limit - count) / (limit - soft).
    uint16_t threshold = (uint16_t)(256UL * (_limit - count) / (_limit - _soft));
    return rnd < threshold;
  }
};
