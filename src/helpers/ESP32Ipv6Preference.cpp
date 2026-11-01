#ifdef ESP_PLATFORM

// Link-time wrap of lwip_getaddrinfo() that asks for a host's AAAA record
// first when the node can actually use it, and otherwise behaves exactly like
// the original.
//
// Why a wrap: esp-tls (precompiled in the Arduino-ESP32 2.x SDK, libesp-tls.a)
// resolves every broker with hints.ai_family = AF_UNSPEC. lwIP answers that
// IPv4-first and returns a single address, and esp-tls connects to that one
// address only. A dual-stack broker is therefore always reached over IPv4 and
// there is no config knob for it on IDF 4.4 (esp_tls_cfg_t gained addr_family
// in IDF 5). [esp32_base] adds `-Wl,--wrap=lwip_getaddrinfo`, which routes the
// SDK's call through __wrap_lwip_getaddrinfo below.
//
// Who is affected: only AF_UNSPEC callers, which in these images means esp-tls,
// i.e. the MQTT slots. Arduino's WiFiClient, WiFiClientSecure, HTTPClient (OTA)
// and WiFiUDP (NTP) resolve through WiFiGenericClass::hostByName(), which is
// IPv4-only and never reaches this function. Callers that ask for AF_INET or
// AF_INET6 explicitly pass straight through.
//
// When the AAAA path is taken, all of these must hold:
//  - `wifi.ipv6` is on (Ipv6Preference::setEnabled);
//  - some interface holds a valid non-link-local IPv6 address (cheap pre-check,
//    so builds that never enable IPv6 do not pay for an AAAA query);
//  - Ipv6FallbackPolicy has not suspended the preference after repeated IPv6
//    connect failures;
//  - the AAAA lookup succeeds and lwIP has a route to the address with a
//    source address of matching scope (a global destination needs a global
//    source, not just a ULA or link-local one).
// Anything else falls through to the original AF_UNSPEC lookup, so a broker
// without an AAAA record, or a network without IPv6 upstream, behaves as before.

#include "ESP32Ipv6Preference.h"
#include "Ipv6FallbackPolicy.h"

#include <Arduino.h>
#include <atomic>
#include <string.h>

#include <esp_netif.h>
#include <freertos/FreeRTOS.h>
#include <lwip/ip6.h>
#include <lwip/ip6_addr.h>
#include <lwip/netdb.h>
#include <lwip/netif.h>
#include <lwip/sockets.h>

namespace {

portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
Ipv6FallbackPolicy::State s_state;
std::atomic<bool> s_enabled{true};

#if defined(MQTT_DEBUG)
// One line per broker lookup, so a node on a dual-stack network shows which
// family each connect attempt used.
void logLookup(const char* nodename, const struct addrinfo* ai) {
  if (Serial.availableForWrite() <= 0 || ai == nullptr || ai->ai_addr == nullptr) return;
  char text[48] = "?";
  if (ai->ai_family == AF_INET6) {
    const struct sockaddr_in6* sa = reinterpret_cast<const struct sockaddr_in6*>(ai->ai_addr);
    inet_ntop(AF_INET6, &sa->sin6_addr, text, sizeof(text));
  } else if (ai->ai_family == AF_INET) {
    const struct sockaddr_in* sa = reinterpret_cast<const struct sockaddr_in*>(ai->ai_addr);
    inet_ntop(AF_INET, &sa->sin_addr, text, sizeof(text));
  }
  Serial.printf("MQTT: %s -> %s %s\n", nodename,
                ai->ai_family == AF_INET6 ? "IPv6" : "IPv4", text);
}
#else
void logLookup(const char*, const struct addrinfo*) {}
#endif

bool policyPrefersIpv6() {
  portENTER_CRITICAL(&s_mux);
  const bool prefer = Ipv6FallbackPolicy::preferIpv6(s_state, millis());
  portEXIT_CRITICAL(&s_mux);
  return prefer;
}

void recordLookup(bool returned_ipv6) {
  portENTER_CRITICAL(&s_mux);
  Ipv6FallbackPolicy::noteLookup(s_state, returned_ipv6);
  portEXIT_CRITICAL(&s_mux);
}

// netif and ND state belong to the TCP/IP thread, so both checks run there
// through esp_netif_tcpip_exec(). getaddrinfo() blocks, so it is never called
// on that thread itself and the exec cannot deadlock.
esp_err_t anyIpv6SourceOnTcpipThread(void* ctx) {
  bool* any = static_cast<bool*>(ctx);
  struct netif* n;
  NETIF_FOREACH(n) {
    if (!netif_is_up(n) || !netif_is_link_up(n)) continue;
    for (int i = 0; i < LWIP_IPV6_NUM_ADDRESSES; i++) {
      const ip6_addr_t* addr = netif_ip6_addr(n, i);
      if (ip6_addr_isvalid(netif_ip6_addr_state(n, i)) &&
          !ip6_addr_islinklocal(addr) && !ip6_addr_isloopback(addr)) {
        *any = true;
        return ESP_OK;
      }
    }
  }
  return ESP_OK;
}

bool anyIpv6Source() {
  bool any = false;
  esp_netif_tcpip_exec(anyIpv6SourceOnTcpipThread, &any);
  return any;
}

struct RouteCheck {
  ip6_addr_t dest;
  bool usable;
};

esp_err_t routeCheckOnTcpipThread(void* ctx) {
  RouteCheck* check = static_cast<RouteCheck*>(ctx);
  check->usable = false;
  struct netif* n = ip6_route(IP6_ADDR_ANY6, &check->dest);
  if (n == nullptr) return ESP_OK;
  const ip_addr_t* src = ip6_select_source_address(n, &check->dest);
  if (src == nullptr || !IP_IS_V6(src)) return ESP_OK;
  const ip6_addr_t* src6 = ip_2_ip6(src);
  // lwIP falls back to a link-local source when nothing better exists; that
  // cannot reach a broker off-link. A global broker needs a global source.
  if (ip6_addr_islinklocal(src6)) return ESP_OK;
  if (ip6_addr_isglobal(&check->dest) && !ip6_addr_isglobal(src6)) return ESP_OK;
  check->usable = true;
  return ESP_OK;
}

bool routable(const struct addrinfo* ai) {
  if (ai == nullptr || ai->ai_family != AF_INET6 || ai->ai_addr == nullptr) return false;
  const struct sockaddr_in6* sa = reinterpret_cast<const struct sockaddr_in6*>(ai->ai_addr);
  RouteCheck check = {};
  memcpy(&check.dest.addr, &sa->sin6_addr, sizeof(check.dest.addr));
  ip6_addr_clear_zone(&check.dest);
  esp_netif_tcpip_exec(routeCheckOnTcpipThread, &check);
  return check.usable;
}

}  // namespace

