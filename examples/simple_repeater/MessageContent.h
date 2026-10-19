#pragma once

#include <stdint.h>
#include <string.h>

#include "FilterStats.h"
#include "MessageAge.h"

// The malformed scan on decrypted group text: a plausible timestamp, and for
// plain text a non-empty, valid UTF-8 body. Shared by the repeater filter and
// the companion filter. Header-only and clock-free (the caller passes `now`)
// so it runs on `native`.

namespace MessageContent {

  // Strict UTF-8: no overlong forms, surrogates, non-characters or code points
  // past U+10FFFF. A NUL ends the scan early.
  inline bool isValidUTF8(const uint8_t* data, uint8_t len) {
    if (data == nullptr || len == 0) return false;

    uint8_t i = 0;
    while (i < len) {
      uint8_t c = data[i++];
      if (c == 0) break;
      if (c < 0x80) continue;

      uint32_t codepoint;
      uint8_t needed;
      if ((c & 0xE0) == 0xC0) {
        codepoint = c & 0x1F;
        needed = 1;
        if (codepoint == 0) return false;
      } else if ((c & 0xF0) == 0xE0) {
        codepoint = c & 0x0F;
        needed = 2;
      } else if ((c & 0xF8) == 0xF0) {
        codepoint = c & 0x07;
        needed = 3;
      } else {
        return false;
      }

      if (i + needed > len) return false;
      for (uint8_t j = 0; j < needed; j++) {
        uint8_t cc = data[i++];
        if ((cc & 0xC0) != 0x80) return false;
        codepoint = (codepoint << 6) | (cc & 0x3F);
      }

      if (needed == 1 && codepoint < 0x80) return false;
      if (needed == 2 && codepoint < 0x0800) return false;
      if (needed == 3 && codepoint < 0x10000) return false;
      if (codepoint > 0x10FFFF) return false;
      if (codepoint >= 0xD800 && codepoint <= 0xDFFF) return false;
      if (codepoint >= 0xFDD0 && codepoint <= 0xFDEF) return false;
      if ((codepoint & 0xFFFE) == 0xFFFE) return false;
    }
    return true;
  }

  // `data` is a decrypted GRP_TXT plaintext: 4-byte timestamp, flags byte
  // (txt_type << 2), then the text. On failure `reason` gets a MalformedReason.
  inline bool validate(const uint8_t* data, uint8_t len, uint32_t now, uint8_t* reason) {
    if (data == nullptr || len <= 5) {
      *reason = MALFORMED_SHORT;
      return false;
    }

    // check timestamp
    uint32_t timestamp;
    memcpy(&timestamp, &data[0], 4);
    if (MessageAge::implausible(timestamp, now)) {
      *reason = MALFORMED_TIMESTAMP;
      return false;
    }

    // only plain text (txt_type 0) has a body to check
    uint8_t txt_type = data[4] >> 2;
    if (txt_type != 0) return true;

    // calculate text length
    uint8_t txt_len = 5;
    while (txt_len < len && data[txt_len] != 0) txt_len++;
    txt_len -= 5;
    if (!txt_len) {
      *reason = MALFORMED_EMPTY;
      return false;
    }

    if (!isValidUTF8(&data[5], txt_len)) {
      *reason = MALFORMED_UTF8;
      return false;
    }
    return true;
  }
}
