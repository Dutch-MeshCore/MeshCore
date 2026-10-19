#pragma once

#include <Arduino.h>
#include <helpers/ConfigSerializer.h>

// DMC companion network extras, for builds that can join a Wi-Fi network:
//   - SNMP agent (read-only SNMPv2c on UDP 161), off until `set snmp on`
//   - pull OTA from the DutchMeshCore OTA server (`ota check|update|status|branch`)
//   - `get mac`, `get wifi.mac`, `get ble.mac` (every ESP32 build)
// Ported from meshcomod (Elektr0Vodka/meshcomod) and the DMC observer firmware.
// Settings persist as JSON in CNET_PREFS_FILE, apart from NodePrefs.

#if defined(ESP32) && defined(ENABLE_WIFI_INTERFACE)
  #define DMC_COMPANION_WIFI 1
#endif

// build.sh bakes the server bases and the variant (the PlatformIO env name) in.
// Boards with 4 MB flash set DMC_COMPANION_NO_OTA: the TLS stack the download
// needs (~180 KB) leaves them no headroom.
#if defined(DMC_COMPANION_WIFI) && !defined(DMC_COMPANION_NO_OTA) && \
    defined(COMPANION_OTA_VARIANT) && defined(COMPANION_OTA_BASE) && \
    defined(COMPANION_OTA_BASE_STABLE) && defined(COMPANION_OTA_BASE_DEV)
  #define DMC_COMPANION_OTA 1
#endif

// SNMP needs the 0neblock/SNMP_Agent library in the env's lib_deps.
#if defined(DMC_COMPANION_WIFI) && __has_include(<SNMP_Agent.h>)
  #define DMC_COMPANION_SNMP 1
#endif

#define CNET_PREFS_FILE "/cnet.json"

// Same budget as the companion filter: remote CLI over the mesh is the tightest.
#define CNET_REPLY_SIZE 158

#define CNET_SNMP_COMMUNITY_MAX 33   // incl. the terminator

class CompanionNetPrefs : public ConfigSerializer {
public:
  bool snmp_on = false;
  char snmp_community[CNET_SNMP_COMMUNITY_MAX] = "public";
  uint8_t ota_channel = 0;   // OTA_CHANNEL_NATIVE / _STABLE / _DEV (helpers/OtaManifest.h)

  void reset();
  void sanitize();

protected:
  void structure() override {
    def("snmp", snmp_on);
    def("snmp_comm", snmp_community, sizeof(snmp_community));
    def("ota_ch", ota_channel);
  }
};

class DataStore;
bool loadCompanionNet(CompanionNetPrefs& prefs, DataStore& store);
bool saveCompanionNet(CompanionNetPrefs& prefs, DataStore& store);

// MAC address replies; true when the command was one of them. Every platform.
bool companionMacCommand(const char* command, char* reply);

#ifdef DMC_COMPANION_OTA
// One OTA run against base. dry_run = `ota check` (plain HTTP manifest, report
// only); otherwise a TLS-verified manifest fetch and, when an update applies, the
// flash and a reboot. Returns true when an installable update exists (check) or
// false with the reason in reply (update returns only on failure).
bool companionOtaRun(const char* base, bool dry_run, char* reply);
#endif

#ifdef DMC_COMPANION_SNMP
class MyMesh;
// Main loop. link_up: Wi-Fi associated with an address.
void companionSnmpLoop(MyMesh& mesh, CompanionNetPrefs& prefs, bool link_up);
// `get snmp.status`.
bool companionSnmpRunning();
// The community changed: applies to the next request.
void companionSnmpSetCommunity(const char* community);
#endif
