#ifdef WITH_SNMP

#include "SNMPAgent.h"
#include "NetworkLink.h"
#include <esp_heap_caps.h>
#include <climits>
#include <cmath>

#define SNMP_PORT 161

MeshSNMPAgent::MeshSNMPAgent()
  : _snmp("public"),
    _running(false),
    _repeater_groups(false),
    _uptime_secs(0),
    _battery_mv(0),
    _mcu_temp_c(SNMP_TEMP_UNAVAILABLE),
    _reset_reason(0),
    _packets_recv(0), _packets_sent(0), _recv_errors(0),
    _noise_floor(0), _last_rssi(0), _last_snr(0),
    _sent_flood(0), _sent_direct(0), _recv_flood(0), _recv_direct(0),
    _total_air_time_secs(0),
    _tx_queue_len(0), _flood_dups(0), _direct_dups(0), _rx_air_time_secs(0),
    _tx_duty_percent(0), _err_flags(0),
    _mqtt_connected_slots(0), _mqtt_queue_depth(0), _mqtt_skipped_publishes(0), _ntp_synced(0),
    _free_heap(0), _max_alloc(0), _internal_free(0), _psram_free(0), _min_free_heap(0),
    _wifi_rssi(-127),
    _dc_gate_enabled(0), _dc_gate_level(0), _dc_gate_max_level(0),
    _dc_gate_threshold(0), _dc_gate_hysteresis(0),
    _filter_enabled(0), _filter_dryrun(0),
    _filter_hops(0), _filter_rate(0), _filter_channel(0), _filter_hash(0),
    _filter_malformed(0), _filter_advert(0), _filter_path(0), _filter_sender(0),
    _filter_text(0), _filter_age(0), _filter_air_ms(0),
    _neighbour_count(0)
{
  _firmware_version[0] = '\0';
  _node_name[0] = '\0';
  _reset_reason_text[0] = '\0';
}

