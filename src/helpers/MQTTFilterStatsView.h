#pragma once

#include <stdint.h>

// Plain snapshot of the repeater packet-filter counters + configuration for the
// observer `filter` topic. The repeater (examples/simple_repeater) owns the
// Filter/FilterStats types and populates this; keeping the view a POD with no
// Arduino/mesh dependencies lets the JSON shaping (MQTTFilterStatsJson.h) stay
// in the src/helpers layer and be unit-tested on `native`.
struct MQTTFilterStatsView {
  static const int TYPE_COUNT = 12;
  const char* origin = nullptr;
  const char* origin_id = nullptr;
  const char* timestamp = nullptr;
  uint32_t uptime_secs = 0;
  uint16_t boot_id = 0;
  bool enabled = false;
  bool dryrun = false;                   // drops are counted but still forwarded

  uint32_t hops[TYPE_COUNT] = {};        // per-type hop-limit drops
  uint32_t rate[TYPE_COUNT] = {};        // per-type rate-limit drops
  uint32_t channel_total = 0;
  uint32_t hash_total = 0;
  uint32_t malformed_total = 0;
  uint32_t advert_total = 0;             // drops by the per-origin advert window
  uint32_t path_total = 0;               // drops by the path-prefix block list
  uint32_t air_ms = 0;                   // estimated TX airtime the drops saved
  uint32_t hash_size[4] = {};            // drops by path-hash size 1B..4B
  uint32_t malformed_reason[4] = {};     // short/time/empty/utf8

  // per-type rate/hop configuration (so an analyzer can read drops in context)
  uint16_t cfg_limit[TYPE_COUNT] = {};
  uint32_t cfg_secs[TYPE_COUNT] = {};
  uint16_t cfg_soft[TYPE_COUNT] = {};
  uint8_t  cfg_hops_max[TYPE_COUNT] = {};

  struct Channel { uint8_t hash; const char* name; uint32_t drops; };
  Channel channels[16] = {};
  int channel_count = 0;

  struct Src { uint8_t hash; uint32_t drops; };
  Src top_sources[8] = {};
  int top_count = 0;

  struct HashType { uint8_t type; uint32_t drops; };
  HashType hash_top_types[3] = {};
  int hash_top_count = 0;

  // per-origin advert window (config + live cache fill)
  uint16_t advert_window_h = 0;          // 0 = off
  int advert_cache = 0;                  // origins currently remembered
  int advert_cache_size = 0;             // cache capacity

  // blocked path prefixes with their drops (prefix strings persist in the caller)
  struct Path { const char* prefix; uint32_t drops; };
  Path paths[8] = {};
  int path_count = 0;

  // --- duty-cycle region gating (transient runtime state; see RegionMap::applyDutyGate) ---
  bool    dc_gate_enabled = false;   // feature opt-in flag
  uint8_t dc_gate_duty = 0;          // live TX duty cycle, 0..100 %
  uint8_t dc_gate_level = 0;         // outer layers currently gated (0 = none)
  uint8_t dc_gate_max_level = 0;     // highest gate level this repeater's region tree allows
  uint8_t dc_gate_threshold = 0;     // config: gate above this TX duty %
  uint8_t dc_gate_hysteresis = 0;    // config: recover below (threshold - hysteresis)
};
