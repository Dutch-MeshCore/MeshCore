// DMC companion network extras: settings, CLI and the MyMesh side. Lives in its
// own file so the shared upstream MyMesh.{cpp,h} only carry one-line calls into it.

#include "MyMesh.h"
#include <helpers/OtaManifest.h>

#ifdef ESP32
  #if __has_include(<esp_mac.h>)
    #include <esp_mac.h>
  #else
    #include <esp_system.h>
  #endif
#endif
#ifdef DMC_COMPANION_WIFI
  #include <WiFi.h>
#endif

void CompanionNetPrefs::reset() {
  snmp_on = false;
  strcpy(snmp_community, "public");
  ota_channel = OTA_CHANNEL_NATIVE;
}

void CompanionNetPrefs::sanitize() {
  snmp_community[sizeof(snmp_community) - 1] = '\0';
  if (snmp_community[0] == '\0') strcpy(snmp_community, "public");
  if (ota_channel > OTA_CHANNEL_DEV) ota_channel = OTA_CHANNEL_NATIVE;
}

bool loadCompanionNet(CompanionNetPrefs& prefs, DataStore& store) {
  FILESYSTEM* fs = store.getPrimaryFS();
  if (fs == nullptr || !fs->exists(CNET_PREFS_FILE)) return true;   // nothing saved yet: defaults

  File file = store.openRead(fs, CNET_PREFS_FILE);
  if (!file) return false;
  prefs.reset();
  bool ok = prefs.loadSerial(file);
  file.close();
  prefs.sanitize();
  return ok;
}

bool saveCompanionNet(CompanionNetPrefs& prefs, DataStore& store) {
  FILESYSTEM* fs = store.getPrimaryFS();
  if (fs == nullptr) return false;

#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  fs->remove(CNET_PREFS_FILE);
  File file = fs->open(CNET_PREFS_FILE, FILE_O_WRITE);
#elif defined(RP2040_PLATFORM)
  File file = fs->open(CNET_PREFS_FILE, "w");
#else
  File file = fs->open(CNET_PREFS_FILE, "w", true);
#endif
  if (!file) return false;
  bool ok = prefs.saveSerial(file);
  file.close();
  return ok;
}

// ---- MAC addresses ----------------------------------------------------------
// The factory MACs, read from eFuse, so they answer with the radio off too.
// Needed for DHCP reservations and MAC filtering.

#ifdef ESP32
static void formatMac(esp_mac_type_t type, char* out) {
  uint8_t m[6] = {0};
  if (esp_read_mac(m, type) != ESP_OK) {
    strcpy(out, "n/a");
    return;
  }
  sprintf(out, "%02X:%02X:%02X:%02X:%02X:%02X", m[0], m[1], m[2], m[3], m[4], m[5]);
}
#endif

bool companionMacCommand(const char* command, char* reply) {
  bool all = strcmp(command, "get mac") == 0;
  bool wifi = strcmp(command, "get wifi.mac") == 0;
  bool ble = strcmp(command, "get ble.mac") == 0;
  if (!all && !wifi && !ble) return false;
#ifdef ESP32
  char w[18], b[18];
  formatMac(ESP_MAC_WIFI_STA, w);
  formatMac(ESP_MAC_BT, b);
  if (all)       sprintf(reply, "> wifi %s, ble %s", w, b);
  else if (wifi) sprintf(reply, "> %s", w);
  else           sprintf(reply, "> %s", b);
#else
  strcpy(reply, "> n/a");
#endif
  return true;
}

// ---- OTA ---------------------------------------------------------------------

#ifdef DMC_COMPANION_OTA
static bool s_ota_pending = false;          // `ota update` accepted, flash starts from the main loop
static uint32_t s_ota_start_at = 0;
static char s_ota_base[96];
static char s_ota_last[CNET_REPLY_SIZE] = "";

static const char* otaBase(const CompanionNetPrefs& prefs) {
  return ota_channel_base(prefs.ota_channel, COMPANION_OTA_BASE, COMPANION_OTA_BASE_STABLE, COMPANION_OTA_BASE_DEV);
}

static const char* otaChannelName(const char* base) {
  return ota_channel_name(base, COMPANION_OTA_BASE_STABLE, COMPANION_OTA_BASE_DEV);
}
#endif

static bool isOtaCommand(const char* command) {
  return strcmp(command, "ota") == 0 || strncmp(command, "ota ", 4) == 0;
}