void MeshSNMPAgent::begin(const char* community) {
  if (_running) return;

  _snmp = SNMPAgent(community);
  _snmp.setUDP(&_udp);
  _snmp.begin();

  // System group (.1.x.0)
  _snmp.addIntegerHandler(MESHCORE_OID_BASE ".1.1.0", &_uptime_secs);
  _snmp.addReadOnlyStaticStringHandler(MESHCORE_OID_BASE ".1.2.0", _firmware_version, sizeof(_firmware_version));
  _snmp.addReadOnlyStaticStringHandler(MESHCORE_OID_BASE ".1.3.0", _node_name, sizeof(_node_name));
  _snmp.addIntegerHandler(MESHCORE_OID_BASE ".1.4.0", &_battery_mv);
  _snmp.addIntegerHandler(MESHCORE_OID_BASE ".1.5.0", &_mcu_temp_c);
  _snmp.addIntegerHandler(MESHCORE_OID_BASE ".1.6.0", &_reset_reason);
  _snmp.addReadOnlyStaticStringHandler(MESHCORE_OID_BASE ".1.7.0", _reset_reason_text);

  // Radio group (.2.x.0)
  _snmp.addIntegerHandler(MESHCORE_OID_BASE ".2.1.0", &_packets_recv);
  _snmp.addIntegerHandler(MESHCORE_OID_BASE ".2.2.0", &_packets_sent);
  _snmp.addIntegerHandler(MESHCORE_OID_BASE ".2.3.0", &_recv_errors);
  _snmp.addIntegerHandler(MESHCORE_OID_BASE ".2.4.0", &_noise_floor);
  _snmp.addIntegerHandler(MESHCORE_OID_BASE ".2.5.0", &_last_rssi);
  _snmp.addIntegerHandler(MESHCORE_OID_BASE ".2.6.0", &_last_snr);
  _snmp.addIntegerHandler(MESHCORE_OID_BASE ".2.7.0", &_sent_flood);
  _snmp.addIntegerHandler(MESHCORE_OID_BASE ".2.8.0", &_sent_direct);
  _snmp.addIntegerHandler(MESHCORE_OID_BASE ".2.9.0", &_recv_flood);
  _snmp.addIntegerHandler(MESHCORE_OID_BASE ".2.10.0", &_recv_direct);
  _snmp.addIntegerHandler(MESHCORE_OID_BASE ".2.11.0", &_total_air_time_secs);
  _snmp.addIntegerHandler(MESHCORE_OID_BASE ".2.12.0", &_tx_queue_len);
  _snmp.addCounter32Handler(MESHCORE_OID_BASE ".2.13.0", &_flood_dups);
  _snmp.addCounter32Handler(MESHCORE_OID_BASE ".2.14.0", &_direct_dups);
  _snmp.addCounter32Handler(MESHCORE_OID_BASE ".2.15.0", &_rx_air_time_secs);
  _snmp.addIntegerHandler(MESHCORE_OID_BASE ".2.16.0", &_tx_duty_percent);
  _snmp.addIntegerHandler(MESHCORE_OID_BASE ".2.17.0", &_err_flags);

  // MQTT group (.3.x.0)
  _snmp.addIntegerHandler(MESHCORE_OID_BASE ".3.1.0", &_mqtt_connected_slots);
  _snmp.addIntegerHandler(MESHCORE_OID_BASE ".3.2.0", &_mqtt_queue_depth);
  _snmp.addIntegerHandler(MESHCORE_OID_BASE ".3.3.0", &_mqtt_skipped_publishes);
  _snmp.addIntegerHandler(MESHCORE_OID_BASE ".3.4.0", &_ntp_synced);

  // Memory group (.4.x.0)
  _snmp.addIntegerHandler(MESHCORE_OID_BASE ".4.1.0", &_free_heap);
  _snmp.addIntegerHandler(MESHCORE_OID_BASE ".4.2.0", &_max_alloc);
  _snmp.addIntegerHandler(MESHCORE_OID_BASE ".4.3.0", &_internal_free);
  _snmp.addIntegerHandler(MESHCORE_OID_BASE ".4.4.0", &_psram_free);
  _snmp.addIntegerHandler(MESHCORE_OID_BASE ".4.5.0", &_min_free_heap);

  // Network group (.5.x.0)
  _snmp.addIntegerHandler(MESHCORE_OID_BASE ".5.1.0", &_wifi_rssi);

  if (_repeater_groups) {
    // Duty gate group (.6.x.0)
    _snmp.addIntegerHandler(MESHCORE_OID_BASE ".6.1.0", &_dc_gate_enabled);
    _snmp.addIntegerHandler(MESHCORE_OID_BASE ".6.2.0", &_dc_gate_level);
    _snmp.addIntegerHandler(MESHCORE_OID_BASE ".6.3.0", &_dc_gate_max_level);
    _snmp.addIntegerHandler(MESHCORE_OID_BASE ".6.4.0", &_dc_gate_threshold);
    _snmp.addIntegerHandler(MESHCORE_OID_BASE ".6.5.0", &_dc_gate_hysteresis);

    // Filter group (.7.x.0)
    _snmp.addIntegerHandler(MESHCORE_OID_BASE ".7.1.0", &_filter_enabled);
    _snmp.addIntegerHandler(MESHCORE_OID_BASE ".7.2.0", &_filter_dryrun);
    _snmp.addCounter32Handler(MESHCORE_OID_BASE ".7.3.0", &_filter_hops);
    _snmp.addCounter32Handler(MESHCORE_OID_BASE ".7.4.0", &_filter_rate);
    _snmp.addCounter32Handler(MESHCORE_OID_BASE ".7.5.0", &_filter_channel);
    _snmp.addCounter32Handler(MESHCORE_OID_BASE ".7.6.0", &_filter_hash);
    _snmp.addCounter32Handler(MESHCORE_OID_BASE ".7.7.0", &_filter_malformed);
    _snmp.addCounter32Handler(MESHCORE_OID_BASE ".7.8.0", &_filter_advert);
    _snmp.addCounter32Handler(MESHCORE_OID_BASE ".7.9.0", &_filter_path);
    _snmp.addCounter32Handler(MESHCORE_OID_BASE ".7.10.0", &_filter_sender);
    _snmp.addCounter32Handler(MESHCORE_OID_BASE ".7.11.0", &_filter_text);
    _snmp.addCounter32Handler(MESHCORE_OID_BASE ".7.12.0", &_filter_age);
    _snmp.addCounter32Handler(MESHCORE_OID_BASE ".7.13.0", &_filter_air_ms);
  }

  // Neighbour group (.8.x.0)
  _snmp.addIntegerHandler(MESHCORE_OID_BASE ".8.1.0", &_neighbour_count);

  _snmp.sortHandlers();
  _running = true;
}

