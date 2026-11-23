# Local Builds

DMC builds with **PlatformIO**. Building locally is useful to
modify firmware, test pre-releases, or produce images that are not in Releases.

## Tools

Install PlatformIO Core, or use the PlatformIO extension in VS Code.

## Listing targets

Each env is a `<board>_<role>` combination. Envs are defined per board under
`variants/*/platformio.ini` (the root `platformio.ini` pulls them in via
`extra_configs = variants/*/platformio.ini`). List the available envs with:

```bash
pio project config
```

or browse the folders under `variants/`.

## Building a target

```bash
pio run -e <env>
```

For example an observer build: `pio run -e <board>_repeater_observer_mqtt`. Observer envs
are ESP32-only and gated on `WITH_MQTT_BRIDGE`.

## Flashing

```bash
pio run -e <env> -t upload --upload-port <PORT>
```

`<PORT>` is `COMx` on Windows or `/dev/ttyUSBx` on Linux.

## Monitoring

```bash
pio device monitor --port <PORT> --baud 115200
```

## Release-style builds

`build.sh` produces branded artifacts. With `FIRMWARE_VERSION=v1.18.0` the node reports
`v1.18.0-dutchmeshcore.nl-<hash>` in `ver`, or `v1.18.0-dev-dutchmeshcore.nl-<hash>` with
`DMC_CHANNEL=dev`. The file name stays `<env>-v1.18.0-<hash>.bin`.

## Firmware sets

`build.sh` also builds whole sets into `out/`, one command per set. Run each from the branch
that has the envs:

| Command | Branch | Builds |
| --- | --- | --- |
| `bash build.sh build-dmc-repeater-firmwares` | `dmc-dev`, `dmc-dev-packetlog` | every `*_repeater` |
| `bash build.sh build-room-server-firmwares` | `dmc-dev` | every `*_room_server` |
| `bash build.sh build-companion-firmwares` | `dmc-dev` | every companion |
| `bash build.sh build-repeater-mqtt-firmwares` | `dmc-observer-dev` | every `*_repeater_observer_mqtt` |
| `bash build.sh build-room-server-mqtt-firmwares` | `dmc-observer-dev` | every `*_room_server_observer_mqtt` |
| `bash build.sh build-companion-observer-firmwares` | `dmc-observer-dev` | every `*_companion_observer_wifi` |

Companion Observer builds report `v1.18.0-dutchmeshcore.nl-comp-obs-<hash>`. For observer
targets `build.sh` first downloads the reduced-TLS mbedTLS libraries
(`scripts/fetch_mbedtls_4k.sh`, SHA-256 pinned), so the first build needs internet access.

To publish a set as a draft pre-release without GitHub runners, use
`scripts/publish-release.sh` (needs a `GITHUB_TOKEN` with `contents: write`):

```bash
GITHUB_TOKEN=... bash scripts/publish-release.sh \
  build-companion-observer-firmwares v1.18.0 dmc-companion-observer-v1.18.0 "DMC Companion Observer Firmware v1.18.0"
```

Review the draft on GitHub and publish it by hand. The usual route is the release button,
see [Releases](releases.md#building-a-release-maintainers).

## Radio defaults

From `[arduino_base]` in `platformio.ini`:

| Parameter | Value |
| --- | --- |
| `LORA_FREQ` | `869.618` |
| `LORA_BW` | `62.5` |
| `LORA_SF` | `8` |
