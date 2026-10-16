#pragma once

#ifdef WITH_SNMP

#include <WiFiUdp.h>
#include <SNMP_Agent.h>

// Temporary private enterprise OID base — replace with registered PEN when available.
// All MeshCore OIDs live under this subtree.
#define MESHCORE_OID_BASE ".1.3.6.1.4.1.99999"

// OID layout:
//   .1.x.0  = system      (uptime, version, node name, battery, MCU temperature, reset reason)
//   .2.x.0  = radio       (packets, RSSI, SNR, noise floor, air time, TX queue, dups, duty cycle)
//   .3.x.0  = mqtt        (connected slots, queue depth, skipped publishes, NTP synced)
//   .4.x.0  = memory      (free heap, max alloc, internal free, PSRAM free, min free heap)
//   .5.x.0  = network     (RSSI, or -127 when disconnected or the medium has no RSSI)
//   .6.x.0  = duty gate   (region gating state; repeater only)
//   .7.x.0  = filter      (packet filter drop totals; repeater only)
//   .8.x.0  = neighbours  (neighbour count)
//
// Levels are INTEGER. Running totals added after the first release are Counter32, so
// pollers graph them as rates.

// MCU temperature value when the board has no sensor (or returned NaN).
#define SNMP_TEMP_UNAVAILABLE -127

// Packet filter drop totals, as pushed from the repeater's Filter counters.
struct SNMPFilterTotals {
  bool enabled = false;
  bool dryrun = false;
  uint32_t hops = 0;       // hop-limit drops, all payload types
  uint32_t rate = 0;       // rate-limit drops, all payload types
  uint32_t channel = 0;
  uint32_t hash = 0;
  uint32_t malformed = 0;
  uint32_t advert = 0;
  uint32_t path = 0;
  uint32_t sender = 0;
  uint32_t text = 0;
  uint32_t age = 0;
  uint32_t air_ms = 0;     // estimated TX airtime the drops saved
};

class MeshSNMPAgent {
public:
  MeshSNMPAgent();
  void begin(const char* community);
  void loop();

  // Called from the mesh task (Core 1) to push fresh stats into SNMP-visible variables.
  // Copies are atomic for 32-bit aligned ints on ESP32, so no mutex needed.
  void updateRadioStats(uint32_t packets_recv, uint32_t packets_sent, uint32_t recv_errors,
                        int16_t noise_floor, int16_t last_rssi, int16_t last_snr,
                        uint32_t sent_flood, uint32_t sent_direct,
                        uint32_t recv_flood, uint32_t recv_direct,
                        uint32_t total_air_time_secs, uint32_t uptime_secs);

  // Mesh-side counters that RepeaterStats also reports. Called from the mesh task (Core 1).
  void updateMeshStats(uint32_t tx_queue_len, uint32_t flood_dups, uint32_t direct_dups,
                       uint32_t rx_air_time_secs, uint8_t tx_duty_percent, uint16_t err_flags,
                       int neighbour_count);

  // Called from the MQTT task (Core 0).
  void updateMQTTStats(int connected_slots, int queue_depth, int skipped_publishes, bool ntp_synced);

  // Battery voltage in millivolts, 0 when the board cannot measure it. Pushed from the
  // mesh task, so the ADC is only ever read from Core 1.
  void updateBattery(uint16_t battery_mv);

  // MCU die temperature in whole degrees C. NaN maps to SNMP_TEMP_UNAVAILABLE.
  void updateMcuTemperature(float celsius);

  // Repeater-only groups. Registered by begin() only after enableRepeaterGroups().
  void updateDutyGate(bool enabled, uint8_t level, uint8_t max_level, uint8_t threshold, uint8_t hysteresis);
  void updateFilterStats(const SNMPFilterTotals& totals);
  void enableRepeaterGroups() { _repeater_groups = true; }

  // String OIDs are copied when begin() registers them, so set these first.
  void setNodeName(const char* name);
  void setFirmwareVersion(const char* version);
  void setResetReason(uint32_t code, const char* text);

  bool isRunning() const { return _running; }

private:
  WiFiUDP _udp;
  SNMPAgent _snmp;
  bool _running;
  bool _repeater_groups;

  // System OIDs
  int _uptime_secs;
  char _firmware_version[32];
  char _node_name[32];
  int _battery_mv;
  int _mcu_temp_c;
  int _reset_reason;
  char _reset_reason_text[32];

  // Radio OIDs
  int _packets_recv;
  int _packets_sent;
  int _recv_errors;
  int _noise_floor;
  int _last_rssi;
  int _last_snr;
  int _sent_flood;
  int _sent_direct;
  int _recv_flood;
  int _recv_direct;
  int _total_air_time_secs;
  int _tx_queue_len;
  uint32_t _flood_dups;
  uint32_t _direct_dups;
  uint32_t _rx_air_time_secs;
  int _tx_duty_percent;
  int _err_flags;

  // MQTT OIDs
  int _mqtt_connected_slots;
  int _mqtt_queue_depth;
  int _mqtt_skipped_publishes;
  int _ntp_synced;

  // Memory OIDs (updated in loop() since we're on Core 0 with WiFi)
  int _free_heap;
  int _max_alloc;
  int _internal_free;
  int _psram_free;
  int _min_free_heap;

  // Network OIDs
  int _wifi_rssi;

  // Duty gate OIDs (repeater only)
  int _dc_gate_enabled;
  int _dc_gate_level;
  int _dc_gate_max_level;
  int _dc_gate_threshold;
  int _dc_gate_hysteresis;

  // Filter OIDs (repeater only)
  int _filter_enabled;
  int _filter_dryrun;
  uint32_t _filter_hops;
  uint32_t _filter_rate;
  uint32_t _filter_channel;
  uint32_t _filter_hash;
  uint32_t _filter_malformed;
  uint32_t _filter_advert;
  uint32_t _filter_path;
  uint32_t _filter_sender;
  uint32_t _filter_text;
  uint32_t _filter_age;
  uint32_t _filter_air_ms;

  // Neighbour OIDs
  int _neighbour_count;
};

#endif // WITH_SNMP
