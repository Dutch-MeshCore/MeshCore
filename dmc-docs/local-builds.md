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

`build.sh` maakt gemerkte artefacten met de kenmerkende versienaam
`v1.17.1.01-dutchmeshcore.nl-<hash>`.

## Radio-standaarden

Uit `[arduino_base]` in `platformio.ini`:

| Parameter | Waarde |
| --- | --- |
| `LORA_FREQ` | `869.618` |
| `LORA_BW` | `62.5` |
| `LORA_SF` | `8` |
