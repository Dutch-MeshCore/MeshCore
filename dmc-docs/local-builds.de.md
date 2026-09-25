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

`build.sh` erzeugt gekennzeichnete Artefakte mit der markanten Versionskennung
`v1.17.1.01-dutchmeshcore.nl-<hash>`.

## Funk-Standardwerte

Aus `[arduino_base]` in `platformio.ini`:

| Parameter | Wert |
| --- | --- |
| `LORA_FREQ` | `869.618` |
| `LORA_BW` | `62.5` |
| `LORA_SF` | `8` |
