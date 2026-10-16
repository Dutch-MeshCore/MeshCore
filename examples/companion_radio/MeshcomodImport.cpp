// Keeps a companion's settings when it is upgraded from meshcomod. See
// MeshcomodPrefs.h for what differs and why. Identity, contacts, channels and
// blobs need nothing: meshcomod keeps them in the same files and formats.

#include "MyMesh.h"
#include "MeshcomodPrefs.h"

// The IDF NVS API, not Arduino's Preferences: PlatformIO's library finder does not
// see the #if, and on RP2040 a framework library (iLabs Hearth) ships its own
// Preferences.h that then gets built and fails.
#if defined(ESP32) && defined(ENABLE_WIFI_INTERFACE)
  #include <nvs.h>
#endif

void importMeshcomodSettings(NodePrefs& prefs, DataStore& store) {
  FILESYSTEM* fs = store.getPrimaryFS();
  // Only on the first boot after a binary-prefs firmware: once /prefs.json
  // exists this firmware owns the settings.
  if (fs == nullptr || fs->exists("/prefs.json") || !fs->exists("/new_prefs")) return;

  uint8_t raw[MeshcomodPrefs::CURRENT_LEN + 8];
  size_t len = 0;
  File file = store.openRead(fs, "/new_prefs");
  if (file) {
    len = file.read(raw, sizeof(raw));
    file.close();
  }

  store.loadPrefs(prefs);   // upstream's migration: /new_prefs in upstream's layout -> /prefs.json

  bool changed = false;
  MeshcomodPrefs::Fix fix;
  if (MeshcomodPrefs::parse(raw, len, &fix)) {
    if (fix.has_cad) prefs.cad_enabled = fix.cad;
    if (fix.shifted) {
      prefs.gps_interval = 0;   // never stored by those versions
      prefs.autoadd_config = fix.autoadd_config;
      prefs.autoadd_max_hops = fix.autoadd_max_hops;
      prefs.rx_boosted_gain = fix.rx_boosted_gain;
      memcpy(prefs.default_scope_name, fix.scope_name, sizeof(prefs.default_scope_name));
      prefs.default_scope_name[sizeof(prefs.default_scope_name) - 1] = '\0';
      memcpy(prefs.default_scope_key, fix.scope_key, sizeof(prefs.default_scope_key));
    }
    changed = true;
  }

#if defined(ESP32) && defined(ENABLE_WIFI_INTERFACE)
  // meshcomod's runtime Wi-Fi credentials (WifiRuntimeStore: NVS "meshcomod").
  // NVS survives an app-only flash; read-only, so meshcomod's copy stays intact.
  nvs_handle_t nvs;
  if (nvs_open("meshcomod", NVS_READONLY, &nvs) == ESP_OK) {
    char ssid[sizeof(prefs.wifi_ssid)] = { 0 };
    char pwd[sizeof(prefs.wifi_pwd)] = { 0 };
    size_t n = sizeof(ssid);
    if (nvs_get_str(nvs, "wifi_ssid", ssid, &n) != ESP_OK) ssid[0] = 0;   // missing or too long
    n = sizeof(pwd);
    if (nvs_get_str(nvs, "wifi_pwd", pwd, &n) != ESP_OK) pwd[0] = 0;
    nvs_close(nvs);
    if (MeshcomodPrefs::shouldImportWifi(prefs.wifi_ssid, ssid)) {
      memcpy(prefs.wifi_ssid, ssid, sizeof(prefs.wifi_ssid));
      memcpy(prefs.wifi_pwd, pwd, sizeof(prefs.wifi_pwd));
      changed = true;
    }
  }
#endif

  if (changed) store.savePrefs(prefs);
}
