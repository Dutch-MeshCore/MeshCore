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
- `filter dryrun on|off` (Drops zählen, aber weiterhin weiterleiten; Statuszeile endet auf `(dry-run)`)
- `filter types`
- `filter count`
- `filter hops <type> <max_hops>` (Typ `00`-`11`, Hops `0`-`64`)
- `filter rate <type> <limit> <seconds> [soft]` (`soft` = probabilistische weiche Kappung,
  muss kleiner als `limit` sein; `0` = harte Kappung)
- `filter channel list|add|remove <#name|Public>` (bis zu 16, nur GRP_TXT)
- `filter hash <min_bytes>` (1-3, minimale Path-Hash-Größe)
- `filter malformed on|off` (UTF-8-/Strukturvalidierung von öffentlichem GRP_TXT)
- `filter advert <stunden>` (0-720; das Advert jedes Knotens höchstens einmal pro Fenster
  weitergeleitet, `0` = aus) / `filter advert clear`
- `filter path list|add|remove <hex>` (bis zu 8 Präfixe mit 2-8 Hex-Ziffern; verwirft alles,
  was über einen Repeater mit diesem ID-Präfix kam)
- `filter sender add <name> [secs] [prob]` / `remove` / `list` (bis zu 8; Name exakt, `Bot*` = Präfix;
  `secs` 0 = blockieren, sonst eine Nachricht pro `secs` durchlassen; `prob` 1-100 = Anteil, den die Regel entscheidet)
- `filter text add <muster> [secs] [prob]` / `remove` / `list` (bis zu 8; Teilstring, `^` = Nachrichtenanfang)
- `filter watch add|remove|list <#name>` (bis zu 4 Kanäle, die die Regeln lesen dürfen, neben Public)
- `filter age <Minuten>|off` (1-10080; verwirft Gruppennachrichten auf Public und beobachteten
  Kanälen, die älter sind, nach der Uhr des Repeaters; inaktiv, solange die Uhr nicht gestellt ist)
- `filter stats <topic>` mit topic = `hops|rate|hash|channel|malformed|top|advert|path|air|sender|text|age`
  (`air` = geschätzte eingesparte Sendezeit)

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
