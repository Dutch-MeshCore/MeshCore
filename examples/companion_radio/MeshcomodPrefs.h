#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Upgrading a companion from meshcomod (Elektr0Vodka/meshcomod) keeps its
// settings. meshcomod still stores prefs as the binary /new_prefs, which
// upstream's DataStore::loadPrefs() already migrates to /prefs.json, but in
// upstream's layout only. meshcomod files differ in two ways:
//
//   141 bytes  current: upstream's 140 plus a trailing hardware-CAD byte.
//   136 bytes  v1.16 and older (before June 2026): gps_interval was never
//              written, so everything after gps_enabled sits 4 bytes early.
//              Read in upstream's layout, auto-add, RX boost and the default
//              flood scope come out shifted.
//
// This header picks those fields out of the raw file. Header-only and free of
// filesystem and NVS so it runs on `native`; MeshcomodImport.cpp applies it.

namespace MeshcomodPrefs {

  static const size_t UPSTREAM_LEN = 140;
  static const size_t CURRENT_LEN = 141;   // + CAD byte at 140
  static const size_t SHIFTED_LEN = 136;   // no gps_interval

  struct Fix {
    bool has_cad = false;
    uint8_t cad = 0;

    bool shifted = false;   // the fields below are valid only when set
    uint8_t autoadd_config = 0;
    uint8_t autoadd_max_hops = 0;
    uint8_t rx_boosted_gain = 0;
    char scope_name[31] = {};
    uint8_t scope_key[16] = {};
  };

  // Returns true when `file` is a meshcomod /new_prefs that needs a fix-up
  // after upstream's migration; `out` then says which.
  inline bool parse(const uint8_t* file, size_t len, Fix* out) {
    *out = Fix();
    if (file == nullptr) return false;

    if (len == CURRENT_LEN) {
      out->has_cad = true;
      out->cad = file[140] ? 1 : 0;
      return true;
    }

    if (len == SHIFTED_LEN) {
      out->shifted = true;
      out->autoadd_config = file[86];
      out->autoadd_max_hops = file[87];
      out->rx_boosted_gain = file[88] ? 1 : 0;
      memcpy(out->scope_name, &file[89], sizeof(out->scope_name));
      out->scope_name[sizeof(out->scope_name) - 1] = '\0';
      memcpy(out->scope_key, &file[120], sizeof(out->scope_key));
      return true;
    }
    return false;
  }

  // meshcomod keeps Wi-Fi credentials in NVS (namespace "meshcomod"), not in
  // the prefs file. They are copied over only when this firmware has none, so
  // a later `set wifi.clear` is never undone.
  inline bool shouldImportWifi(const char* current_ssid, const char* nvs_ssid) {
    return current_ssid != nullptr && current_ssid[0] == '\0' && nvs_ssid != nullptr && nvs_ssid[0] != '\0';
  }
}

// Firmware side (MeshcomodImport.cpp). Call before the regular prefs load in
// MyMesh::begin(); it only acts on the first boot after a binary-prefs firmware.
class NodePrefs;
class DataStore;
void importMeshcomodSettings(NodePrefs& prefs, DataStore& store);
