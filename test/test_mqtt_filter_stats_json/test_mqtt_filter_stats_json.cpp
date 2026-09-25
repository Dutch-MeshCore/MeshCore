#include <gtest/gtest.h>

#include <ArduinoJson.h>
#include <cstring>
#include <string>

// Header-only JSON shaping of the observer `filter` topic. It depends only on
// ArduinoJson and the POD view struct, so the exact payload contract (and its
// size against the 4 KB publish buffer) is verified on `native`.
#include "helpers/MQTTFilterStatsJson.h"

static const size_t PUBLISH_BUFFER = 4096;   // MQTTBridge::FILTER_JSON_BUFFER_SIZE

static void envelope(MQTTFilterStatsView& v) {
  v.origin = "repeater-1";
  v.origin_id = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
  v.timestamp = "2026-09-25T12:00:00.000Z";
  v.uptime_secs = 86400;
  v.boot_id = 4711;
  v.enabled = true;
}

static std::string render(const MQTTFilterStatsView& v, size_t* len_out = nullptr) {
  JsonDocument doc;
  JsonObject root = doc.to<JsonObject>();
  MQTTFilterStatsJson::fill(root, v);
  std::string out;
  serializeJson(root, out);
  if (len_out) *len_out = out.size();
  return out;
}

TEST(MQTTFilterStatsJson, KeepsTheEstablishedTopLevelContract) {
  MQTTFilterStatsView v;
  envelope(v);

  JsonDocument doc;
  deserializeJson(doc, render(v));

  EXPECT_STREQ("repeater-1", doc["origin"]);
  EXPECT_EQ(4711, doc["boot_id"].as<int>());
  EXPECT_TRUE(doc["enabled"].as<bool>());
  EXPECT_TRUE(doc["totals"].is<JsonObject>());
  EXPECT_TRUE(doc["config"]["05"].is<JsonObject>());
  EXPECT_TRUE(doc["region_gate"].is<JsonObject>());
}

TEST(MQTTFilterStatsJson, PublishesDryRunAndTheNewDropTotals) {
  MQTTFilterStatsView v;
  envelope(v);
  v.dryrun = true;
  v.advert_total = 132;
  v.path_total = 412;
  v.air_ms = 214500;

  JsonDocument doc;
  deserializeJson(doc, render(v));

  EXPECT_TRUE(doc["dryrun"].as<bool>());
  EXPECT_EQ(132u, doc["totals"]["advert"].as<uint32_t>());
  EXPECT_EQ(412u, doc["totals"]["path"].as<uint32_t>());
  EXPECT_EQ(214500u, doc["air_ms"].as<uint32_t>());
}

TEST(MQTTFilterStatsJson, PublishesTheAdvertWindowState) {
  MQTTFilterStatsView v;
  envelope(v);
  v.advert_window_h = 48;
  v.advert_cache = 87;
  v.advert_cache_size = 256;

  JsonDocument doc;
  deserializeJson(doc, render(v));

  EXPECT_EQ(48, doc["advert"]["window_h"].as<int>());
  EXPECT_EQ(87, doc["advert"]["cache"].as<int>());
  EXPECT_EQ(256, doc["advert"]["cache_size"].as<int>());
}

TEST(MQTTFilterStatsJson, PublishesTheMessageAgeLimit) {
  MQTTFilterStatsView v;
  envelope(v);
  v.age_total = 37;
  v.age_max_mins = 60;
  v.age_clock_set = true;

  JsonDocument doc;
  deserializeJson(doc, render(v));

  EXPECT_EQ(37u, doc["totals"]["age"].as<uint32_t>());
  EXPECT_EQ(60, doc["age"]["max_mins"].as<int>());
  EXPECT_TRUE(doc["age"]["clock_set"].as<bool>());
}

TEST(MQTTFilterStatsJson, AgeLimitOffStillReportsTheClockState) {
  MQTTFilterStatsView v;
  envelope(v);

  JsonDocument doc;
  deserializeJson(doc, render(v));

  EXPECT_EQ(0, doc["age"]["max_mins"].as<int>());
  EXPECT_FALSE(doc["age"]["clock_set"].as<bool>());
  EXPECT_EQ(0u, doc["totals"]["age"].as<uint32_t>());
}

TEST(MQTTFilterStatsJson, PublishesBlockedPathPrefixesWithDropsOnlyWhenConfigured) {
  MQTTFilterStatsView v;
  envelope(v);

  JsonDocument none;
  deserializeJson(none, render(v));
  EXPECT_FALSE(none["paths"].is<JsonArray>()) << "no paths block when nothing is blocked";

  v.paths[0].prefix = "A1B2";
  v.paths[0].drops = 5;
  v.paths[1].prefix = "C3";
  v.paths[1].drops = 0;
  v.path_count = 2;

  JsonDocument doc;
  deserializeJson(doc, render(v));
  ASSERT_EQ(2u, doc["paths"].size());
  EXPECT_STREQ("A1B2", doc["paths"][0]["prefix"]);
  EXPECT_EQ(5u, doc["paths"][0]["drops"].as<uint32_t>());
  EXPECT_STREQ("C3", doc["paths"][1]["prefix"]);
}

