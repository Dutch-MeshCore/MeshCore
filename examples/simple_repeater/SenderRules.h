#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// Sender and text rules for decrypted group text: block, throttle (one match
// per N seconds slips past, the excess is dropped) and probabilistic dosing
// (the rule decides only that share of its matches). Rules are evaluated top
// to bottom; a rule that steps aside (failed roll, or a within-budget throttle
// pass) leaves the decision to the next rule, and the first rule that decides
// drops the packet. Header-only and dependency-free so it runs on `native`.

#define FILTER_RULE_COUNT  8    // sender rules, and separately text rules
#define FILTER_SENDER_LEN  16   // sender pattern incl. NUL
#define FILTER_TEXT_LEN    24   // text pattern incl. NUL
#define FILTER_WATCH_COUNT 4    // extra channels decrypted for the rules (Public is always)

struct SenderRule {
  char name[FILTER_SENDER_LEN];   // exact name, or prefix when it ends in '*'; '' = empty slot
  uint16_t secs;                  // 0 = block, else throttle: one pass per secs
  uint8_t prob;                   // 1..100: share of matches the rule decides
  uint8_t reserved;
};

struct TextRule {
  char text[FILTER_TEXT_LEN];     // substring, or prefix when it starts with '^'; '' = empty slot
  uint16_t secs;
  uint8_t prob;
  uint8_t reserved;
};

namespace FilterRules {

  inline const char* patternOf(const SenderRule& r) { return r.name; }
  inline const char* patternOf(const TextRule& r) { return r.text; }

  struct GroupText {
    const char* sender;
    size_t sender_len;
    const char* text;
    size_t text_len;
  };

  // Splits a decrypted GRP_TXT plaintext (4-byte timestamp, flags byte, then
  // "Sender: text") into its sender and text. Only plain text (txt_type 0)
  // carries that shape; anything else is rejected. Neither slice is
  // NUL-terminated.
  inline bool parseGroupText(const uint8_t* data, int len, GroupText* out) {
    if (data == nullptr || out == nullptr || len < 6) return false;
    if ((data[4] >> 2) != 0) return false;   // TXT_TYPE_PLAIN only

    const char* body = (const char*)data + 5;
    size_t n = (size_t)len - 5;
    size_t end = 0;
    while (end < n && body[end] != '\0') end++;
    n = end;

    for (size_t i = 0; i + 1 < n; i++) {
      if (body[i] != ':' || body[i + 1] != ' ') continue;
      if (i == 0) return false;   // no sender
      out->sender = body;
      out->sender_len = i;
      out->text = body + i + 2;
      out->text_len = n - i - 2;
      return true;
    }
    return false;
  }

  // Exact match, or prefix match when the pattern ends in '*' (a lone '*'
  // matches every sender). Case-sensitive.
  inline bool matchName(const char* pattern, const char* s, size_t n) {
    if (pattern == nullptr || pattern[0] == '\0' || s == nullptr) return false;
    size_t plen = strlen(pattern);
    if (pattern[plen - 1] == '*') {
      plen--;
      return n >= plen && memcmp(s, pattern, plen) == 0;
    }
    return n == plen && memcmp(s, pattern, plen) == 0;
  }

  // Substring match, or prefix match when the pattern starts with '^'.
  // Case-sensitive; `s` need not be NUL-terminated.
  inline bool matchText(const char* pattern, const char* s, size_t n) {
    if (pattern == nullptr || pattern[0] == '\0' || s == nullptr) return false;
    if (pattern[0] == '^') {
      const char* p = pattern + 1;
      size_t plen = strlen(p);
      if (plen == 0) return false;
      return n >= plen && memcmp(s, p, plen) == 0;
    }
    size_t plen = strlen(pattern);
    if (plen > n) return false;
    for (size_t i = 0; i + plen <= n; i++) {
      if (memcmp(s + i, pattern, plen) == 0) return true;
    }
    return false;
  }

  // Ordered first-match evaluation. Returns the slot of the rule that decided
  // to drop, or -1 when every rule stepped aside or none matched. `rnd100` is
  // a roll in 0..99; `last_pass` (millis, 0 = never) and `passes` are per-slot
  // throttle state the caller owns. Unsigned subtraction keeps the window
  // correct across the millis wrap.
  template <typename Rule, typename Match>
  inline int evaluate(const Rule* rules, int count, uint32_t now_ms, uint8_t rnd100,
                      uint32_t* last_pass, uint32_t* passes, Match match) {
    if (rules == nullptr || last_pass == nullptr) return -1;
    for (int i = 0; i < count; i++) {
      const Rule& r = rules[i];
      if (patternOf(r)[0] == '\0') continue;
      if (!match(r)) continue;
      if (r.prob < 100 && rnd100 >= r.prob) continue;   // failed roll: step aside
      if (r.secs > 0) {
        uint32_t window = (uint32_t)r.secs * 1000UL;
        if (last_pass[i] == 0 || now_ms - last_pass[i] >= window) {
          last_pass[i] = now_ms != 0 ? now_ms : 1;   // within budget: pass, start a new window
          if (passes != nullptr) passes[i]++;
          continue;
        }
      }
      return i;
    }
    return -1;
  }

  inline bool parseUint(const char* s, long max, long* out) {
    if (s == nullptr || s[0] == '\0') return false;
    for (const char* p = s; *p; p++) {
      if (*p < '0' || *p > '9') return false;
    }
    long v = strtol(s, nullptr, 10);
    if (v < 0 || v > max) return false;
    *out = v;
    return true;
  }

  // Optional CLI arguments of a rule: `[secs] [prob]`. Missing = block, 100%.
  // 0% is rejected: a rule that never decides is a disabled rule, remove it.
  inline bool parseArgs(const char* secs_str, const char* prob_str, uint16_t* secs, uint8_t* prob) {
    long s = 0, p = 100;
    if (secs_str != nullptr && !parseUint(secs_str, 65535, &s)) return false;
    if (prob_str != nullptr && (!parseUint(prob_str, 100, &p) || p == 0)) return false;
    *secs = (uint16_t)s;
    *prob = (uint8_t)p;
    return true;
  }
}
