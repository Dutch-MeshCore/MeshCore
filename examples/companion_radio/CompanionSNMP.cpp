// DMC companion SNMP agent: read-only SNMPv2c (GET / GETNEXT / GETBULK) on UDP 161.
// Ported from meshcomod (Elektr0Vodka/meshcomod CompanionSNMP.cpp).
//
// Off by default: until `set snmp on` the agent is never allocated, so a build
// that leaves it off costs flash but no RAM. When on, it runs in the main loop
// whenever Wi-Fi holds an address and stops while the link is down.
//
// The OID tree follows the DMC observer firmware (dmc-observer-dev, MQTT_SNMP.md)
// so one poller template reads both: shared numbers keep the observer's meaning,
// type and unit, and an OID the companion has no data for is not registered, so it
// reads noSuchObject instead of a 0 that looks real. Companion-only values use .9
// and up, the same numbers as meshcomod's companion.

#include "CompanionNet.h"

#ifdef DMC_COMPANION_SNMP

#include "MyMesh.h"
#include <WiFi.h>
#include <WiFiUdp.h>
#include <SNMP_Agent.h>
#include <esp_heap_caps.h>
#include <math.h>
#include <string>

#ifdef ENABLE_WIFI_INTERFACE
  #include <helpers/wifi/SerialWifiInterface.h>
  extern SerialWifiInterface wifi_interface;
#endif

// Temporary, unregistered enterprise number, the same one the observer firmware uses.
#define MESHCORE_OID_BASE ".1.3.6.1.4.1.99999"

// Same "no value" marker as the observer for a board without an MCU temperature sensor.
#define SNMP_TEMP_UNAVAILABLE (-127)

extern SimpleMeshTables tables;

static MyMesh* s_mesh = nullptr;

// Agent and socket are created on first start and kept, so toggling never leaks.
static WiFiUDP* s_udp = nullptr;
static SNMPAgent* s_agent = nullptr;
static bool s_running = false;

// Values the agent serves from a copy instead of reading them per request.
// Hardware (battery ADC, die temperature) is read every 30 s: some battery reads
// block for 10 ms and neither value moves quickly. Counter32 handlers need a
// variable to point at, so those totals are copied every 2 s. All of it happens in
// companionSnmpLoop(), on the main loop with the mesh, never inside a request.
static int s_battery_mv = 0;
static int s_mcu_temp_c = SNMP_TEMP_UNAVAILABLE;
static int s_reset_reason = 0;
static char s_reset_reason_text[48] = "";
static uint32_t s_flood_dups = 0;
static uint32_t s_direct_dups = 0;
static uint32_t s_rx_air_secs = 0;
static uint32_t s_cf_malformed = 0;
static uint32_t s_cf_age = 0;
static uint32_t s_cf_path = 0;
static uint32_t s_cf_sender = 0;
static uint32_t s_cf_text = 0;
static uint32_t s_last_stats_ms = 0;
static uint32_t s_last_health_ms = 0;
static bool s_health_read = false;

static void readHealth() {
  s_battery_mv = (int)board.getBattMilliVolts();
  const float t = board.getMCUTemperature();
  s_mcu_temp_c = isnan(t) ? SNMP_TEMP_UNAVAILABLE : (int)lroundf(t);
}

static void copyCounters() {
  s_flood_dups = tables.getNumFloodDups();
  s_direct_dups = tables.getNumDirectDups();
  s_rx_air_secs = (uint32_t)(s_mesh->getReceiveAirTime() / 1000);
  const Counters& c = s_mesh->dmcFilterCounters();
  s_cf_malformed = c.malformed;
  s_cf_age = c.age;
  s_cf_path = c.path;
  s_cf_sender = c.sender;
  s_cf_text = c.text;
}

