#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string.h>

// Mock SHA256 for native testing — deterministic but not cryptographic.
// finalize() writes real (non-garbage) output so calculatePacketHash() produces
// distinguishable results for packets with different payloads.
// Pointer params are void* to match the real Crypto library signatures, so this
// mock also backs TransportKeyStore.cpp (which hashes into non-uint8_t* dests);
// the region-gating tests exercise pure hierarchy logic and never rely on the
// HMAC output, so resetHMAC/finalizeHMAC stay inert.
class SHA256 {
  uint8_t _state[32];
  size_t _len;
public:
  SHA256() : _len(0) { memset(_state, 0, sizeof(_state)); }

  void update(const void* data, size_t len) {
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < len; i++) {
      uint8_t b = bytes[i];
      _state[_len % 32] ^= b;
      _state[(_len + 1) % 32] += (uint8_t)((b >> 1) | (b << 7));
      _len++;
    }
  }

  void finalize(void* hash, size_t hashLen) {
    uint8_t* out = static_cast<uint8_t*>(hash);
    for (size_t i = 0; i < hashLen; i++) {
      out[i] = _state[i % 32];
    }
  }

  void resetHMAC(const void* key, size_t keyLen) { (void)key; (void)keyLen; }
  void finalizeHMAC(const void* key, size_t keyLen, void* hash, size_t hashLen) {
    (void)key; (void)keyLen; (void)hash; (void)hashLen;
  }
};
