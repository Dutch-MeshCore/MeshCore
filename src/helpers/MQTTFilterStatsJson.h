#pragma once

#include <ArduinoJson.h>
#include <stdio.h>

#include "MQTTFilterStatsView.h"

// Shapes the observer `filter` topic message from a populated view. Header-only
// and dependent on ArduinoJson alone, so the payload contract and its size are
// unit-tested on `native` (test_mqtt_filter_stats_json). The caller serializes
// `root` into the bridge's FILTER_JSON_BUFFER_SIZE buffer; a message that does
// not fit is dropped whole, which is why the heavy-payload test exists.
namespace MQTTFilterStatsJson {

  inline void addRules(JsonArray arr, const MQTTFilterStatsView::Rule* rules, int count) {
    for (int i = 0; i < count; i++) {
      JsonObject e = arr.add<JsonObject>();
      e["pattern"] = rules[i].pattern;   // persists in filter prefs
      e["secs"] = rules[i].secs;
      e["prob"] = rules[i].prob;
      e["drops"] = rules[i].drops;
      e["pass"] = rules[i].pass;
    }
  }

  inline void fill(JsonObject root, const MQTTFilterStatsView& v) {
    root["timestamp"] = v.timestamp;
    root["origin"] = v.origin;
    root["origin_id"] = v.origin_id;
    root["uptime_secs"] = v.uptime_secs;
    root["boot_id"] = v.boot_id;
    root["enabled"] = v.enabled;
    root["dryrun"] = v.dryrun;

    uint32_t hops_total = 0, rate_total = 0;
    for (int i = 0; i < MQTTFilterStatsView::TYPE_COUNT; i++) {
      hops_total += v.hops[i];
      rate_total += v.rate[i];
    }
    JsonObject totals = root["totals"].to<JsonObject>();
    totals["hops"] = hops_total;
    totals["rate"] = rate_total;
    totals["channel"] = v.channel_total;
    totals["hash"] = v.hash_total;
    totals["malformed"] = v.malformed_total;
    totals["advert"] = v.advert_total;
    totals["path"] = v.path_total;
    totals["sender"] = v.sender_total;
    totals["text"] = v.text_total;
    root["air_ms"] = v.air_ms;

    char key[4];  // two-digit type id, mutable so ArduinoJson copies it

    JsonObject hops = root["hops"].to<JsonObject>();
    for (int i = 0; i < MQTTFilterStatsView::TYPE_COUNT; i++) {
      if (v.hops[i] == 0) continue;
      snprintf(key, sizeof(key), "%02d", i);
      hops[key] = v.hops[i];
    }
    JsonObject rate = root["rate"].to<JsonObject>();
    for (int i = 0; i < MQTTFilterStatsView::TYPE_COUNT; i++) {
      if (v.rate[i] == 0) continue;
      snprintf(key, sizeof(key), "%02d", i);
      rate[key] = v.rate[i];
    }

    JsonObject hash = root["hash"].to<JsonObject>();
    JsonObject hsz = hash["size"].to<JsonObject>();
    hsz["1B"] = v.hash_size[0];
    hsz["2B"] = v.hash_size[1];
    hsz["3B"] = v.hash_size[2];
    if (v.hash_size[3] > 0) hsz["4B"] = v.hash_size[3];
    if (v.hash_top_count > 0) {
      JsonObject htt = hash["top_types"].to<JsonObject>();
      for (int i = 0; i < v.hash_top_count; i++) {
        snprintf(key, sizeof(key), "%02d", v.hash_top_types[i].type);
        htt[key] = v.hash_top_types[i].drops;
      }
    }

    JsonObject mal = root["malformed"].to<JsonObject>();
    mal["short"] = v.malformed_reason[0];
    mal["time"] = v.malformed_reason[1];
    mal["empty"] = v.malformed_reason[2];
    mal["utf8"] = v.malformed_reason[3];

    if (v.channel_count > 0) {
      JsonArray chs = root["channels"].to<JsonArray>();
      char hh[3];
      for (int i = 0; i < v.channel_count; i++) {
        JsonObject e = chs.add<JsonObject>();
        snprintf(hh, sizeof(hh), "%02x", v.channels[i].hash);
        e["hash"] = hh;                       // mutable char[] -> copied
        e["name"] = v.channels[i].name;       // persists in filter prefs
        e["drops"] = v.channels[i].drops;
      }
    }

    if (v.top_count > 0) {
      JsonArray top = root["top_sources"].to<JsonArray>();
      char hh[3];
      for (int i = 0; i < v.top_count; i++) {
        JsonObject e = top.add<JsonObject>();
        snprintf(hh, sizeof(hh), "%02x", v.top_sources[i].hash);
        e["hash"] = hh;
        e["drops"] = v.top_sources[i].drops;
      }
    }

    // Per-origin advert window: config plus live cache fill.
    JsonObject adv = root["advert"].to<JsonObject>();
    adv["window_h"] = v.advert_window_h;
    adv["cache"] = v.advert_cache;
    adv["cache_size"] = v.advert_cache_size;

    // Blocked path prefixes with their drops; omitted entirely when none are set.
    if (v.path_count > 0) {
      JsonArray paths = root["paths"].to<JsonArray>();
      for (int i = 0; i < v.path_count; i++) {
        JsonObject e = paths.add<JsonObject>();
        e["prefix"] = v.paths[i].prefix;      // persists in the caller's buffer
        e["drops"] = v.paths[i].drops;
      }
    }

    // Sender / text rules with their drops and throttle passes; each list is
    // omitted when empty, as is the watch list.
    if (v.sender_count > 0) addRules(root["senders"].to<JsonArray>(), v.senders, v.sender_count);
    if (v.text_count > 0) addRules(root["texts"].to<JsonArray>(), v.texts, v.text_count);
    if (v.watch_count > 0) {
      JsonArray watch = root["watch"].to<JsonArray>();
      for (int i = 0; i < v.watch_count; i++) watch.add(v.watch[i]);   // persists in prefs
    }

    JsonObject cfg = root["config"].to<JsonObject>();
    for (int i = 0; i < MQTTFilterStatsView::TYPE_COUNT; i++) {
      snprintf(key, sizeof(key), "%02d", i);
      JsonObject c = cfg[key].to<JsonObject>();
      c["limit"] = v.cfg_limit[i];
      c["secs"] = v.cfg_secs[i];
      c["soft"] = v.cfg_soft[i];
      c["hops_max"] = v.cfg_hops_max[i];
    }

    // Duty-cycle region gating: live shed state so the observer can map which
    // repeaters are gating inter-region traffic, and how hard, mesh-wide.
    JsonObject rg = root["region_gate"].to<JsonObject>();
    rg["enabled"] = v.dc_gate_enabled;
    rg["duty"] = v.dc_gate_duty;              // live TX duty cycle %
    rg["level"] = v.dc_gate_level;            // outer layers currently gated
    rg["max_level"] = v.dc_gate_max_level;    // highest gate level this tree allows
    rg["threshold"] = v.dc_gate_threshold;    // config: gate above this %
    rg["hysteresis"] = v.dc_gate_hysteresis;  // config: recover below (threshold - hysteresis)
  }
}