static void registerHandlers(SNMPAgent* a) {
  // System (.1.x.0)
  a->addDynamicIntegerHandler(MESHCORE_OID_BASE ".1.1.0", []() -> int { return (int)(millis() / 1000); });
  a->addDynamicReadOnlyStringHandler(MESHCORE_OID_BASE ".1.2.0", []() -> const std::string { return FIRMWARE_VERSION; });
  a->addDynamicReadOnlyStringHandler(MESHCORE_OID_BASE ".1.3.0", []() -> const std::string {
    return std::string(s_mesh->getNodePrefs()->node_name);
  });
  a->addIntegerHandler(MESHCORE_OID_BASE ".1.4.0", &s_battery_mv);
  a->addIntegerHandler(MESHCORE_OID_BASE ".1.5.0", &s_mcu_temp_c);
  a->addIntegerHandler(MESHCORE_OID_BASE ".1.6.0", &s_reset_reason);
  a->addDynamicReadOnlyStringHandler(MESHCORE_OID_BASE ".1.7.0", []() -> const std::string {
    return std::string(s_reset_reason_text);
  });

  // Radio (.2.x.0). .2.16 (TX duty cycle) is left out: this firmware does not track it.
  a->addDynamicIntegerHandler(MESHCORE_OID_BASE ".2.1.0", []() -> int { return (int)radio_driver.getPacketsRecv(); });
  a->addDynamicIntegerHandler(MESHCORE_OID_BASE ".2.2.0", []() -> int { return (int)radio_driver.getPacketsSent(); });
  a->addDynamicIntegerHandler(MESHCORE_OID_BASE ".2.3.0", []() -> int { return (int)radio_driver.getPacketsRecvErrors(); });
  a->addDynamicIntegerHandler(MESHCORE_OID_BASE ".2.4.0", []() -> int { return radio_driver.getNoiseFloor(); });
  a->addDynamicIntegerHandler(MESHCORE_OID_BASE ".2.5.0", []() -> int { return (int)radio_driver.getLastRSSI(); });
  a->addDynamicIntegerHandler(MESHCORE_OID_BASE ".2.6.0", []() -> int { return (int)(radio_driver.getLastSNR() * 4); });
  a->addDynamicIntegerHandler(MESHCORE_OID_BASE ".2.7.0", []() -> int { return (int)s_mesh->getNumSentFlood(); });
  a->addDynamicIntegerHandler(MESHCORE_OID_BASE ".2.8.0", []() -> int { return (int)s_mesh->getNumSentDirect(); });
  a->addDynamicIntegerHandler(MESHCORE_OID_BASE ".2.9.0", []() -> int { return (int)s_mesh->getNumRecvFlood(); });
  a->addDynamicIntegerHandler(MESHCORE_OID_BASE ".2.10.0", []() -> int { return (int)s_mesh->getNumRecvDirect(); });
  a->addDynamicIntegerHandler(MESHCORE_OID_BASE ".2.11.0", []() -> int { return (int)(s_mesh->getTotalAirTime() / 1000); });
  a->addDynamicIntegerHandler(MESHCORE_OID_BASE ".2.12.0", []() -> int { return s_mesh->dmcTxQueueLen(); });
  a->addCounter32Handler(MESHCORE_OID_BASE ".2.13.0", &s_flood_dups);
  a->addCounter32Handler(MESHCORE_OID_BASE ".2.14.0", &s_direct_dups);
  a->addCounter32Handler(MESHCORE_OID_BASE ".2.15.0", &s_rx_air_secs);
  // Bits 1, 2 and 4 as on the observer. Bit 8 (radio watchdog) does not exist here and stays 0.
  a->addDynamicIntegerHandler(MESHCORE_OID_BASE ".2.17.0", []() -> int { return (int)s_mesh->dmcErrFlags(); });

  // MQTT (.3.x.0): no bridge and no NTP on the companion, so nothing is registered.

  // Memory (.4.x.0). heap_caps reports 0 for PSRAM on boards without it.
  a->addDynamicIntegerHandler(MESHCORE_OID_BASE ".4.1.0", []() -> int { return (int)ESP.getFreeHeap(); });
  a->addDynamicIntegerHandler(MESHCORE_OID_BASE ".4.2.0", []() -> int { return (int)ESP.getMaxAllocHeap(); });
  a->addDynamicIntegerHandler(MESHCORE_OID_BASE ".4.3.0", []() -> int { return (int)heap_caps_get_free_size(MALLOC_CAP_INTERNAL); });
  a->addDynamicIntegerHandler(MESHCORE_OID_BASE ".4.4.0", []() -> int { return (int)heap_caps_get_free_size(MALLOC_CAP_SPIRAM); });
  a->addDynamicIntegerHandler(MESHCORE_OID_BASE ".4.5.0", []() -> int { return (int)ESP.getMinFreeHeap(); });

  // Network (.5.x.0)
  a->addDynamicIntegerHandler(MESHCORE_OID_BASE ".5.1.0", []() -> int {
    return WiFi.status() == WL_CONNECTED ? (int)WiFi.RSSI() : -127;
  });

  // .6 duty gate, .7 packet filter and .8 neighbours are repeater features: not registered.

  // Companion (.9.x.0), companion only. .9.2 (WebSocket clients) is meshcomod-only.
#ifdef ENABLE_WIFI_INTERFACE
  a->addDynamicIntegerHandler(MESHCORE_OID_BASE ".9.1.0", []() -> int { return wifi_interface.isConnected() ? 1 : 0; });
#endif
  a->addDynamicIntegerHandler(MESHCORE_OID_BASE ".9.3.0", []() -> int { return s_mesh->getNumContacts(); });
  a->addDynamicIntegerHandler(MESHCORE_OID_BASE ".9.4.0", []() -> int { return s_mesh->dmcNumChannels(); });

  // Receive filter (.10.x.0), companion only. Not the repeater's .7: these count messages
  // kept from the user (dropped as junk or hidden by taste), not packets kept off the air.
  a->addDynamicIntegerHandler(MESHCORE_OID_BASE ".10.1.0", []() -> int { return s_mesh->dmcFilterEnabled() ? 1 : 0; });
  a->addCounter32Handler(MESHCORE_OID_BASE ".10.2.0", &s_cf_malformed);
  a->addCounter32Handler(MESHCORE_OID_BASE ".10.3.0", &s_cf_age);
  a->addCounter32Handler(MESHCORE_OID_BASE ".10.4.0", &s_cf_path);
  a->addCounter32Handler(MESHCORE_OID_BASE ".10.5.0", &s_cf_sender);
  a->addCounter32Handler(MESHCORE_OID_BASE ".10.6.0", &s_cf_text);
}

