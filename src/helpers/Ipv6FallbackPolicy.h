#pragma once

#include <stdint.h>

#include "bridges/MQTTErrorLabels.h"

// When the observer may prefer a broker's AAAA record over its A record.
//
// esp-tls on IDF 4.4 resolves with AF_UNSPEC, which lwIP answers IPv4-first,
// and it only ever tries the first address. ESP32Ipv6Preference.cpp therefore
// wraps lwip_getaddrinfo() to ask for AAAA first when the node holds a usable
// IPv6 source address. A network can hand out a global address over SLAAC and
// still drop IPv6 upstream, and esp-tls has no second address to fall back
// to, so a preferred-but-broken IPv6 path would keep every slot offline. This
// policy is that fallback: a few consecutive connect-level failures on an IPv6
// attempt suspend the preference for a while, and the next lookup returns the
// A record as if IPv6 were off.
//
// Pure state machine so host tests can drive it; the caller serializes access
// (the esp-mqtt task of each slot reports into one shared State).
namespace Ipv6FallbackPolicy {

static constexpr uint8_t kFailuresBeforeSuspend = 2;
// Long enough that a broken upstream does not cost a retry ladder rung every
// few minutes, short enough that a fixed one is picked up the same hour.
static constexpr uint32_t kSuspendMs = 15UL * 60UL * 1000UL;

struct State {
  uint8_t failures = 0;
  bool suspended = false;
  uint32_t suspended_at_ms = 0;
  bool last_lookup_ipv6 = false;  // family the most recent lookup returned
};

static inline bool suspensionActive(const State& s, uint32_t now_ms) {
  return s.suspended && (uint32_t)(now_ms - s.suspended_at_ms) < kSuspendMs;
}

static inline bool preferIpv6(const State& s, uint32_t now_ms) {
  return !suspensionActive(s, now_ms);
}

// Only failures that say the TCP connect itself went nowhere. A TLS handshake,
// certificate or CONNACK failure reached the broker, so the address family
// worked and switching it would not help.
static inline bool isConnectFailure(int tls_err) {
  return tls_err == MQTTErrorLabels::kTlsFailedConnectToHost ||
         tls_err == MQTTErrorLabels::kTlsConnectionTimeout ||
         tls_err == MQTTErrorLabels::kTlsCannotCreateSocket;
}

static inline void noteLookup(State& s, bool returned_ipv6) {
  s.last_lookup_ipv6 = returned_ipv6;
}

// Returns true when this failure started a suspension.
static inline bool noteConnectFailure(State& s, int tls_err, uint32_t now_ms) {
  if (!isConnectFailure(tls_err) || !s.last_lookup_ipv6) return false;
  if (s.suspended && !suspensionActive(s, now_ms)) {
    // The window ran out and this was the retry over IPv6: start counting again.
    s.suspended = false;
    s.failures = 0;
  }
  if (s.suspended) return false;
  if (s.failures < UINT8_MAX) s.failures++;
  if (s.failures < kFailuresBeforeSuspend) return false;
  s.suspended = true;
  s.suspended_at_ms = now_ms;
  s.failures = 0;
  return true;
}

// A broker connection over IPv6 proves the path. One over IPv4 during a
// suspension proves nothing about IPv6, so the window keeps running.
static inline void noteConnected(State& s) {
  if (s.last_lookup_ipv6) s.failures = 0;
}

// A new association may be a different network with a working IPv6 upstream.
static inline void noteLinkChange(State& s) {
  s = State();
}

static inline const char* stateName(const State& s, uint32_t now_ms) {
  return suspensionActive(s, now_ms) ? "suspended" : "preferred";
}

}  // namespace Ipv6FallbackPolicy
