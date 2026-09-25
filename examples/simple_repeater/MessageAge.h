#pragma once

#include <stdint.h>

// Timestamp checks on decrypted group text: the malformed scan's sanity window
// and the configurable `filter age` limit. Header-only and clock-free (the
// caller passes `now`) so they run on `native`.

// Upper bound of `filter age`, in minutes: one week, the malformed scan's window.
#define FILTER_AGE_MAX_MINS 10080

namespace MessageAge {

  // 1 Jan 2026 00:00 UTC. A repeater without a set clock boots at 15 May 2024
  // (VolatileRTCClock, ESP32Board) and would need well over a year of uptime to
  // reach this, so a clock below it has not been set. Against such a clock every
  // current message looks far in the future, so no timestamp is judged by it.
  static const uint32_t CLOCK_SET_AFTER = 1767225600;

  static const uint32_t SANITY_WINDOW_SECS = 7UL * 24UL * 60UL * 60UL;

  inline bool clockSet(uint32_t now) {
    return now >= CLOCK_SET_AFTER;
  }

  // Malformed scan: a zero stamp is always invalid; one more than a week off
  // either way only once the clock has been set.
  inline bool implausible(uint32_t ts, uint32_t now) {
    if (ts == 0) return true;
    if (!clockSet(now)) return false;
    return ts < now - SANITY_WINDOW_SECS || ts > now + SANITY_WINDOW_SECS;
  }

  // `filter age`: stamped more than `max_mins` before `now`. Off at 0 and while
  // the clock is unset. A future stamp is never old; the sanity window covers it.
  inline bool tooOld(uint32_t ts, uint32_t now, uint16_t max_mins) {
    if (max_mins == 0 || !clockSet(now)) return false;
    return ts < now && now - ts > (uint32_t)max_mins * 60UL;
  }
}