static void startAgent(const char* community) {
  if (!s_agent) {
    const uint32_t reason = board.getResetReason();
    s_reset_reason = (int)reason;
    StrHelper::strncpy(s_reset_reason_text, board.getResetReasonString(reason), sizeof(s_reset_reason_text));

    s_udp = new WiFiUDP();
    s_agent = new SNMPAgent(community);
    registerHandlers(s_agent);
    s_agent->sortHandlers();
    s_agent->setUDP(s_udp);   // registers the socket and binds UDP 161
  } else {
    s_agent->setReadWriteCommunity(community);
    s_agent->restartUDP();    // the old socket died with the link
  }
  s_running = true;
  Serial.printf("[snmp] agent up on udp/161 (free=%u)\n", (unsigned)ESP.getFreeHeap());
}

static void stopAgent() {
  s_agent->stop();
  s_running = false;
  Serial.println("[snmp] agent stopped");
}

void companionSnmpLoop(MyMesh& mesh, CompanionNetPrefs& prefs, bool link_up) {
  s_mesh = &mesh;
  const bool want = prefs.snmp_on && link_up;
  if (want && !s_running) {
    startAgent(prefs.snmp_community);
  } else if (!want && s_running) {
    stopAgent();
  }
  if (!s_running) return;

  const uint32_t now = millis();
  if (!s_health_read || now - s_last_health_ms >= 30000) {
    s_health_read = true;
    s_last_health_ms = now;
    readHealth();
  }
  if (s_last_stats_ms == 0 || now - s_last_stats_ms >= 2000) {
    s_last_stats_ms = now;
    copyCounters();
  }
  s_agent->loop();
}

bool companionSnmpRunning() {
  return s_running;
}

void companionSnmpSetCommunity(const char* community) {
  if (s_agent) s_agent->setReadWriteCommunity(community);
}

#endif  // DMC_COMPANION_SNMP
