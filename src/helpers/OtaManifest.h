#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// Manifest pull OTA (`ota check` / `ota update`): the pure decisions, kept free of
// network and flash so they run in the native tests. The DMC companion does the
// I/O in examples/companion_radio/CompanionOta.cpp. Ported from meshcomod
// (Elektr0Vodka/meshcomod src/helpers/OtaManifest.h); keep the shared parts in sync.
//
// The device reads <base>/<variant>.json, served by the DutchMeshCore OTA server in
// the same shape as for the DMC observer builds:
//   {"file":"<image url>","version":"v1.18.0","hash":"abc1234","partSig":"..."}
// build.sh bakes the bases and the variant (the PlatformIO env name) in.

struct OtaVersion {
  int part[4];   // "v1.18.0.1" -> 1, 18, 0, 1; missing parts stay 0
  int count;     // number of parts present
  char hash[16]; // token after the last '-', or "" when there is no '-'
};

// Parse "v1.18.0.1[-...]-<hash>". The numeric version ends at the first '-'.
// Returns false when the text does not start with a version.
static inline bool ota_version_parse(const char* s, OtaVersion* out) {
  memset(out, 0, sizeof(*out));
  if (s == nullptr) return false;
  const char* p = s;
  if (*p == 'v' || *p == 'V') p++;
  while (out->count < 4) {
    if (*p < '0' || *p > '9') return false;
    int n = 0;
    while (*p >= '0' && *p <= '9') n = n * 10 + (*p++ - '0');
    out->part[out->count++] = n;
    if (*p != '.') break;
    p++;
  }
  if (*p != '\0' && *p != '-') return false;
  const char* dash = strrchr(s, '-');
  if (dash != nullptr) {
    strncpy(out->hash, dash + 1, sizeof(out->hash) - 1);
  }
  return true;
}

enum OtaVerdict : uint8_t {
  OTA_VERDICT_CURRENT = 0,  // the published build is the one running
  OTA_VERDICT_NEWER,        // the published build has a higher version
  OTA_VERDICT_OLDER,        // the running build is newer than the published one
  OTA_VERDICT_REBUILD,      // same version, different commit
};

// Shared-prefix compare absorbs the 7 vs 8 character git abbreviation.
static inline bool ota_hash_equal(const char* a, const char* b) {
  size_t la = strlen(a), lb = strlen(b);
  size_t m = la < lb ? la : lb;
  return m >= 7 && strncmp(a, b, m) == 0;
}

// Compare the running FIRMWARE_VERSION with a manifest's version and hash.
static inline OtaVerdict ota_compare(const char* running, const char* avail_version, const char* avail_hash) {
  OtaVersion own, avail;
  bool own_ok = ota_version_parse(running, &own);
  bool avail_ok = ota_version_parse(avail_version, &avail);
  if (!own_ok || !avail_ok) {
    // No usable version on one side: only the commit can tell them apart.
    char own_hash[16] = {0};
    const char* dash = running ? strrchr(running, '-') : nullptr;
    if (dash) strncpy(own_hash, dash + 1, sizeof(own_hash) - 1);
    return ota_hash_equal(own_hash, avail_hash ? avail_hash : "") ? OTA_VERDICT_CURRENT : OTA_VERDICT_REBUILD;
  }
  for (int i = 0; i < 4; i++) {
    if (avail.part[i] > own.part[i]) return OTA_VERDICT_NEWER;
    if (avail.part[i] < own.part[i]) return OTA_VERDICT_OLDER;
  }
  if (own.hash[0] && avail_hash && avail_hash[0] && !ota_hash_equal(own.hash, avail_hash)) {
    return OTA_VERDICT_REBUILD;
  }
  return OTA_VERDICT_CURRENT;
}

// One partition-table entry, as esp_partition reports it.
struct OtaPartEntry {
  uint8_t type, subtype;
  uint32_t offset, size;
};

// Canonical partition-table signature. MUST match scripts/partition_signature.py:
// each entry "type:subtype:offset:size" in lowercase hex, sorted by offset, joined
// by ','. Sorts `e` in place.
static inline void ota_partsig_format(OtaPartEntry* e, int n, char* out, size_t out_sz) {
  for (int i = 1; i < n; i++) {
    OtaPartEntry k = e[i];
    int j = i - 1;
    while (j >= 0 && e[j].offset > k.offset) { e[j + 1] = e[j]; j--; }
    e[j + 1] = k;
  }
  if (out_sz == 0) return;
  out[0] = 0;
  size_t pos = 0;
  for (int i = 0; i < n; i++) {
    int w = snprintf(out + pos, out_sz - pos, "%s%x:%x:%x:%x", i ? "," : "", e[i].type, e[i].subtype,
                     (unsigned)e[i].offset, (unsigned)e[i].size);
    if (w < 0 || (size_t)w >= out_sz - pos) {  // truncated: keep what fits, stop
      out[out_sz - 1] = 0;
      return;
    }
    pos += (size_t)w;
  }
}

// Text after "http://" or "https://", or nullptr for any other scheme.
static inline const char* ota_url_after_scheme(const char* url) {
  if (url == nullptr) return nullptr;
  if (strncmp(url, "https://", 8) == 0) return url + 8;
  if (strncmp(url, "http://", 7) == 0) return url + 7;
  return nullptr;
}

// Manifest URL for `ota check` / `ota update`, always over plain HTTP: it only
// carries version info, and skipping TLS keeps the check cheap on RAM-tight
// builds. The image itself is fetched from the manifest's `file`.
static inline bool ota_manifest_url(const char* base, const char* variant, char* out, size_t out_sz) {
  const char* rest = ota_url_after_scheme(base);
  if (rest == nullptr) return false;
  int n = snprintf(out, out_sz, "http://%s/%s.json", rest, variant);
  return n > 0 && (size_t)n < out_sz;
}

