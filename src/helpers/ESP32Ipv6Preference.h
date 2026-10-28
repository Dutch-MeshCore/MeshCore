#pragma once

#include <stddef.h>

#ifdef ESP_PLATFORM

// IPv6-first broker lookups with IPv4 fallback; see ESP32Ipv6Preference.cpp
// for the lwip_getaddrinfo() wrap and Ipv6FallbackPolicy.h for the rules.
//
// Everything here is safe to call from any task: the esp-mqtt event task of
// each slot reports connect outcomes, the Arduino event task reports link
// changes, and the CLI reads the state.
namespace Ipv6Preference {

// The stored `wifi.ipv6` setting. Off makes the lookup wrapper a pass-through
// and keeps the Wi-Fi link from requesting IPv6 on its next association.
void setEnabled(bool enabled);
bool enabled();

void noteConnectFailure(int tls_err);
void noteConnected();
void noteLinkChange();

// "off" when disabled, "none" without a usable IPv6 source address, else
// "preferred"/"suspended".
const char* stateName();

// First global (else unique-local) address of the Wi-Fi station. Writes "-"
// and returns false when it has none.
bool formatStationAddress(char* out, size_t out_size);

}  // namespace Ipv6Preference

#endif
