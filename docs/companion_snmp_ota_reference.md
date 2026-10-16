# MeshCore Companion SNMP, OTA and MAC

## Overview

DMC-only network extras in the companion firmware:

* **SNMP**: a read-only SNMPv2c agent on UDP 161, with the same OID layout as the
  DMC observer firmware, so one poller template (LibreNMS, Zabbix, RTFM-EV) reads
  companions and observers alike.
* **OTA**: `ota check` / `ota update` pull the published build for this board from
  the DutchMeshCore OTA server (`ota.dutchmeshcore.nl/companion`), the same server
  the observer firmware updates from.
* **MAC**: `get mac`, `get wifi.mac`, `get ble.mac` show the factory MAC addresses,
  for DHCP reservations and MAC filtering.

Commands are sent through the app's command console (`CMD_RUN_CLI_COMMAND`), USB
serial, or remote CLI. Settings are saved to `/cnet.json`.

---

# Which builds have what

SNMP and OTA need a Wi-Fi network, so they exist on ESP32 builds that can join one:

| Build | SNMP | OTA | MAC |
| --- | --- | --- | --- |
| ESP32 `*_companion_radio_wifi` | yes | yes | yes |
| ESP32 `*_companion_radio_ble` with Wi-Fi (Heltec V3, WSL3, V4, V4 R8, Wireless Paper, RAK3112, ThinkNode M7, Xiao S3, Xiao S3 WIO) | yes | yes | yes |
| Other ESP32 `*_companion_radio_ble` | no | no | yes |
| nRF52, RP2040 | no | no | `n/a` |

On the BLE builds with Wi-Fi, Wi-Fi is off until it is set up:
`set wifi.ssid <name>`, `set wifi.pwd <password>`, `set wifi.enabled 1` (a number:
`on` reads as 0 and turns Wi-Fi off), then reboot.

---

# SNMP

Off by default. Until `set snmp on` the agent is never started, so it costs no RAM.
When on, it runs whenever Wi-Fi has an address and stops while the link is down.

| Command | |
| --- | --- |
| `get snmp` | `on` or `off` |
| `set snmp on\|off` | start or stop the agent (applies live) |
| `get snmp.status` | `off`, `on, waiting for Wi-Fi` or `running on udp/161` |
| `get snmp.community` | the community string (default `public`); not over remote CLI |
| `set snmp.community <text>` | 1 to 32 characters, applies to the next request |

The community string is the only access control: change it from `public` on any
network you do not fully trust. The agent is read-only.

## OIDs

Base `.1.3.6.1.4.1.99999` (the same temporary enterprise number as the observer).
Shared numbers keep the observer's meaning, type and unit. Anything the companion
has no data for is not registered and reads `noSuchObject`, never a fake 0.

| OID | Type | Value |
| --- | --- | --- |
| `.1.1.0` | Integer | uptime (s) |
| `.1.2.0` | String | firmware version |
| `.1.3.0` | String | node name |
| `.1.4.0` | Integer | battery (mV), read every 30 s |
| `.1.5.0` | Integer | MCU temperature (°C), `-127` when the board has no sensor |
| `.1.6.0` | Integer | reset reason code |
| `.1.7.0` | String | reset reason |
| `.2.1.0` - `.2.3.0` | Integer | packets received, sent, receive errors |
| `.2.4.0` | Integer | noise floor (dBm) |
| `.2.5.0` | Integer | last RSSI (dBm) |
| `.2.6.0` | Integer | last SNR × 4 |
| `.2.7.0` - `.2.10.0` | Integer | sent flood, sent direct, received flood, received direct |
| `.2.11.0` | Integer | TX air time (s) |
| `.2.12.0` | Integer | TX queue length |
| `.2.13.0`, `.2.14.0` | Counter32 | flood, direct duplicates |
| `.2.15.0` | Counter32 | RX air time (s) |
| `.2.17.0` | Integer | error flags (1 queue full, 2 CAD timeout, 4 start RX timeout) |
| `.4.1.0` - `.4.5.0` | Integer | free heap, largest block, free internal, free PSRAM, lowest free heap |
| `.5.1.0` | Integer | Wi-Fi RSSI (dBm), `-127` when not connected |
| `.9.1.0` | Integer | app connected over Wi-Fi (0/1) |
| `.9.3.0` | Integer | contacts |
| `.9.4.0` | Integer | channels |
| `.10.1.0` | Integer | receive filter on (0/1) |
| `.10.2.0` - `.10.6.0` | Counter32 | receive filter: malformed, age, path, sender, text |

Not registered: `.2.16` (TX duty cycle), `.3` (MQTT and NTP), `.6` (duty gate),
`.7` (repeater packet filter), `.8` (neighbours), `.9.2` (meshcomod WebSocket clients).

```
snmpwalk -v2c -c public <companion-ip> .1.3.6.1.4.1.99999
```

---

# OTA

| Command | |
| --- | --- |
| `ota check` | is a newer build published? |
| `ota update` | check, then download, flash and reboot |
| `ota status` | `idle`, `starting`, or the last failure |
| `ota branch` | the channel in use |
| `ota branch stable\|dev\|default` | pull from the stable or dev channel; `default` = the channel the build was made for |

`ota update` answers straight away, then starts the download a few seconds later so
the reply still reaches the app. Progress is printed on USB serial; the device
reboots into the new build when it is written. A failure leaves the running build
in place, and `ota status` shows why.

How it works:

1. The device reads `<base>/<env>.json`, where `<base>` is
   `https://ota.dutchmeshcore.nl/companion/v` (stable) or `.../companion/dev/v`
   (dev) and `<env>` is the PlatformIO env name baked in by `build.sh`. The server
   builds these manifests from the `dmc-companion-*` releases.
2. Versions compare numerically (`v1.18.0` < `v1.18.1`). The same version from
   another commit counts as an update (a newer dev build). It never downgrades.
3. `ota check` reads the manifest over plain HTTP. `ota update` reads it again over
   HTTPS and downloads the image over HTTPS. Both are verified against a short list
   of root CAs in `CompanionOtaCerts.h` (regenerate with
   `scripts/gen_companion_ota_certs.py`).
4. The image must sit under the same product's `fw/` path on the server.
5. The update is refused, and needs a USB flash, when the new build has a different
   partition table (the manifest's `partSig` against the flashed table) or the
   board has no second app slot.

A build made without `build.sh` has no server bases and answers
`OTA not configured`.

---

# MAC

| Command | Reply |
| --- | --- |
| `get mac` | `> wifi AA:BB:CC:DD:EE:FF, ble AA:BB:CC:DD:EE:01` |
| `get wifi.mac` | Wi-Fi station MAC |
| `get ble.mac` | Bluetooth MAC |

Read from the chip's eFuse, so they answer with Wi-Fi or BLE off. Non-ESP32 builds
reply `n/a`.