TEST(MQTTFilterStatsJson, PublishesSenderAndTextRulesWithTheirCountsOnlyWhenSet) {
  MQTTFilterStatsView v;
  envelope(v);

  JsonDocument none;
  deserializeJson(none, render(v));
  EXPECT_FALSE(none["senders"].is<JsonArray>());
  EXPECT_FALSE(none["texts"].is<JsonArray>());
  EXPECT_FALSE(none["watch"].is<JsonArray>());

  v.sender_total = 15;
  v.text_total = 7;
  v.senders[0] = { "Bob", 60, 100, 12, 3 };
  v.senders[1] = { "Bot*", 0, 50, 3, 0 };
  v.sender_count = 2;
  v.texts[0] = { "^BEACON", 0, 100, 7, 0 };
  v.text_count = 1;
  v.watch[0] = "#bots";
  v.watch_count = 1;

  JsonDocument doc;
  deserializeJson(doc, render(v));

  EXPECT_EQ(15u, doc["totals"]["sender"].as<uint32_t>());
  EXPECT_EQ(7u, doc["totals"]["text"].as<uint32_t>());
  ASSERT_EQ(2u, doc["senders"].size());
  EXPECT_STREQ("Bob", doc["senders"][0]["pattern"]);
  EXPECT_EQ(60, doc["senders"][0]["secs"].as<int>());
  EXPECT_EQ(100, doc["senders"][0]["prob"].as<int>());
  EXPECT_EQ(12u, doc["senders"][0]["drops"].as<uint32_t>());
  EXPECT_EQ(3u, doc["senders"][0]["pass"].as<uint32_t>());
  EXPECT_EQ(50, doc["senders"][1]["prob"].as<int>());
  ASSERT_EQ(1u, doc["texts"].size());
  EXPECT_STREQ("^BEACON", doc["texts"][0]["pattern"]);
  ASSERT_EQ(1u, doc["watch"].size());
  EXPECT_STREQ("#bots", doc["watch"][0]);
}

// A busy repeater with every feature in use must still fit the publish buffer,
// otherwise the message is silently skipped (buildFilterStatsMessage returns 0).
TEST(MQTTFilterStatsJson, HeavyRealisticPayloadFitsThePublishBuffer) {
  MQTTFilterStatsView v;
  envelope(v);
  v.dryrun = true;
  for (int i = 0; i < MQTTFilterStatsView::TYPE_COUNT; i++) {
    v.hops[i] = 123456;
    v.rate[i] = 654321;
    v.cfg_limit[i] = 20;
    v.cfg_secs[i] = 60;
    v.cfg_soft[i] = 15;
    v.cfg_hops_max[i] = 32;
  }
  v.channel_total = 999999;
  v.hash_total = 999999;
  v.malformed_total = 999999;
  v.advert_total = 999999;
  v.path_total = 999999;
  v.age_total = 999999;
  v.age_max_mins = 10080;
  v.age_clock_set = true;
  v.air_ms = 4294967295u;
  for (int j = 0; j < 4; j++) { v.hash_size[j] = 123456; v.malformed_reason[j] = 123456; }
  static const char* names[] = { "#wardriving", "#memes", "#weather", "#local" };
  for (int i = 0; i < 4; i++) { v.channels[i].hash = 0xa0 + i; v.channels[i].name = names[i]; v.channels[i].drops = 123456; }
  v.channel_count = 4;
  for (int i = 0; i < 8; i++) { v.top_sources[i].hash = i; v.top_sources[i].drops = 65535; }
  v.top_count = 8;
  for (int i = 0; i < 3; i++) { v.hash_top_types[i].type = i; v.hash_top_types[i].drops = 123456; }
  v.hash_top_count = 3;
  v.advert_window_h = 720;
  v.advert_cache = 256;
  v.advert_cache_size = 256;
  static const char* prefixes[] = { "A1B2C3D4", "B2C3D4E5", "C3D4E5F6", "D4E5F6A7", "E5F6A7B8", "F6A7B8C9", "A7B8C9D0", "B8C9D0E1" };
  for (int i = 0; i < 8; i++) { v.paths[i].prefix = prefixes[i]; v.paths[i].drops = 123456; }
  v.path_count = 8;
  v.sender_total = 999999;
  v.text_total = 999999;
  static const char* snames[] = { "SpamBot12345678", "Bot*", "Alice", "Bob", "Carol", "Dave", "Erin", "Frank" };
  static const char* tpats[] = { "^BEACON", "RX_in_place_report_1234", "spam", "buy", "sell", "crypto", "^ID:", "test" };
  for (int i = 0; i < 8; i++) {
    v.senders[i] = { snames[i], 65535, 50, 123456, 123456 };
    v.texts[i] = { tpats[i], 65535, 50, 123456, 123456 };
  }
  v.sender_count = 8;
  v.text_count = 8;
  static const char* wnames[] = { "#wardriving-long-name-here", "#memes", "#weather", "#local" };
  for (int i = 0; i < 4; i++) v.watch[i] = wnames[i];
  v.watch_count = 4;
  v.dc_gate_enabled = true;
  v.dc_gate_duty = 100; v.dc_gate_level = 4; v.dc_gate_max_level = 4;
  v.dc_gate_threshold = 70; v.dc_gate_hysteresis = 10;

  size_t len = 0;
  render(v, &len);

  EXPECT_LT(len, PUBLISH_BUFFER) << "payload of " << len << " bytes would be dropped by the bridge";
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
