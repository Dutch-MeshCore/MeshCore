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
- `filter age <minuten>|off` (1-10080; groepsberichten op Public en gevolgde kanalen die ouder zijn
  dan dit worden gedropt, volgens de klok van de repeater; doet niets zolang de klok niet is gezet)
- `filter stats <topic>` met topic = `hops|rate|hash|channel|malformed|top|advert|path|air|sender|text|age`
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

## Companion filter

`filter ...` op de **companion**-firmware: verbergt rommel en ongewenste kanaalberichten voor de
app. Te versturen via de commandoconsole van de app, USB-serieel of remote CLI. Standaard **uit**;
directe berichten worden nooit gefilterd. Opgeslagen in `/cfilter.json`. Zie
[`docs/companion_filter_reference.md`](https://github.com/Dutch-MeshCore/MeshCore/blob/dmc-dev/docs/companion_filter_reference.md).

- `filter`: status en totalen per reden
- `filter help` / `filter on` / `filter off` / `filter reset`
- `filter malformed on|off` (foute tijdstempel, lege tekst of ongeldige UTF-8, op elk kanaal dat je hebt)
- `filter age <minuten>|off` (1-10080; groepsberichten ouder dan dit)
- `filter path list|add|remove <hex>` (max. 8; adverts en groepsverkeer via die repeater)
- `filter sender add <naam> [secs] [prob]` / `remove` / `list` (max. 8; `Bot*` = prefix)
- `filter text add <patroon> [secs] [prob]` / `remove` / `list` (max. 8; `^` = begin van het bericht)
- `filter stats malformed|age|path|sender|text`

Met repeat aan worden malformed/age/path-berichten ook niet doorgestuurd; sender/text-regels
verbergen een bericht alleen voor jou en het wordt nog steeds doorgestuurd voor anderen.

## Companion SNMP, OTA en MAC

Op **companion**-builds die met wifi kunnen verbinden (ESP32 `*_companion_radio_wifi`, en de
ESP32 `*_companion_radio_ble`-boards met wifi). Opgeslagen in `/cnet.json`. Zie
[`docs/companion_snmp_ota_reference.md`](https://github.com/Dutch-MeshCore/MeshCore/blob/dmc-dev/docs/companion_snmp_ota_reference.md).

- `get snmp` / `set snmp on|off` (standaard **uit**; UDP 161, alleen lezen, OID-indeling van de observer)
- `get snmp.status` / `get snmp.community` / `set snmp.community <tekst>` (standaard `public`)
- `ota check` / `ota update` / `ota status` (DutchMeshCore OTA-server, HTTPS)
- `ota branch` / `ota branch stable|dev|default`
- `get mac` / `get wifi.mac` / `get ble.mac` (elke ESP32-build)

Geen OTA op boards met 4 MB flash (ThinkNode M2/M5, Xiao C3, LilyGo T-LoRa V2.1). Een nieuwe partitietabel, of een board zonder tweede app-slot, heeft nog steeds een USB-flash nodig.

## Duty-cycle region gating

`dc.gate.*`. Standaard **uit**. Werpt inter-regio-floodverkeer af wanneer de repeater
een groot deel van de tijd zendt. Transient: wordt nooit naar
de regioconfiguratie weggeschreven. Draait vóór de packet filter, dus deze drops
verschijnen niet in `filter stats`.

- `set dc.gate <on|off>` / `get dc.gate` (`1`/`0` werkt ook)
- `set dc.gate.thresh <1-100>` / `get dc.gate.thresh` (standaard 35)
- `set dc.gate.hyst <0-50>` / `get dc.gate.hyst` (standaard 10)
- `get dc.gate.status` (bijv. `duty 41%, gate level 2/4`)

De drempel, de hysterese en `duty` zijn de **echte TX-duty-cycle**: het deel van de
kloktijd dat de repeater de afgelopen 60 seconden zond. Dat is iets anders dan de
duty-cycle-limiet (`set dutycycle`): die geldt per uur, uit een zendtijdreserve die de
repeater in pieken mag opmaken. Met `dutycycle 10` (360 seconden per uur) blijft normaal
verkeer ruim onder 35%; een repeater die 35% van de tijd zendt, heeft de reserve in
ongeveer 24 minuten op, en de gating grijpt eerder in. Op een 1%-subband is `10`-`15`
een betere drempel.

Eerdere DMC-builds maten een percentage van het duty-cycle-budget (standaard 70). Die
waarde wordt na de update niet overgenomen; de drempel begint dan op 35.

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
- **Netwerkverbinding**: `get link.status` (medium, verbinding, IP, signaal en uptime;
  vervangt `get wifi.status`; toont ook het IPv6-adres als de node er een heeft), `get link.diag` (waarom Ethernet of wifi is gekozen,
  ThinkNode M7-Ethernet-builds), `get link.dns` (actieve DNS-resolvers en gateway)
- **Advertcontrole**: `set mqtt.advert.verify <on|off>` / `get mqtt.advert.verify`
  (publiceer geen adverts waarvan de handtekening niet klopt; standaard `off`)
- **Handmatige OTA**: `stop ota` (stopt een inactieve `start ota`-webserver en geeft
  poort 80 vrij; weigert tijdens een upload)

Gepubliceerde topics (suffixen op een gemeenschappelijk prefix): `status`, `packets`,
`filter`, `neighbors`, `config`.