extern "C" int __real_lwip_getaddrinfo(const char* nodename, const char* servname,
                                       const struct addrinfo* hints, struct addrinfo** res);

extern "C" int __wrap_lwip_getaddrinfo(const char* nodename, const char* servname,
                                       const struct addrinfo* hints, struct addrinfo** res) {
  const bool unspecified = hints == nullptr || hints->ai_family == AF_UNSPEC;
  if (nodename != nullptr && res != nullptr && unspecified &&
      s_enabled.load(std::memory_order_relaxed) && anyIpv6Source() && policyPrefersIpv6()) {
    struct addrinfo v6_hints;
    if (hints != nullptr) {
      v6_hints = *hints;
    } else {
      memset(&v6_hints, 0, sizeof(v6_hints));
    }
    v6_hints.ai_family = AF_INET6;
    struct addrinfo* v6 = nullptr;
    if (__real_lwip_getaddrinfo(nodename, servname, &v6_hints, &v6) == 0 && v6 != nullptr) {
      if (routable(v6)) {
        *res = v6;
        recordLookup(true);
        logLookup(nodename, v6);
        return 0;
      }
      lwip_freeaddrinfo(v6);
    }
  }
  const int rc = __real_lwip_getaddrinfo(nodename, servname, hints, res);
  if (rc == 0 && res != nullptr && *res != nullptr) {
    recordLookup((*res)->ai_family == AF_INET6);
    if (nodename != nullptr && unspecified) logLookup(nodename, *res);
  }
  return rc;
}

namespace Ipv6Preference {

void setEnabled(bool enabled) {
  s_enabled.store(enabled, std::memory_order_relaxed);
  // Turning it back on should not inherit a suspension from before.
  noteLinkChange();
}

bool enabled() { return s_enabled.load(std::memory_order_relaxed); }

void noteConnectFailure(int tls_err) {
  portENTER_CRITICAL(&s_mux);
  const bool suspended = Ipv6FallbackPolicy::noteConnectFailure(s_state, tls_err, millis());
  portEXIT_CRITICAL(&s_mux);
#if defined(MQTT_DEBUG)
  if (suspended && Serial.availableForWrite() > 0) {
    Serial.printf("MQTT: IPv6 connects failing, using IPv4 for %lu min\n",
                  (unsigned long)(Ipv6FallbackPolicy::kSuspendMs / 60000UL));
  }
#else
  (void)suspended;
#endif
}

void noteConnected() {
  portENTER_CRITICAL(&s_mux);
  Ipv6FallbackPolicy::noteConnected(s_state);
  portEXIT_CRITICAL(&s_mux);
}

void noteLinkChange() {
  portENTER_CRITICAL(&s_mux);
  Ipv6FallbackPolicy::noteLinkChange(s_state);
  portEXIT_CRITICAL(&s_mux);
}

const char* stateName() {
  if (!enabled()) return "off";
  if (!anyIpv6Source()) return "none";
  portENTER_CRITICAL(&s_mux);
  const char* name = Ipv6FallbackPolicy::stateName(s_state, millis());
  portEXIT_CRITICAL(&s_mux);
  return name;
}

bool formatStationAddress(char* out, size_t out_size) {
  if (out == nullptr || out_size == 0) return false;
  snprintf(out, out_size, "-");
  esp_netif_t* sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
  if (sta == nullptr) return false;
  esp_ip6_addr_t addrs[LWIP_IPV6_NUM_ADDRESSES] = {};
  const int count = esp_netif_get_all_ip6(sta, addrs);
  int best = -1;
  for (int i = 0; i < count && i < LWIP_IPV6_NUM_ADDRESSES; i++) {
    ip6_addr_t a;
    memcpy(&a.addr, addrs[i].addr, sizeof(a.addr));
    ip6_addr_clear_zone(&a);
    if (ip6_addr_islinklocal(&a)) continue;
    if (ip6_addr_isglobal(&a)) { best = i; break; }
    if (best < 0) best = i;  // unique-local: keep looking for a global one
  }
  if (best < 0) return false;
  ip6_addr_t a;
  memcpy(&a.addr, addrs[best].addr, sizeof(a.addr));
  ip6_addr_clear_zone(&a);
  ip6addr_ntoa_r(&a, out, (int)out_size);
  return true;
}

}  // namespace Ipv6Preference

#endif  // ESP_PLATFORM