void MeshSNMPAgent::loop() {
  if (!_running) return;

  // Update memory and selected-network stats locally on Core 0.
  _free_heap = (int)ESP.getFreeHeap();
  _max_alloc = (int)ESP.getMaxAllocHeap();
  _internal_free = (int)heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
#ifdef BOARD_HAS_PSRAM
  _psram_free = (int)heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
#else
  _psram_free = 0;
#endif
  _min_free_heap = (int)ESP.getMinFreeHeap();

  const int signal = activeNetworkLink().rssi();
  _wifi_rssi = signal == INT_MIN ? -127 : signal;

  _snmp.loop();
}

void MeshSNMPAgent::updateRadioStats(
    uint32_t packets_recv, uint32_t packets_sent, uint32_t recv_errors,
    int16_t noise_floor, int16_t last_rssi, int16_t last_snr,
    uint32_t sent_flood, uint32_t sent_direct,
    uint32_t recv_flood, uint32_t recv_direct,
    uint32_t total_air_time_secs, uint32_t uptime_secs) {
  _packets_recv = (int)packets_recv;
  _packets_sent = (int)packets_sent;
  _recv_errors = (int)recv_errors;
  _noise_floor = (int)noise_floor;
  _last_rssi = (int)last_rssi;
  _last_snr = (int)last_snr;
  _sent_flood = (int)sent_flood;
  _sent_direct = (int)sent_direct;
  _recv_flood = (int)recv_flood;
  _recv_direct = (int)recv_direct;
  _total_air_time_secs = (int)total_air_time_secs;
  _uptime_secs = (int)uptime_secs;
}

void MeshSNMPAgent::updateMeshStats(uint32_t tx_queue_len, uint32_t flood_dups, uint32_t direct_dups,
                                    uint32_t rx_air_time_secs, uint8_t tx_duty_percent, uint16_t err_flags,
                                    int neighbour_count) {
  _tx_queue_len = (int)tx_queue_len;
  _flood_dups = flood_dups;
  _direct_dups = direct_dups;
  _rx_air_time_secs = rx_air_time_secs;
  _tx_duty_percent = (int)tx_duty_percent;
  _err_flags = (int)err_flags;
  _neighbour_count = neighbour_count;
}

void MeshSNMPAgent::updateMQTTStats(int connected_slots, int queue_depth, int skipped_publishes, bool ntp_synced) {
  _mqtt_connected_slots = connected_slots;
  _mqtt_queue_depth = queue_depth;
  _mqtt_skipped_publishes = skipped_publishes;
  _ntp_synced = ntp_synced ? 1 : 0;
}

void MeshSNMPAgent::updateBattery(uint16_t battery_mv) {
  _battery_mv = (int)battery_mv;
}

void MeshSNMPAgent::updateMcuTemperature(float celsius) {
  _mcu_temp_c = std::isnan(celsius) ? SNMP_TEMP_UNAVAILABLE : (int)lroundf(celsius);
}

void MeshSNMPAgent::updateDutyGate(bool enabled, uint8_t level, uint8_t max_level, uint8_t threshold, uint8_t hysteresis) {
  _dc_gate_enabled = enabled ? 1 : 0;
  _dc_gate_level = level;
  _dc_gate_max_level = max_level;
  _dc_gate_threshold = threshold;
  _dc_gate_hysteresis = hysteresis;
}

void MeshSNMPAgent::updateFilterStats(const SNMPFilterTotals& t) {
  _filter_enabled = t.enabled ? 1 : 0;
  _filter_dryrun = t.dryrun ? 1 : 0;
  _filter_hops = t.hops;
  _filter_rate = t.rate;
  _filter_channel = t.channel;
  _filter_hash = t.hash;
  _filter_malformed = t.malformed;
  _filter_advert = t.advert;
  _filter_path = t.path;
  _filter_sender = t.sender;
  _filter_text = t.text;
  _filter_age = t.age;
  _filter_air_ms = t.air_ms;
}

void MeshSNMPAgent::setNodeName(const char* name) {
  strncpy(_node_name, name, sizeof(_node_name) - 1);
  _node_name[sizeof(_node_name) - 1] = '\0';
}

void MeshSNMPAgent::setFirmwareVersion(const char* version) {
  strncpy(_firmware_version, version, sizeof(_firmware_version) - 1);
  _firmware_version[sizeof(_firmware_version) - 1] = '\0';
}

void MeshSNMPAgent::setResetReason(uint32_t code, const char* text) {
  _reset_reason = (int)code;
  strncpy(_reset_reason_text, text ? text : "", sizeof(_reset_reason_text) - 1);
  _reset_reason_text[sizeof(_reset_reason_text) - 1] = '\0';
}

#endif // WITH_SNMP
