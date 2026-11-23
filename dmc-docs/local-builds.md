# Lokaal bouwen

DMC bouwt met **PlatformIO**. Lokaal bouwen is handig om
firmware aan te passen, pre-releases te testen of images te maken die niet in de Releases
staan.

## Gereedschap

Installeer PlatformIO Core, of gebruik de PlatformIO-extensie in VS Code.

## Targets bekijken

Elke env is een `<board>_<rol>`-combinatie. De envs worden per board gedefinieerd onder
`variants/*/platformio.ini` (de root-`platformio.ini` trekt ze binnen via
`extra_configs = variants/*/platformio.ini`). Bekijk de beschikbare envs met:

```bash
pio project config
```

of blader door de mappen onder `variants/`.

## Een target bouwen

```bash
pio run -e <env>
```

Bijvoorbeeld een observer-build: `pio run -e <board>_repeater_observer_mqtt`. Observer-envs
zijn alleen voor ESP32 en zijn afhankelijk van `WITH_MQTT_BRIDGE`.

## Flashen

```bash
pio run -e <env> -t upload --upload-port <PORT>
```

`<PORT>` is `COMx` op Windows of `/dev/ttyUSBx` op Linux.

## Monitoren

```bash
pio device monitor --port <PORT> --baud 115200
```

## Release-achtige builds

`build.sh` maakt gemerkte artefacten. Met `FIRMWARE_VERSION=v1.18.0` meldt de node
`v1.18.0-dutchmeshcore.nl-<hash>` in `ver`, of `v1.18.0-dev-dutchmeshcore.nl-<hash>` met
`DMC_CHANNEL=dev`. De bestandsnaam blijft `<env>-v1.18.0-<hash>.bin`.

## Firmwaresets

`build.sh` bouwt ook hele sets in `out/`, met één opdracht per set. Voer elke opdracht uit op
de branch die de envs heeft:

| Opdracht | Branch | Bouwt |
| --- | --- | --- |
| `bash build.sh build-dmc-repeater-firmwares` | `dmc-dev`, `dmc-dev-packetlog` | elke `*_repeater` |
| `bash build.sh build-room-server-firmwares` | `dmc-dev` | elke `*_room_server` |
| `bash build.sh build-companion-firmwares` | `dmc-dev` | elke companion |
| `bash build.sh build-repeater-mqtt-firmwares` | `dmc-observer-dev` | elke `*_repeater_observer_mqtt` |
| `bash build.sh build-room-server-mqtt-firmwares` | `dmc-observer-dev` | elke `*_room_server_observer_mqtt` |
| `bash build.sh build-companion-observer-firmwares` | `dmc-observer-dev` | elke `*_companion_observer_wifi` |

Companion Observer-builds melden `v1.18.0-dutchmeshcore.nl-comp-obs-<hash>`. Voor
observer-targets downloadt `build.sh` eerst de mbedTLS-bibliotheken met kleinere TLS-buffers
(`scripts/fetch_mbedtls_4k.sh`, vastgepind op SHA-256), dus de eerste build heeft internet
nodig.

Om een set zonder GitHub-runners als concept-pre-release te publiceren, gebruik je
`scripts/publish-release.sh` (vraagt een `GITHUB_TOKEN` met `contents: write`):

```bash
GITHUB_TOKEN=... bash scripts/publish-release.sh \
  build-companion-observer-firmwares v1.18.0 dmc-companion-observer-v1.18.0 "DMC Companion Observer Firmware v1.18.0"
```

Controleer het concept op GitHub en publiceer het met de hand. De gewone weg is de
release-knop, zie [Releases](releases.md#een-release-bouwen-beheerders).

## Radio-standaarden

Uit `[arduino_base]` in `platformio.ini`:

| Parameter | Waarde |
| --- | --- |
| `LORA_FREQ` | `869.618` |
| `LORA_BW` | `62.5` |
| `LORA_SF` | `8` |