static bool otaCommand(CompanionNetPrefs& prefs, DataStore& store, const char* command, char* reply) {
#ifndef DMC_COMPANION_WIFI
  (void)prefs; (void)store; (void)command;
  strcpy(reply, "Error: OTA needs a Wi-Fi build (*_companion_radio_wifi)");
  return true;
#elif defined(DMC_COMPANION_NO_OTA)
  (void)prefs; (void)store; (void)command;
  strcpy(reply, "Error: no OTA on 4 MB flash boards, update over USB");
  return true;
#elif !defined(DMC_COMPANION_OTA)
  (void)prefs; (void)store; (void)command;
  strcpy(reply, "Error: OTA not configured (build with build.sh)");
  return true;
#else
  const char* arg = command[3] ? &command[4] : "";
  if (strcmp(arg, "check") == 0) {
    companionOtaRun(otaBase(prefs), true, reply);
    return true;
  }
  if (strcmp(arg, "update") == 0) {
    if (s_ota_pending) {
      strcpy(reply, "> OTA already starting");
      return true;
    }
    char found[CNET_REPLY_SIZE];
    if (!companionOtaRun(otaBase(prefs), true, found)) {
      strcpy(reply, found);   // up to date, or why it cannot update
      return true;
    }
    // Flash from the main loop a moment later, so this reply still reaches the
    // app or the mesh before the download takes the CPU and the reboot follows.
    StrHelper::strncpy(s_ota_base, otaBase(prefs), sizeof(s_ota_base));
    s_ota_pending = true;
    s_ota_start_at = millis() + 3000;
    snprintf(reply, CNET_REPLY_SIZE, "> %s; installing, reboots when done", found);
    return true;
  }
  if (strcmp(arg, "status") == 0) {
    if (s_ota_pending)          strcpy(reply, "> starting");
    else if (s_ota_last[0])     snprintf(reply, CNET_REPLY_SIZE, "> last: %s", s_ota_last);
    else                        strcpy(reply, "> idle");
    return true;
  }
  if (strcmp(arg, "branch") == 0) {
    const char* base = otaBase(prefs);
    snprintf(reply, CNET_REPLY_SIZE, "> %s%s", otaChannelName(base),
             prefs.ota_channel == OTA_CHANNEL_NATIVE ? " (build default)" : "");
    return true;
  }
  if (strncmp(arg, "branch ", 7) == 0) {
    uint8_t ch;
    if (!ota_channel_parse(&arg[7], &ch)) {
      strcpy(reply, "Error: use ota branch stable|dev|default");
      return true;
    }
    prefs.ota_channel = ch;
    saveCompanionNet(prefs, store);
    snprintf(reply, CNET_REPLY_SIZE, "> OTA channel is now %s", otaChannelName(otaBase(prefs)));
    return true;
  }
  strcpy(reply, "> ota [ check | update | status | branch [stable|dev|default] ]");
  return true;
#endif
}

// ---- SNMP --------------------------------------------------------------------

static bool isSnmpCommand(const char* command) {
  return strcmp(command, "get snmp") == 0 || strncmp(command, "get snmp.", 9) == 0 ||
         strncmp(command, "set snmp ", 9) == 0 || strncmp(command, "set snmp.", 9) == 0;
}

static bool snmpCommand(CompanionNetPrefs& prefs, DataStore& store, const char* command, bool remote, char* reply) {
#ifndef DMC_COMPANION_SNMP
  (void)prefs; (void)store; (void)command; (void)remote;
  strcpy(reply, "Error: SNMP is not in this build");
  return true;
#else
  if (strcmp(command, "get snmp") == 0) {
    strcpy(reply, prefs.snmp_on ? "> on" : "> off");
    return true;
  }
  if (strcmp(command, "get snmp.status") == 0) {
    if (!prefs.snmp_on)               strcpy(reply, "> off");
    else if (companionSnmpRunning())  strcpy(reply, "> running on udp/161");
    else                              strcpy(reply, "> on, waiting for Wi-Fi");
    return true;
  }
  if (strncmp(command, "set snmp ", 9) == 0) {
    const char* v = &command[9];
    bool on;
    if (strcmp(v, "on") == 0 || strcmp(v, "1") == 0)        on = true;
    else if (strcmp(v, "off") == 0 || strcmp(v, "0") == 0)  on = false;
    else { strcpy(reply, "Error: use set snmp on|off"); return true; }
    prefs.snmp_on = on;   // the main loop starts or stops the agent
    saveCompanionNet(prefs, store);
    sprintf(reply, "> snmp is now %s", on ? "on" : "off");
    return true;
  }
  if (strcmp(command, "get snmp.community") == 0) {
    if (remote) {   // the community is the only access control: keep it off the mesh
      strcpy(reply, "Error: not available remotely");
    } else {
      sprintf(reply, "> %s", prefs.snmp_community);
    }
    return true;
  }
  if (strncmp(command, "set snmp.community ", 19) == 0) {
    const char* v = &command[19];
    size_t len = strlen(v);
    if (len == 0 || len >= sizeof(prefs.snmp_community)) {
      sprintf(reply, "Error: 1 to %d characters", (int)sizeof(prefs.snmp_community) - 1);
      return true;
    }
    strcpy(prefs.snmp_community, v);
    saveCompanionNet(prefs, store);
    companionSnmpSetCommunity(prefs.snmp_community);
    strcpy(reply, "> snmp.community updated");
    return true;
  }
  strcpy(reply, "> snmp: get snmp | set snmp on|off | get snmp.status | get|set snmp.community");
  return true;
#endif
}

// ---- MyMesh ------------------------------------------------------------------

// From handleCommand(). sender_timestamp is 0 for USB serial and the app, and
// the sender's clock for remote CLI over the mesh.
bool MyMesh::dmcNetCommand(const char* command, uint32_t sender_timestamp, char* reply) {
  if (companionMacCommand(command, reply)) return true;
  if (isOtaCommand(command)) return otaCommand(_cnet, *_store, command, reply);
  if (isSnmpCommand(command)) return snmpCommand(_cnet, *_store, command, sender_timestamp != 0, reply);
  return false;
}

// From the main loop.
void MyMesh::dmcNetLoop() {
#ifdef DMC_COMPANION_SNMP
  companionSnmpLoop(*this, _cnet, WiFi.status() == WL_CONNECTED);
#endif
#ifdef DMC_COMPANION_OTA
  if (s_ota_pending && (int32_t)(millis() - s_ota_start_at) >= 0) {
    s_ota_pending = false;
    companionOtaRun(s_ota_base, false, s_ota_last);   // reboots on success
    Serial.printf("OTA: %s\n", s_ota_last);
  }
#endif
}

int MyMesh::dmcNumChannels() {
  int n = 0;
#ifdef MAX_GROUP_CHANNELS
  ChannelDetails ch;
  for (int i = 0; i < MAX_GROUP_CHANNELS; i++) {
    if (getChannel(i, ch) && ch.name[0]) n++;
  }
#endif
  return n;
}
