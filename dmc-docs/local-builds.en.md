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

`build.sh` produces branded artifacts with the distinctive version string
`v1.17.1.01-dutchmeshcore.nl-<hash>`.

## Radio defaults

From `[arduino_base]` in `platformio.ini`:

| Parameter | Value |
| --- | --- |
| `LORA_FREQ` | `869.618` |
| `LORA_BW` | `62.5` |
| `LORA_SF` | `8` |
