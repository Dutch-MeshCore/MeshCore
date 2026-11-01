# SNMP Agent for MeshCore Observer Firmware

Observer nodes can run a read-only SNMP v2c agent so tools such as Zabbix, LibreNMS, or
PRTG can poll radio, MQTT, memory, and network stats.

- **Protocol**: SNMPv2c (read-only, no SET support)
- **Port**: UDP 161 (standard SNMP)
- **Library**: [0neblock/SNMP_Agent](https://github.com/0neblock/Arduino_SNMP)
- **Off by default**

## Quick Start

```bash
# Enable SNMP
set snmp on
reboot

# Verify from a host on the same network
snmpwalk -v2c -c public <device-ip> 1.3.6.1.4.1.99999
```

## CLI Commands

| Command | Description |
|---|---|
| `get snmp` | Show SNMP agent status (`on` / `off`) |
| `set snmp on` | Enable SNMP agent (restart required) |
| `set snmp off` | Disable SNMP agent (restart required) |
| `get snmp.community` | Show SNMP community string |
| `set snmp.community <string>` | Set community string (restart required, default: `public`) |

## OID Reference

All OIDs are under the private enterprise subtree `1.3.6.1.4.1.99999` (`.iso.org.dod.internet.private.enterprises.99999`).

Levels (queue lengths, percentages, voltages) are INTEGER. Running totals added from `.2.13.0` onwards are Counter32, so LibreNMS, Zabbix and similar tools graph them as rates. They start again from 0 after a reboot or `clear stats`. The older totals in `.2.x.0` and `.3.3.0` are still INTEGER.

### System (.1.x.0)

| OID | Type | Description |
|---|---|---|
| `.1.1.0` | INTEGER | System uptime (seconds) |
| `.1.2.0` | STRING | Firmware version |
| `.1.3.0` | STRING | Node name |
| `.1.4.0` | INTEGER | Battery voltage (mV), 0 when the board cannot measure it. Read every 30 s |
| `.1.5.0` | INTEGER | MCU die temperature (whole °C), -127 when the board has no sensor. Read every 30 s |
| `.1.6.0` | INTEGER | Reset reason code for the current boot (ESP-IDF `esp_reset_reason_t`: 1 power-on, 3 software, 4 panic, 5/6/7 watchdog, 9 brownout) |
| `.1.7.0` | STRING | Reset reason as text, for example `Brownout (low voltage)` |

### Radio (.2.x.0)

| OID | Type | Description |
|---|---|---|
| `.2.1.0` | INTEGER | Total packets received |
| `.2.2.0` | INTEGER | Total packets sent |
| `.2.3.0` | INTEGER | Receive errors (CRC failures, etc.) |
| `.2.4.0` | INTEGER | Noise floor (dBm) |
| `.2.5.0` | INTEGER | Last RSSI (dBm) |
| `.2.6.0` | INTEGER | Last SNR (dB x 4) |
| `.2.7.0` | INTEGER | Flood packets sent |
| `.2.8.0` | INTEGER | Direct packets sent |
| `.2.9.0` | INTEGER | Flood packets received |
| `.2.10.0` | INTEGER | Direct packets received |
| `.2.11.0` | INTEGER | Total air time (seconds) |
| `.2.12.0` | INTEGER | Packets waiting in the transmit queue |
| `.2.13.0` | Counter32 | Duplicate flood packets received |
| `.2.14.0` | Counter32 | Duplicate direct packets received |
| `.2.15.0` | Counter32 | Total receive air time (seconds) |
| `.2.16.0` | INTEGER | Transmit duty cycle over the last ~60 s (%) |
| `.2.17.0` | INTEGER | Error flags since boot (bitmask: 1 packet pool full, 2 CAD timeout, 4 start-RX timeout, 8 radio watchdog fired) |

### MQTT (.3.x.0)

| OID | Type | Description |
|---|---|---|
| `.3.1.0` | INTEGER | Connected MQTT slot count |
| `.3.2.0` | INTEGER | Packet queue depth |
| `.3.3.0` | INTEGER | Skipped publishes (memory pressure) |
| `.3.4.0` | INTEGER | Clock set by NTP (1 yes, 0 no) |

### Memory (.4.x.0)

| OID | Type | Description |
|---|---|---|
| `.4.1.0` | INTEGER | Free heap (bytes) |
| `.4.2.0` | INTEGER | Max allocatable block (bytes) |
| `.4.3.0` | INTEGER | Free internal RAM (bytes) |
| `.4.4.0` | INTEGER | Free PSRAM (bytes, 0 if no PSRAM) |
| `.4.5.0` | INTEGER | Lowest free heap since boot (bytes). A value that keeps dropping points to a leak |

### Network (.5.x.0)

| OID | Type | Description |
|---|---|---|
| `.5.1.0` | INTEGER | WiFi RSSI (dBm) |

### Duty gate (.6.x.0, repeater only)

Region gating state (`set dc.gate on`). Not present on room servers.

| OID | Type | Description |
|---|---|---|
| `.6.1.0` | INTEGER | Gating enabled (1 yes, 0 no) |
| `.6.2.0` | INTEGER | Outer region layers currently gated (0 = none) |
| `.6.3.0` | INTEGER | Highest layer that can be gated |
| `.6.4.0` | INTEGER | Duty cycle threshold that starts gating (%) |
| `.6.5.0` | INTEGER | Hysteresis: layers come back below threshold minus this (%) |

### Filter (.7.x.0, repeater only)

Packet filter drop totals since boot or the last `clear stats`. In dry-run mode the drops are counted but the packets are still forwarded. Not present on room servers.

| OID | Type | Description |
|---|---|---|
| `.7.1.0` | INTEGER | Filter enabled (1 yes, 0 no) |
| `.7.2.0` | INTEGER | Dry-run mode (1 yes, 0 no) |
| `.7.3.0` | Counter32 | Hop-limit drops, all payload types |
| `.7.4.0` | Counter32 | Rate-limit drops, all payload types |
| `.7.5.0` | Counter32 | Blocked-channel drops |
| `.7.6.0` | Counter32 | Path-hash size drops |
| `.7.7.0` | Counter32 | Malformed packet drops |
| `.7.8.0` | Counter32 | Advert window drops |
| `.7.9.0` | Counter32 | Path-prefix block list drops |
| `.7.10.0` | Counter32 | Sender rule drops |
| `.7.11.0` | Counter32 | Text rule drops |
| `.7.12.0` | Counter32 | Message age drops |
| `.7.13.0` | Counter32 | Estimated transmit air time the drops saved (ms) |

### Neighbours (.8.x.0)

| OID | Type | Description |
|---|---|---|
| `.8.1.0` | INTEGER | Neighbours in the neighbour table (always 0 on room servers without PSRAM, which keep no table) |

## Example Output

```
$ snmpwalk -v2c -c public 192.168.50.95 1.3.6.1.4.1.99999

SNMPv2-SMI::enterprises.99999.1.1.0 = INTEGER: 18        # uptime 18s
SNMPv2-SMI::enterprises.99999.1.2.0 = STRING: "v1.14.1"  # firmware
SNMPv2-SMI::enterprises.99999.1.3.0 = STRING: "Heltec Repeater"
SNMPv2-SMI::enterprises.99999.1.4.0 = INTEGER: 4105      # battery 4.105 V
SNMPv2-SMI::enterprises.99999.1.5.0 = INTEGER: 41        # MCU 41 °C
SNMPv2-SMI::enterprises.99999.1.6.0 = INTEGER: 1
SNMPv2-SMI::enterprises.99999.1.7.0 = STRING: "Power-on reset"
SNMPv2-SMI::enterprises.99999.2.1.0 = INTEGER: 3         # 3 packets received
SNMPv2-SMI::enterprises.99999.2.4.0 = INTEGER: -96       # noise floor -96 dBm
SNMPv2-SMI::enterprises.99999.2.5.0 = INTEGER: -25       # last RSSI -25 dBm
SNMPv2-SMI::enterprises.99999.4.1.0 = INTEGER: 199612    # ~195 KB free heap
SNMPv2-SMI::enterprises.99999.4.2.0 = INTEGER: 188404    # ~184 KB max alloc
SNMPv2-SMI::enterprises.99999.5.1.0 = INTEGER: -22       # WiFi RSSI -22 dBm
SNMPv2-SMI::enterprises.99999.7.4.0 = Counter32: 12      # 12 rate-limit drops
SNMPv2-SMI::enterprises.99999.8.1.0 = INTEGER: 7         # 7 neighbours
```

## Architecture

The SNMP agent runs inside the existing MQTT FreeRTOS task on Core 0 (the WiFi/network core). No additional task is created.

- **Radio, mesh, duty gate, filter and neighbour stats** are pushed from Core 1 (mesh task) every 2 seconds by `MyMesh::pushSNMPStats()`
- **Battery voltage and MCU temperature** are read on Core 1 every 30 seconds (some boards block on the ADC read)
- **Reset reason** is set once at startup
- **MQTT stats and NTP state** are updated from Core 0 each MQTT task loop iteration
- **Memory and WiFi stats** are read directly in the SNMP loop on Core 0

The agent starts once the network connects with SNMP enabled, and uses about 2-3 KB of RAM.

## Build Configuration

`WITH_SNMP=1` is set on most ESP32 observer environments. Check a board with
`grep -n WITH_SNMP variants/<board>/platformio.ini`. To add it to another observer env:

```ini
build_flags =
  ...
  -D WITH_SNMP=1

build_src_filter =
  ...
  +<helpers/SNMPAgent.cpp>

lib_deps =
  ...
  0neblock/SNMP_Agent
```

## Files

| File | Description |
|---|---|
| `src/helpers/SNMPAgent.h` | SNMP agent wrapper class declaration |
| `src/helpers/SNMPAgent.cpp` | OID registration and stats update implementation |
| `src/helpers/CommonCLI.h` | `MQTTPrefs` (`snmp_enabled`, `snmp_community`) |
| `src/helpers/CommonCLI_Observer.cpp` | CLI commands |
| `src/helpers/bridges/MQTTBridge.cpp` | SNMP loop integration in `mqttTaskLoop()` |
| `examples/simple_repeater/MyMesh.cpp` | Stats push (`pushSNMPStats`) and agent initialization |
| `examples/simple_room_server/MyMesh.cpp` | Stats push for room servers (no duty gate or filter groups) |

## Private Enterprise Number

The OIDs use `99999`, a placeholder. A real Private Enterprise Number is free from
[IANA](https://pen.iana.org/pen/PenApplication.page).