// A manifest may only point at images under the server's firmware path for this
// product: base ".../meshcomod/v" -> ".../meshcomod/fw/". Either scheme is accepted.
static inline bool ota_firmware_url_allowed(const char* url, const char* base) {
  const char* u = ota_url_after_scheme(url);
  const char* b = ota_url_after_scheme(base);
  if (u == nullptr || b == nullptr) return false;
  size_t blen = strlen(b);
  if (blen < 2 || strcmp(b + blen - 2, "/v") != 0) return false;
  size_t root = blen - 1;  // keep the '/' before "v"
  if (strncmp(u, b, root) != 0 || strncmp(u + root, "fw/", 3) != 0) return false;
  return strstr(u, "..") == nullptr;
}

// The image download is only ever done over TLS.
static inline bool ota_url_is_https(const char* url) {
  return url != nullptr && strncmp(url, "https://", 8) == 0;
}

// Skip a JSON string. p is at the opening quote; returns the character after the
// closing quote, or nullptr when the string never ends.
static inline const char* ota_json_skip_string(const char* p) {
  for (p++; *p; p++) {
    if (*p == '\\') {
      if (*++p == '\0') return nullptr;
    } else if (*p == '"') {
      return p + 1;
    }
  }
  return nullptr;
}

static inline const char* ota_json_skip_ws(const char* p) {
  while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
  return p;
}

// Read the string value of a top-level key of a flat JSON object (the manifest),
// so the companion needs no JSON library. Handles the escapes a JSON encoder
// emits for URLs and plain text; \u escapes are accepted for ASCII only. Returns
// false (out = "") when the key is missing, not a string, malformed, or does not
// fit in out.
static inline bool ota_json_string(const char* json, const char* key, char* out, size_t out_sz) {
  if (out_sz) out[0] = 0;
  if (json == nullptr || key == nullptr || out_sz == 0) return false;
  const size_t klen = strlen(key);
  int depth = 0;
  bool expect_key = false;
  const char* p = json;
  while (*p) {
    char c = *p;
    if (c == '{' || c == '[') {
      depth++;
      expect_key = (c == '{' && depth == 1);
      p++;
    } else if (c == '}' || c == ']') {
      depth--;
      p++;
    } else if (c == ',') {
      expect_key = (depth == 1);
      p++;
    } else if (c == '"') {
      const char* end = ota_json_skip_string(p);
      if (end == nullptr) return false;
      if (depth != 1 || !expect_key) {   // a value, or a key of a nested object
        p = end;
        continue;
      }
      expect_key = false;
      bool match = (size_t)(end - p - 2) == klen && strncmp(p + 1, key, klen) == 0;
      p = ota_json_skip_ws(end);
      if (*p != ':') return false;
      p = ota_json_skip_ws(p + 1);
      if (!match) continue;
      if (*p != '"') return false;
      size_t n = 0;
      for (p++; *p != '"'; p++) {
        if (*p == '\0') { out[0] = 0; return false; }
        char ch = *p;
        if (ch == '\\') {
          p++;
          switch (*p) {
            case '"': case '\\': case '/': ch = *p; break;
            case 'b': ch = '\b'; break;
            case 'f': ch = '\f'; break;
            case 'n': ch = '\n'; break;
            case 'r': ch = '\r'; break;
            case 't': ch = '\t'; break;
            case 'u': {
              unsigned v = 0;
              for (int i = 1; i <= 4; i++) {
                char h = p[i];
                v <<= 4;
                if (h >= '0' && h <= '9') v |= (unsigned)(h - '0');
                else if (h >= 'a' && h <= 'f') v |= (unsigned)(h - 'a' + 10);
                else if (h >= 'A' && h <= 'F') v |= (unsigned)(h - 'A' + 10);
                else { out[0] = 0; return false; }
              }
              if (v == 0 || v > 0x7f) { out[0] = 0; return false; }
              ch = (char)v;
              p += 4;
              break;
            }
            default: out[0] = 0; return false;
          }
        }
        if (n + 1 >= out_sz) { out[0] = 0; return false; }
        out[n++] = ch;
      }
      out[n] = 0;
      return true;
    } else {
      p++;
    }
  }
  return false;
}

// `ota branch`: which server channel the device pulls from. NATIVE is the one the
// build was made for (dev for a DMC_CHANNEL=dev build, else stable).
enum : uint8_t {
  OTA_CHANNEL_NATIVE = 0,
  OTA_CHANNEL_STABLE = 1,
  OTA_CHANNEL_DEV = 2,
};

static inline bool ota_channel_parse(const char* name, uint8_t* out) {
  if (name == nullptr) return false;
  if (strcmp(name, "stable") == 0)  { *out = OTA_CHANNEL_STABLE; return true; }
  if (strcmp(name, "dev") == 0)     { *out = OTA_CHANNEL_DEV; return true; }
  if (strcmp(name, "default") == 0) { *out = OTA_CHANNEL_NATIVE; return true; }
  return false;
}

static inline const char* ota_channel_base(uint8_t sel, const char* native, const char* stable, const char* dev) {
  if (sel == OTA_CHANNEL_STABLE) return stable;
  if (sel == OTA_CHANNEL_DEV) return dev;
  return native;
}

static inline const char* ota_channel_name(const char* base, const char* stable, const char* dev) {
  if (strcmp(base, stable) == 0) return "stable";
  if (strcmp(base, dev) == 0) return "dev";
  return "custom";
}
