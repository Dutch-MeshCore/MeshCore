# Custom CLI

These are the commands DMC adds on top of upstream MeshCore. For the full CLI reference see
[`docs/cli_commands.md`](https://github.com/Dutch-MeshCore/MeshCore/blob/dmc-dev/docs/cli_commands.md)
and
[`docs/packet_filter_reference.md`](https://github.com/Dutch-MeshCore/MeshCore/blob/dmc-dev/docs/packet_filter_reference.md).

## Packet filter

`filter ...` (repeater only). **Off** by default. Direct-routed packets and ACL-contact
packets bypass it. State is persisted to `/filter_prefs`.

- `filter`: status and per-reason totals
- `filter help`
- `filter on` / `filter off`
- `filter reset`
- `filter types`
- `filter count`
- `filter hops <type> <max_hops>` (type `00`-`11`, hops `0`-`64`)
- `filter rate <type> <limit> <seconds> [soft]` (`soft` = probabilistic soft cutoff, must be
  less than `limit`; `0` = hard cutoff)
- `filter channel list|add|remove <#name|Public>` (up to 16, GRP_TXT only)
- `filter hash <min_bytes>` (1-3, minimum path-hash size)
- `filter malformed on|off` (UTF-8 / structure validation of public GRP_TXT)
- `filter stats <topic>` where topic = `hops|rate|hash|channel|malformed|top`

### Packet types

| ID | Type | ID | Type |
| --- | --- | --- | --- |
| `00` | REQ | `06` | GRP_DATA |
| `01` | RESPONSE | `07` | ANON_REQ |
| `02` | TXT_MSG | `08` | PATH |
| `03` | ACK | `09` | TRACE |
| `04` | ADVERT | `10` | MULTIPART |
| `05` | GRP_TXT | `11` | CONTROL |

## Duty-cycle region gating

`dc.gate.*`. **Off** by default. Sheds inter-region flood traffic when the repeater's own
TX duty cycle is high. Transient: never written to the region config. Runs before the
packet filter, so these drops do not appear in `filter stats`.

- `set dc.gate <0|1>` / `get dc.gate`
- `set dc.gate.thresh <1-100>` / `get dc.gate.thresh` (default 70)
- `set dc.gate.hyst <0-50>` / `get dc.gate.hyst` (default 10)
- `get dc.gate.status` (e.g. `duty 74%, gate level 2/4`)

## Duty cycle

DMC added `auto` and changed the default from 50% to `auto`.

- `set dutycycle auto`: reads the ETSI EN 300 220-2 sub-band table for 863-870 MHz and
  re-derives on `set freq` / `set radio`. Default.
- Setting an explicit percentage or `set af` turns `auto` off (`af`/airtime-factor is the
  deprecated upstream mechanism).
- `get dutycycle`: shows the current setting.

## Observer / MQTT

ESP32 observer builds only.

- **WiFi**: `set wifi.ssid <ssid>`, `set wifi.pwd <pwd>`, `set wifi.powersave ...`,
  `get wifi.status`
- **MQTT identity and routing**: `set mqtt.iata <code>`, `set mqtt.email <email>`,
  `set mqtt.owner <64-hex>`, `set mqtt<N>.preset <name>` / `set mqtt<N>.server <...>`,
  `set mqtt.rx|tx <on|off|advert>`, `get mqtt.status`
- **MQTT extras**: `set mqtt.config <on|off>` (publishes non-sensitive node config to the
  `config` topic, stored in `/mqtt.json`), `set mqtt.filter.interval <60-600>`,
  `set mqtt.neighbors <on|off>` + `set mqtt.neighbors.interval <hours>` (PSRAM only)
- **NTP**: `set mqtt.ntp <host|none>`, `get mqtt.ntp.diag`

Published topics (suffixes off a common prefix): `status`, `packets`, `filter`,
`neighbors`, `config`.
