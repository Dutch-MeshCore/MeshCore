# Lokale Builds

DMC baut mit **PlatformIO**. Lokales Bauen ist nützlich,
um Firmware anzupassen, Pre-Releases zu testen oder Images zu erzeugen, die nicht in den
Releases stehen.

## Werkzeuge

Installiere PlatformIO Core oder nutze die PlatformIO-Erweiterung in VS Code.

## Targets auflisten

Jede Env ist eine `<board>_<rolle>`-Kombination. Envs werden pro Board unter
`variants/*/platformio.ini` definiert (die root-`platformio.ini` bindet sie über
`extra_configs = variants/*/platformio.ini` ein). Liste die verfügbaren Envs mit:

```bash
pio project config
```

oder durchsuche die Ordner unter `variants/`.

## Ein Target bauen

```bash
pio run -e <env>
```

Zum Beispiel ein Observer-Build: `pio run -e <board>_repeater_observer_mqtt`. Observer-Envs
sind nur für ESP32 und hängen von `WITH_MQTT_BRIDGE` ab.

## Flashen

```bash
pio run -e <env> -t upload --upload-port <PORT>
```

`<PORT>` ist `COMx` unter Windows oder `/dev/ttyUSBx` unter Linux.

## Überwachen

```bash
pio device monitor --port <PORT> --baud 115200
```

## Release-artige Builds

`build.sh` erzeugt gekennzeichnete Artefakte. Mit `FIRMWARE_VERSION=v1.18.0` meldet der Node
`v1.18.0-dutchmeshcore.nl-<hash>` in `ver`, mit `DMC_CHANNEL=dev`
`v1.18.0-dev-dutchmeshcore.nl-<hash>`. Der Dateiname bleibt `<env>-v1.18.0-<hash>.bin`.

## Firmware-Sets

`build.sh` baut auch ganze Sets nach `out/`, ein Befehl pro Set. Führe jeden Befehl auf dem
Branch aus, der die Envs hat:

| Befehl | Branch | Baut |
| --- | --- | --- |
| `bash build.sh build-dmc-repeater-firmwares` | `dmc-dev`, `dmc-dev-packetlog` | jedes `*_repeater` |
| `bash build.sh build-room-server-firmwares` | `dmc-dev` | jedes `*_room_server` |
| `bash build.sh build-companion-firmwares` | `dmc-dev` | jeden Companion |
| `bash build.sh build-repeater-mqtt-firmwares` | `dmc-observer-dev` | jedes `*_repeater_observer_mqtt` |
| `bash build.sh build-room-server-mqtt-firmwares` | `dmc-observer-dev` | jedes `*_room_server_observer_mqtt` |
| `bash build.sh build-companion-observer-firmwares` | `dmc-observer-dev` | jedes `*_companion_observer_wifi` |

Companion-Observer-Builds melden `v1.18.0-dutchmeshcore.nl-comp-obs-<hash>`. Für
Observer-Targets lädt `build.sh` zuerst die mbedTLS-Bibliotheken mit kleineren TLS-Puffern
herunter (`scripts/fetch_mbedtls_4k.sh`, per SHA-256 fixiert), der erste Build braucht also
Internet.

Um ein Set ohne GitHub-Runner als Entwurfs-Pre-Release zu veröffentlichen, nutze
`scripts/publish-release.sh` (braucht ein `GITHUB_TOKEN` mit `contents: write`):

```bash
GITHUB_TOKEN=... bash scripts/publish-release.sh \
  build-companion-observer-firmwares v1.18.0 dmc-companion-observer-v1.18.0 "DMC Companion Observer Firmware v1.18.0"
```

Prüfe den Entwurf auf GitHub und veröffentliche ihn von Hand. Der übliche Weg ist der
Release-Button, siehe [Releases](releases.md#ein-release-bauen-maintainer).

## Funk-Standardwerte

Aus `[arduino_base]` in `platformio.ini`:

| Parameter | Wert |
| --- | --- |
| `LORA_FREQ` | `869.618` |
| `LORA_BW` | `62.5` |
| `LORA_SF` | `8` |
