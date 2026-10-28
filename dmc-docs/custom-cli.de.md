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

## Companion-Filter

`filter ...` auf der **Companion**-Firmware: blendet Müll und unerwünschte Kanalnachrichten in der
App aus. Wird über die Befehlskonsole der App, USB-Seriell oder Remote-CLI gesendet. Standardmäßig
**aus**; Direktnachrichten werden nie gefiltert. Gespeichert in `/cfilter.json`. Siehe
[`docs/companion_filter_reference.md`](https://github.com/Dutch-MeshCore/MeshCore/blob/dmc-dev/docs/companion_filter_reference.md).

- `filter`: Status und Summen pro Grund
- `filter help` / `filter on` / `filter off` / `filter reset`
- `filter malformed on|off` (falscher Zeitstempel, leerer Text oder ungültiges UTF-8, auf jedem eigenen Kanal)
- `filter age <Minuten>|off` (1-10080; Gruppennachrichten, die älter sind)
- `filter path list|add|remove <hex>` (bis zu 8; Adverts und Gruppenverkehr über diesen Repeater)
- `filter sender add <Name> [secs] [prob]` / `remove` / `list` (bis zu 8; `Bot*` = Präfix)
- `filter text add <Muster> [secs] [prob]` / `remove` / `list` (bis zu 8; `^` = Anfang der Nachricht)
- `filter stats malformed|age|path|sender|text`

Mit aktiviertem Repeat werden malformed/age/path-Nachrichten auch nicht weitergeleitet;
sender/text-Regeln blenden eine Nachricht nur für dich aus, sie wird für andere weiterhin weitergeleitet.

## Companion SNMP, OTA und MAC

Auf **Companion**-Builds, die sich mit WLAN verbinden können (ESP32 `*_companion_radio_wifi` und
die ESP32-`*_companion_radio_ble`-Boards mit WLAN). Gespeichert in `/cnet.json`. Siehe
[`docs/companion_snmp_ota_reference.md`](https://github.com/Dutch-MeshCore/MeshCore/blob/dmc-dev/docs/companion_snmp_ota_reference.md).

- `get snmp` / `set snmp on|off` (standardmäßig **aus**; UDP 161, nur lesend, OID-Layout des Observers)
- `get snmp.status` / `get snmp.community` / `set snmp.community <Text>` (Standard `public`)
- `ota check` / `ota update` / `ota status` (DutchMeshCore-OTA-Server, HTTPS)
- `ota branch` / `ota branch stable|dev|default`
- `get mac` / `get wifi.mac` / `get ble.mac` (jeder ESP32-Build)

Kein OTA auf Boards mit 4 MB Flash (ThinkNode M2/M5, Xiao C3, LilyGo T-LoRa V2.1). Eine neue Partitionstabelle oder ein Board ohne zweiten App-Slot braucht weiterhin einen USB-Flash.

## Duty-Cycle-Region-Gating

`dc.gate.*`. Standardmäßig **aus**. Wirft Inter-Regions-Flood-Verkehr ab, wenn der
Repeater einen großen Teil der Zeit sendet. Transient: wird nie in
die Regionskonfiguration geschrieben. Läuft vor dem Packet-Filter, daher erscheinen diese
Drops nicht in `filter stats`.

- `set dc.gate <on|off>` / `get dc.gate` (`1`/`0` funktionieren auch)
- `set dc.gate.thresh <1-100>` / `get dc.gate.thresh` (Standard 35)
- `set dc.gate.hyst <0-50>` / `get dc.gate.hyst` (Standard 10)
- `get dc.gate.status` (z. B. `duty 41%, gate level 2/4`)

Schwelle, Hysterese und `duty` sind der **echte TX-Duty-Cycle**: der Anteil der Uhrzeit,
in dem der Repeater in den letzten 60 Sekunden gesendet hat. Das ist nicht dasselbe wie
das Duty-Cycle-Limit (`set dutycycle`): Es gilt pro Stunde, aus einer Sendezeitreserve,
die der Repeater in Spitzen aufbrauchen darf. Mit `dutycycle 10` (360 Sekunden pro
Stunde) bleibt normaler Verkehr deutlich unter 35 %; ein Repeater, der 35 % der Zeit
sendet, hat die Reserve in etwa 24 Minuten aufgebraucht, und das Gating greift vorher
ein. Auf einem 1-%-Subband ist `10`-`15` eine bessere Schwelle.

Frühere DMC-Builds maßen einen Prozentsatz des Duty-Cycle-Budgets (Standard 70). Dieser
Wert wird nach dem Update nicht übernommen; die Schwelle beginnt dann bei 35.

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
- **Netzwerkverbindung**: `get link.status` (Medium, Verbindung, IP, Signal und Uptime;
  ersetzt `get wifi.status`; zeigt auch die IPv6-Adresse, wenn der Node eine hat), `get link.diag` (warum Ethernet oder WiFi gewählt wurde,
  ThinkNode-M7-Ethernet-Builds), `get link.dns` (aktive DNS-Resolver und Gateway)
- **Advert-Prüfung**: `set mqtt.advert.verify <on|off>` / `get mqtt.advert.verify`
  (Adverts mit ungültiger Signatur nicht veröffentlichen; Standard `off`)
- **Manuelles OTA**: `stop ota` (stoppt einen untätigen `start ota`-Webserver und gibt
  Port 80 frei; verweigert während eines Uploads)

Veröffentlichte Topics (Suffixe an einem gemeinsamen Prefix): `status`, `packets`,
`filter`, `neighbors`, `config`.
