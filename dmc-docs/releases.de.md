# Releases

DMC-Firmware wird als fertige Binaries pro Board und Track veröffentlicht. Am einfachsten
flasht du mit dem Web-Flasher.

## Erste Schritte

1. Öffne den [Toolbox-Flasher](https://toolbox.dutchmeshcore.nl/#/flasher).
2. Wähle deinen Firmware-Typ (Track) und dein Board.
3. Wähle eine Version (standardmäßig die neueste).
4. Wähle einen Image-Typ: **Update** oder **Full Flash**.
5. Flashe und richte das Gerät danach über das [Webpanel](web-panel.md) oder die serielle
   Konsole ein.

## Firmware-Tracks

DMC hat drei Tracks, jeweils auf einem Branch basierend. Alle drei bauen Repeater-Firmware;
zwei davon bauen auch einen Companion:

- **Companion Filter** (`dmc-dev`): das Companion-Radio mit Empfangsfilter, dazu SNMP und
  OTA auf ESP32-Boards mit WiFi. Siehe [Custom CLI](custom-cli.md#companion-filter).
- **Companion Observer** (`dmc-observer-dev`): ein WiFi-Companion-Radio und ein
  MQTT-Observer in einem Image. Siehe [Companion Observer](#companion-observer) unten.

Der Companion von [Meshcomod (EV Fork)](https://github.com/Elektr0Vodka/meshcomod) ist
ebenfalls im Toolbox-Flasher (Pre-Release).

| Track (Branch) | Was es ist | Enthält |
| --- | --- | --- |
| `dmc-dev` | Offizielle Repeater-Firmware | Repeater + Filter; Companion Filter |
| `dmc-dev-packetlog` | Repeater-Firmware mit PacketLog | Repeater + Filter + Packet-Logging |
| `dmc-observer-dev` | Observer-Firmware | Repeater + Filter + On-Device-MQTT + OTA (kein PacketLog); Companion Observer |

Im Observer-Track bekommen Repeater alles; Room Server bekommen MQTT und OTA, aber keinen
Filter, da ein Room Server per Definition nicht repeatet.

## Stabile und Dev-Releases

- **Stabil**: ein Release pro Version, zum Beispiel `dmc-repeater-v1.17.1`.
- **Dev**: rollierende Pre-Releases, die vom Branch an Ort und Stelle neu gebaut werden:
  `dmc-repeater-dev`, `dmc-repeater-packetlog-dev` und `observer-mqtt-dev`. Sie laufen jetzt
  auf `v1.18.0` (Upstream-MeshCore v1.18.0, zusammengeführt am 2026-10-15). Ein Dev-Build
  meldet `v1.18.0-dev-dutchmeshcore.nl-<hash>` in `ver`; der Dateiname hat kein `-dev`.
  Der Toolbox-Flasher zeigt sie als `1.18.0-dev`.
- **Companions**: `dmc-companion-dev` (Companion Filter) und `dmc-companion-observer-dev`
  (Companion Observer) sind ebenfalls rollierende Dev-Releases; versionierte Releases heißen
  `dmc-companion-vX.Y.Z` und `dmc-companion-observer-vX.Y.Z`. Der Companion Observer meldet
  `vX.Y.Z[-dev]-dutchmeshcore.nl-comp-obs-<hash>`.

## Dateiauswahl

- `*.bin`: inkrementelles Update für ein Gerät, das bereits DMC-Firmware ausführt.
- `*-merged.bin`: vollständiges Flash-Image ab Adresse `0x0` für eine saubere Installation.

## Einrichtung nach dem Flashen

- **`dmc-dev` (Repeater)**: über die serielle Konsole oder das [Webpanel](web-panel.md)
  konfigurieren.
- **`dmc-dev-packetlog`**: wie der Repeater; keine zusätzliche Konfiguration für PacketLog.
- **`dmc-observer-dev` (Observer)**: WiFi einstellen und `set mqtt.iata <code>`, ein
  Broker-Preset wählen. Siehe [Custom CLI](custom-cli.md).
- **Companion Observer**: siehe nächster Abschnitt.

## Companion Observer

Ein Companion-Radio und ein MQTT-Observer in einem Image. Die MeshCore-App verbindet sich über
WiFi (TCP-Port 5000), während die Pakete, die der Node hört, an die eingestellten MQTT-Broker
gehen. **Noch nicht auf Hardware getestet.**

| Board | Env |
| --- | --- |
| Heltec V3 | `Heltec_v3_companion_observer_wifi` |
| Heltec V4 | `heltec_v4_companion_observer_wifi` |
| Heltec V4 Expansion Kit | `heltec_v4_expansionkit_companion_observer_wifi` |
| Xiao S3 WIO | `Xiao_S3_WIO_companion_observer_wifi` |
| Waveshare S3 LR1121 | `Waveshare_S3_LR1121_companion_observer_wifi` |

- **Einrichtung**: über die serielle USB-Konsole (115200 Baud) `set wifi.ssid <ssid>`,
  `set wifi.pwd <pwd>`, `set mqtt.iata <code>` und `set mqtt1.preset <name>` ausführen, dann
  neu starten. Dieselben Observer-Befehle funktionieren auch über die Befehlszeile der App und
  über Remote-Admin.
- **Geteilt mit dem Repeater-Observer**: beide Firmwares lesen dieselbe `/mqtt.json` und
  Identität, beim Hin- und Herflashen bleiben Broker, Slots, IATA, WiFi, Zeitzone und
  öffentlicher Schlüssel also erhalten. Hat der Companion seine Einstellungen gespeichert,
  startet ein zurückgeflashter Repeater mit seinen eigenen Einstellungen auf Build-Standard,
  **einschließlich des Admin-Passworts**: setze es neu. Führe `set repeat on` aus, wenn der
  Repeater weiterleiten soll.
- **Empfangsfilter**: der DMC-Companion-Filter (`filter ...`) ist enthalten. Er ändert nur,
  was die App zeigt und was wiederholt wird; der Observer lädt weiterhin jedes gehörte Paket
  hoch.
- **Nicht enthalten**: Pull-OTA, SNMP, das Webportal, Störungsmeldungen, MQTT-Self-Heal und
  Nachbar-Veröffentlichung. Updates per USB.
- **Heltec V3** (ohne PSRAM): höchstens 150 Kontakte und 2 aktive MQTT-Slots. Boards mit PSRAM
  behalten 350 Kontakte.

Mehr in
[`MQTT_IMPLEMENTATION.md`](https://github.com/Dutch-MeshCore/MeshCore/blob/dmc-observer-dev/MQTT_IMPLEMENTATION.md#companion-observer).

## Ein Release bauen (Maintainer)

Releases werden auf GitHub mit dem Button **DMC Firmware Release** gebaut: Actions → DMC
Firmware Release → Run workflow. Der Workflow checkt den richtigen Branch selbst aus.

| Firmware | Branch | Envs |
| --- | --- | --- |
| `repeater` | `dmc-dev` | `*_repeater` |
| `room-server` | `dmc-dev` | `*_room_server` |
| `repeater-packetlog` | `dmc-dev-packetlog` | `*_repeater` |
| `repeater-mqtt` | `dmc-observer-dev` | `*_repeater_observer_mqtt` |
| `room-server-mqtt` | `dmc-observer-dev` | `*_room_server_observer_mqtt` |
| `observer-mqtt` | `dmc-observer-dev` | `*_observer_mqtt` |
| `companion` | `dmc-dev` | `*_companion_radio_ble`, `*_companion_radio_wifi` |
| `companion-observer` | `dmc-observer-dev` | `*_companion_observer_wifi` |

- **Kanal `dev`** baut das rollierende Pre-Release `dmc-<firmware>-dev` an Ort und Stelle neu
  (`observer-mqtt-dev` für `observer-mqtt`), mit `DMC_CHANNEL=dev`. `repeater-mqtt` und
  `room-server-mqtt` gibt es nur als Release. `dmc-companion-observer-dev` speist nur den
  Toolbox-Flasher, nie den OTA-Server, weil der Companion Observer kein Pull-OTA hat.
- **Kanal `release`** erstellt das Entwurfs-Pre-Release `dmc-<firmware>-<version>`; prüfe es
  und veröffentliche es von Hand. Ein vorhandener Tag wird abgelehnt.
- Jedes Board baut auf einem eigenen Runner. Ein fehlgeschlagenes Board steht in der
  Zusammenfassung des Laufs und wird weggelassen; der Rest wird trotzdem veröffentlicht.

Alternativen für den Companion Observer: der nur manuell startbare Workflow **Build Companion
Observer Firmwares** (Run workflow, *Use workflow from* `dmc-observer-dev`, Version eingeben)
baut alle Boards in einem Job und erstellt den Entwurf `dmc-companion-observer-<version>`.
Oder lokal bauen und veröffentlichen, siehe [Lokale Builds](local-builds.md#firmware-sets).
