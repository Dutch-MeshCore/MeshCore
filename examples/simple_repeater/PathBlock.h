#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// Path-prefix block list: drop flood packets whose recorded path contains a
// repeater ID starting with one of the configured prefixes. Lets an operator
// isolate one upstream repeater that keeps injecting junk without needing the
// key to any channel. Header-only and dependency-free so it runs on `native`.

#define FILTER_PATH_COUNT   8   // configurable prefixes
#define FILTER_PATH_MAX_LEN 4   // bytes per prefix (2-8 hex digits)

struct PathPrefix {
  uint8_t len;                        // 0 = empty slot
  uint8_t bytes[FILTER_PATH_MAX_LEN];
};

namespace FilterPath {

  inline int hexVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  }

  // Parses 2-8 hex digits (1-4 bytes). Rejects anything else and leaves `out`
  // untouched on failure.
  inline bool parse(const char* hex, PathPrefix* out) {
    if (hex == nullptr || out == nullptr) return false;

    size_t n = strlen(hex);
    if (n < 2 || n > 2 * FILTER_PATH_MAX_LEN || (n & 1)) return false;

    PathPrefix p;
    memset(&p, 0, sizeof(p));
    p.len = (uint8_t)(n / 2);
    for (uint8_t i = 0; i < p.len; i++) {
      int hi = hexVal(hex[2 * i]);
      int lo = hexVal(hex[2 * i + 1]);
      if (hi < 0 || lo < 0) return false;
      p.bytes[i] = (uint8_t)((hi << 4) | lo);
    }
    *out = p;
    return true;
  }

  inline void format(char* out, size_t cap, const PathPrefix& p) {
    if (out == nullptr || cap == 0) return;
    out[0] = '\0';
    size_t len = 0;
    for (uint8_t i = 0; i < p.len && i < FILTER_PATH_MAX_LEN; i++) {
      if (len + 3 > cap) break;
      snprintf(out + len, cap - len, "%02X", (unsigned)p.bytes[i]);
      len += 2;
    }
  }

  // Index of the first prefix that matches the start of any path entry, or -1.
  // A prefix longer than the packet's hash size never matches: A1B2 is one
  // 2-byte ID, not the two 1-byte IDs A1 then B2.
  inline int findMatch(const uint8_t* path, uint8_t hash_size, uint8_t hash_count,
                       const PathPrefix* list, int count) {
    if (path == nullptr || list == nullptr || hash_size == 0) return -1;

    for (int s = 0; s < count; s++) {
      const PathPrefix& p = list[s];
      if (p.len == 0 || p.len > hash_size) continue;
      for (uint8_t h = 0; h < hash_count; h++) {
        if (memcmp(path + (size_t)h * hash_size, p.bytes, p.len) == 0) return s;
      }
    }
    return -1;
  }
}
