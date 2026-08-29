#include <gtest/gtest.h>

#include <cstring>

#define WITH_MQTT_BRIDGE 1
#include "helpers/MQTTPrefsStorage.h"

// `filter` and `config` are DMC extension topics. Publishing them at a community
// broker wastes airtime and broker budget, and `config` discloses the node's
// settings to an operator who never asked for them. The per-slot mask decides;
// mqttDefaultExtrasForPreset is what makes a newly-assigned slot default sanely.

TEST(MqttSlotExtras, DmcCollectorsGetTheExtensionTopics) {
  EXPECT_EQ(mqttDefaultExtrasForPreset("dutchmeshcore-1"), MQTT_SLOT_EXTRAS_ALL);
  EXPECT_EQ(mqttDefaultExtrasForPreset("dutchmeshcore-2"), MQTT_SLOT_EXTRAS_ALL);
}

TEST(MqttSlotExtras, CustomBrokerGetsThemBecauseTheOperatorOwnsIt) {
  EXPECT_EQ(mqttDefaultExtrasForPreset("custom"), MQTT_SLOT_EXTRAS_ALL);
}

TEST(MqttSlotExtras, CommunityBrokersDoNot) {
  // The regression this exists to prevent: a DMC node feeding a public analyzer
  // must not push DMC-only topics at it.
  const char* community[] = {
    "analyzer-us", "analyzer-eu", "meshmapper", "meshrank", "waev",
    "cascadiamesh", "tennmesh", "ntxmesh", "meshcore-fi", "okimesh-1",
  };
  for (const char* p : community) {
    EXPECT_EQ(mqttDefaultExtrasForPreset(p), MQTT_SLOT_EXTRAS_NONE) << "preset: " << p;
  }
}

TEST(MqttSlotExtras, NoneAndEmptyAreSafe) {
  EXPECT_EQ(mqttDefaultExtrasForPreset("none"), MQTT_SLOT_EXTRAS_NONE);
  EXPECT_EQ(mqttDefaultExtrasForPreset(""), MQTT_SLOT_EXTRAS_NONE);
  EXPECT_EQ(mqttDefaultExtrasForPreset(nullptr), MQTT_SLOT_EXTRAS_NONE);
}

TEST(MqttSlotExtras, PrefixMatchDoesNotOverreach) {
  // "dutchmeshcore" is matched as a prefix so future collectors are covered
  // without a code change, but an unrelated name starting similarly must not be.
  EXPECT_EQ(mqttDefaultExtrasForPreset("dutchmeshcore-9"), MQTT_SLOT_EXTRAS_ALL);
  EXPECT_EQ(mqttDefaultExtrasForPreset("dutchmesh"), MQTT_SLOT_EXTRAS_NONE);
}

TEST(MqttSlotExtras, BitsAreIndependent) {
  EXPECT_EQ(MQTT_SLOT_EXTRAS_ALL, MQTT_SLOT_EXTRA_FILTER | MQTT_SLOT_EXTRA_CONFIG);
  EXPECT_NE(MQTT_SLOT_EXTRA_FILTER, MQTT_SLOT_EXTRA_CONFIG);
  EXPECT_EQ(MQTT_SLOT_EXTRAS_NONE, 0);
}

// The derivation must also be what an upgrading node lands on. These lock the
// three load paths' shared expectation: a community slot ends up with nothing,
// a DMC slot with both, without anyone touching the CLI.
TEST(MqttSlotExtras, UpgradeDerivationStopsTheLeakWithoutOperatorAction) {
  // A realistic DMC fleet layout: two DMC collectors plus a community analyzer.
  const char* slots[] = { "dutchmeshcore-1", "dutchmeshcore-2",
                          "meshcore-analyzer-eu", "none", "none", "none" };
  uint8_t derived[6];
  for (int i = 0; i < 6; i++) derived[i] = mqttDefaultExtrasForPreset(slots[i]);

  EXPECT_EQ(derived[0], MQTT_SLOT_EXTRAS_ALL);
  EXPECT_EQ(derived[1], MQTT_SLOT_EXTRAS_ALL);
  // The one that matters: the community slot must not inherit DMC topics.
  EXPECT_EQ(derived[2], MQTT_SLOT_EXTRAS_NONE);
  EXPECT_EQ(derived[3], MQTT_SLOT_EXTRAS_NONE);
}

// applyMQTTDefaults() is deliberately NOT unit-tested here: MQTTDefaults.h pulls
// in CommonCLI/Mesh/MQTTPresets, which are not host-buildable (PROGMEM, Arduino
// types). Its loop is a direct call to the pure function above, and the wiring is
// covered by the firmware build plus TESTING.md section 4.25.

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
