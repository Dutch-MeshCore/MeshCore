# Custom CLI

Dies sind die Befehle, die DMC zusätzlich zu Upstream-MeshCore bereitstellt. Die
vollständige CLI-Referenz findest du in
[`docs/cli_commands.md`](https://github.com/Dutch-MeshCore/MeshCore/blob/dmc-dev/docs/cli_commands.md)
und
[`docs/packet_filter_reference.md`](https://github.com/Dutch-MeshCore/MeshCore/blob/dmc-dev/docs/packet_filter_reference.md).

## Packet-Filter

`filter ...` (nur Repeater). Standardmäßig **aus**. Direkt geroutete Pakete und
ACL-Kontaktpakete werden übersprungen. Der Zustand wird in `/filter_prefs` gespeichert.

- `filter`: Status und Summen je Grund
- `filter help`
- `filter on` / `filter off`
- `filter reset`
- `filter types`
- `filter count`
- `filter hops <type> <max_hops>` (Typ `00`-`11`, Hops `0`-`64`)
- `filter rate <type> <limit> <seconds> [soft]` (`soft` = probabilistische weiche Kappung,
  muss kleiner als `limit` sein; `0` = harte Kappung)
- `filter channel list|add|remove <#name|Public>` (bis zu 16, nur GRP_TXT)
- `filter hash <min_bytes>` (1-3, minimale Path-Hash-Größe)
- `filter malformed on|off` (UTF-8-/Strukturvalidierung von öffentlichem GRP_TXT)
- `filter stats <topic>` mit topic = `hops|rate|hash|channel|malformed|top`

### Pakettypen

| ID | Typ | ID | Typ |
| --- | --- | --- | --- |
| `00` | REQ | `06` | GRP_DATA |
| `01` | RESPONSE | `07` | ANON_REQ |
| `02` | TXT_MSG | `08` | PATH |
| `03` | ACK | `09` | TRACE |
| `04` | ADVERT | `10` | MULTIPART |
| `05` | GRP_TXT | `11` | CONTROL |

## Duty-Cycle-Region-Gating

`dc.gate.*`. Standardmäßig **aus**. Wirft Inter-Regions-Flood-Verkehr ab, wenn der eigene
TX-Duty-Cycle des Repeaters hoch ist. Transient: wird nie in die Regionskonfiguration
geschrieben. Läuft vor dem Packet-Filter, daher erscheinen diese Drops nicht in
`filter stats`.

- `set dc.gate <0|1>` / `get dc.gate`
- `set dc.gate.thresh <1-100>` / `get dc.gate.thresh` (Standard 70)
- `set dc.gate.hyst <0-50>` / `get dc.gate.hyst` (Standard 10)
- `get dc.gate.status` (z. B. `duty 74%, gate level 2/4`)

## Duty-Cycle

DMC ergänzte `auto` und änderte die Vorgabe von 50% auf `auto`.

- `set dutycycle auto`: liest die Sub-Band-Tabelle von ETSI EN 300 220-2 für 863-870 MHz und
  berechnet bei `set freq` / `set radio` neu. Standard.
- Ein explizites Prozent oder `set af` schaltet `auto` aus (`af`/Airtime-Factor ist der
  veraltete Upstream-Mechanismus).
- `get dutycycle`: zeigt die aktuelle Einstellung.

## Observer / MQTT

Nur auf ESP32-Observer-Builds.

- **WiFi**: `set wifi.ssid <ssid>`, `set wifi.pwd <pwd>`, `set wifi.powersave ...`,
  `get wifi.status`
- **MQTT-Identität und -Routing**: `set mqtt.iata <code>`, `set mqtt.email <email>`,
  `set mqtt.owner <64-hex>`, `set mqtt<N>.preset <name>` / `set mqtt<N>.server <...>`,
  `set mqtt.rx|tx <on|off|advert>`, `get mqtt.status`
- **MQTT-Extras**: `set mqtt.config <on|off>` (veröffentlicht nicht sensible Node-Konfig ins
  `config`-Topic, gespeichert in `/mqtt.json`), `set mqtt.filter.interval <60-600>`,
  `set mqtt.neighbors <on|off>` + `set mqtt.neighbors.interval <Stunden>` (nur mit PSRAM)
- **NTP**: `set mqtt.ntp <host|none>`, `get mqtt.ntp.diag`

Veröffentlichte Topics (Suffixe an einem gemeinsamen Prefix): `status`, `packets`,
`filter`, `neighbors`, `config`.
