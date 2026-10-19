#pragma once

#include <stdint.h>

/**
 * \brief  Rolling-window TX-airtime accumulator that reports the true TX duty
 *      cycle (transmit airtime / wall-clock time) as a 0..100 percent.
 *
 * The window is SLOTS fixed-width time bins (SLOTS * SLOT_MS of wall-clock in
 * total). Transmit airtime is credited to the current bin via addAirtime();
 * bins age out as wall-clock advances, so an idle node decays back to 0. All
 * times are milliseconds and the caller injects "now" (the mesh clock), which
 * keeps this type pure and unit-testable off-device.
 *
 * This is the metric the region gate and the observer chart read: a repeater
 * transmitting 4% of the time reads 4%. It is intentionally separate from the
 * Dispatcher's tx_budget_ms airtime limiter, which enforces TX pacing and is
 * left untouched.
 */
class TxDutyWindow {
public:
  static const uint8_t       SLOTS     = 6;
  static const unsigned long SLOT_MS   = 10000;            // 10 s per slot
  static const unsigned long WINDOW_MS = SLOTS * SLOT_MS;  // 60 s window

  TxDutyWindow() { reset(0); }

  // Start (or restart) the window empty, anchored at `now`.
  void reset(unsigned long now) {
    for (uint8_t i = 0; i < SLOTS; i++) _slot[i] = 0;
    _cur = 0;
    _slot_start = now;
  }

  // Credit `airtime_ms` of transmit time that finished at `now`.
  void addAirtime(unsigned long now, unsigned long airtime_ms) {
    advance(now);
    _slot[_cur] += airtime_ms;
  }

  // True TX duty cycle over the last WINDOW_MS, as a rounded 0..100 percent.
  uint8_t percent(unsigned long now) {
    advance(now);
    unsigned long sum = 0;
    for (uint8_t i = 0; i < SLOTS; i++) sum += _slot[i];
    if (sum == 0) return 0;
    if (sum >= WINDOW_MS) return 100;                       // cannot exceed 100%
    return (uint8_t)((sum * 100UL + WINDOW_MS / 2) / WINDOW_MS);
  }

private:
  // Roll the window forward to `now`, clearing bins that have aged out. Uses
  // unsigned subtraction so it is correct across the millis() wrap.
  void advance(unsigned long now) {
    unsigned long elapsed = now - _slot_start;
    if (elapsed < SLOT_MS) return;                          // still in current bin
    unsigned long steps = elapsed / SLOT_MS;
    if (steps >= SLOTS) {                                   // idle >= whole window
      for (uint8_t i = 0; i < SLOTS; i++) _slot[i] = 0;
      _cur = 0;
      _slot_start = now;
      return;
    }
    for (unsigned long s = 0; s < steps; s++) {
      _cur = (uint8_t)((_cur + 1) % SLOTS);
      _slot[_cur] = 0;
    }
    _slot_start += steps * SLOT_MS;
  }

  unsigned long _slot[SLOTS];   // TX airtime (ms) credited to each bin
  unsigned long _slot_start;    // wall-clock start of the current bin
  uint8_t       _cur;           // index of the current bin
};
