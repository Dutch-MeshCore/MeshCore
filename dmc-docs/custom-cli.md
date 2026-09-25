# Custom CLI

Dit zijn de commando's die DMC bovenop upstream MeshCore toevoegt. Voor de volledige
CLI-referentie zie
[`docs/cli_commands.md`](https://github.com/Dutch-MeshCore/MeshCore/blob/dmc-dev/docs/cli_commands.md)
en
[`docs/packet_filter_reference.md`](https://github.com/Dutch-MeshCore/MeshCore/blob/dmc-dev/docs/packet_filter_reference.md).

## Packet filter

`filter ...` (alleen repeater). Standaard **uit**. Direct-routed pakketten en
ACL-contactpakketten worden overgeslagen. De status wordt bewaard in `/filter_prefs`.

- `filter`: status en totalen per reden
- `filter help`
- `filter on` / `filter off`
- `filter reset`
- `filter dryrun on|off` (drops tellen maar wél doorsturen; statusregel eindigt op `(dry-run)`)
- `filter types`
- `filter count`
- `filter hops <type> <max_hops>` (type `00`-`11`, hops `0`-`64`)
- `filter rate <type> <limit> <seconds> [soft]` (`soft` = probabilistische zachte afkap,
  moet kleiner zijn dan `limit`; `0` = harde afkap)
- `filter channel list|add|remove <#naam|Public>` (max. 16, alleen GRP_TXT)
- `filter hash <min_bytes>` (1-3, minimale path-hashgrootte)
- `filter malformed on|off` (UTF-8-/structuurvalidatie van publieke GRP_TXT)
- `filter advert <uren>` (0-720; elke node's advert hooguit één keer per venster doorgestuurd,
  `0` = uit) / `filter advert clear`
- `filter path list|add|remove <hex>` (max. 8 prefixen van 2-8 hexcijfers; blokkeert alles wat
  via een repeater met dat ID-prefix kwam)
- `filter sender add <naam> [secs] [prob]` / `remove` / `list` (max. 8; naam exact, `Bot*` = prefix;
  `secs` 0 = blokkeren, anders 1 bericht per `secs` doorlaten; `prob` 1-100 = aandeel dat de regel beslist)
- `filter text add <patroon> [secs] [prob]` / `remove` / `list` (max. 8; substring, `^` = begin van het bericht)
- `filter watch add|remove|list <#naam>` (max. 4 kanalen die de regels mogen lezen, naast Public)
- `filter stats <topic>` met topic = `hops|rate|hash|channel|malformed|top|advert|path|air|sender|text`
  (`air` = geschatte bespaarde zendtijd)

### Pakkettypes

| ID | Type | ID | Type |
| --- | --- | --- | --- |
| `00` | REQ | `06` | GRP_DATA |
| `01` | RESPONSE | `07` | ANON_REQ |
| `02` | TXT_MSG | `08` | PATH |
| `03` | ACK | `09` | TRACE |
| `04` | ADVERT | `10` | MULTIPART |
| `05` | GRP_TXT | `11` | CONTROL |

## Duty-cycle region gating

`dc.gate.*`. Standaard **uit**. Werpt inter-regio-floodverkeer af wanneer de eigen
TX-duty-cycle van de repeater hoog is. Transient: wordt nooit naar de regioconfiguratie
weggeschreven. Draait vóór de packet filter, dus deze drops verschijnen niet in
`filter stats`.

- `set dc.gate <0|1>` / `get dc.gate`
- `set dc.gate.thresh <1-100>` / `get dc.gate.thresh` (standaard 70)
- `set dc.gate.hyst <0-50>` / `get dc.gate.hyst` (standaard 10)
- `get dc.gate.status` (bijv. `duty 74%, gate level 2/4`)

## Duty cycle

DMC voegde `auto` toe en veranderde de standaard van 50% naar `auto`.

- `set dutycycle auto`: leest de sub-bandtabel van ETSI EN 300 220-2 voor 863-870 MHz en
  herberekent bij `set freq` / `set radio`. Standaard.
- Een expliciet percentage of `set af` zet `auto` uit (`af`/airtime-factor is het
  verouderde upstream-mechanisme).
- `get dutycycle`: toont de huidige instelling.

## Observer / MQTT

Alleen op ESP32 observer-builds.

- **Wifi**: `set wifi.ssid <ssid>`, `set wifi.pwd <pwd>`, `set wifi.powersave ...`,
  `get wifi.status`
- **MQTT-identiteit en -routing**: `set mqtt.iata <code>`, `set mqtt.email <email>`,
  `set mqtt.owner <64-hex>`, `set mqtt<N>.preset <naam>` / `set mqtt<N>.server <...>`,
  `set mqtt.rx|tx <on|off|advert>`, `get mqtt.status`
- **MQTT extra**: `set mqtt.config <on|off>` (publiceert niet-gevoelige node-config naar het
  `config`-topic, opgeslagen in `/mqtt.json`), `set mqtt.filter.interval <60-600>`,
  `set mqtt.neighbors <on|off>` + `set mqtt.neighbors.interval <uren>` (alleen met PSRAM)
- **NTP**: `set mqtt.ntp <host|none>`, `get mqtt.ntp.diag`

Gepubliceerde topics (suffixen op een gemeenschappelijk prefix): `status`, `packets`,
`filter`, `neighbors`, `config`.
