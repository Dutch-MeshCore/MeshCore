#pragma once

#include <stdint.h>

// Boards built with GPS_DEFAULT_ON turn the GPS on once, including nodes whose saved
// prefs predate the marker. A later "gps off" persists, because the marker stays set.
// Returns true when the prefs changed and need saving.
inline bool applyGpsDefaultOn(uint8_t& gps_enabled, uint8_t& gps_default_applied) {
  if (gps_default_applied) return false;
  gps_enabled = 1;
  gps_default_applied = 1;
  return true;
}
