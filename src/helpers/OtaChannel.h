#pragma once
#include <stdint.h>
#include <string.h>

// OTA release-channel selector, persisted in NodePrefs::ota_channel.
enum OtaChannel : uint8_t {
  OTA_CH_NATIVE = 0,  // follow the channel this build was made for
  OTA_CH_STABLE = 1,
  OTA_CH_DEV    = 2,
};

// Resolve the effective manifest base URL for a channel selector.
// build.sh injects the three bases as compile-time macros:
//   OTA_MANIFEST_BASE        = this build's native channel (defined on every OTA build)
//   OTA_MANIFEST_BASE_STABLE = stable channel
//   OTA_MANIFEST_BASE_DEV    = dev channel
// stable/dev fall back to the native base when their macro is undefined (legacy/local
// builds that only define OTA_MANIFEST_BASE), so this never returns nullptr on an
// OTA-capable build. On a non-OTA build it returns nullptr.
static inline const char* ota_resolve_base(uint8_t channel) {
#if defined(OTA_MANIFEST_BASE)
  switch (channel) {
    case OTA_CH_STABLE:
#if defined(OTA_MANIFEST_BASE_STABLE)
      return OTA_MANIFEST_BASE_STABLE;
#else
      return OTA_MANIFEST_BASE;
#endif
    case OTA_CH_DEV:
#if defined(OTA_MANIFEST_BASE_DEV)
      return OTA_MANIFEST_BASE_DEV;
#else
      return OTA_MANIFEST_BASE;
#endif
    case OTA_CH_NATIVE:
    default:
      return OTA_MANIFEST_BASE;
  }
#else
  (void)channel;
  return nullptr;
#endif
}

// Human label for a selector (for the `ota branch` report).
static inline const char* ota_channel_name(uint8_t channel) {
  switch (channel) {
    case OTA_CH_STABLE: return "stable";
    case OTA_CH_DEV:    return "dev";
    default:            return "native";
  }
}

// Parse an `ota branch` argument. Returns true and sets *out on a known keyword
// (stable|dev|default; "default" -> native); returns false and leaves *out untouched
// otherwise.
static inline bool ota_parse_channel(const char* arg, uint8_t* out) {
  if (strcmp(arg, "stable") == 0)  { *out = OTA_CH_STABLE; return true; }
  if (strcmp(arg, "dev") == 0)     { *out = OTA_CH_DEV;    return true; }
  if (strcmp(arg, "default") == 0) { *out = OTA_CH_NATIVE; return true; }
  return false;
}
