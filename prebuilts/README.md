# Prebuilt firmware (test builds)

Observer MQTT dev-channel test builds of the packet-filter enhancements (advert origin window, path-prefix block, dry-run, saved airtime, sender/text rules with throttle and dosing, watch list, extended MQTT `filter` topic). Built from commit `8efae0e5` of this branch.

Branch: `enhancement/dmc-observer-dev-filtering`

Built locally with:

```bash
DMC_CHANNEL=dev FIRMWARE_VERSION=v1.17.1 bash build.sh build-firmware <env>
```

| File | Bytes | sha256 (first 16) | Embedded version |
| --- | ---: | --- | --- |
| `Heltec_Wireless_Tracker_repeater_observer_mqtt-v1.17.1-8efae0e5-merged.bin` | 1743408 | `bf86728d6f89302b` |  |
| `Heltec_Wireless_Tracker_repeater_observer_mqtt-v1.17.1-8efae0e5.bin` | 1677872 | `9513eb3779df1d4b` | `v1.17.1-dev-dutchmeshcore.nl-observer-mqtt-8efae0e5` |
| `Heltec_Wireless_Tracker_repeater_observer_mqtt.partsig` | 102 | `74bbeb3d2757ef05` |  |
| `Xiao_S3_WIO_repeater_observer_mqtt-v1.17.1-8efae0e5-merged.bin` | 1729456 | `0a3bd530fc569964` |  |
| `Xiao_S3_WIO_repeater_observer_mqtt-v1.17.1-8efae0e5.bin` | 1663920 | `5f166f9792f25d78` | `v1.17.1-dev-dutchmeshcore.nl-observer-mqtt-8efae0e5` |
| `Xiao_S3_WIO_repeater_observer_mqtt.partsig` | 102 | `74bbeb3d2757ef05` |  |
| `heltec_v4_r8_repeater_observer_mqtt-v1.17.1-8efae0e5-merged.bin` | 1802656 | `26a27942884cba2b` |  |
| `heltec_v4_r8_repeater_observer_mqtt-v1.17.1-8efae0e5.bin` | 1737120 | `d1d91e9d077d314a` | `v1.17.1-dev-dutchmeshcore.nl-observer-mqtt-8efae0e5` |
| `heltec_v4_r8_repeater_observer_mqtt.partsig` | 102 | `da10873b4117fa4a` |  |
| `heltec_v4_repeater_observer_mqtt-v1.17.1-8efae0e5-merged.bin` | 1799312 | `63fe66d8a3e56b19` |  |
| `heltec_v4_repeater_observer_mqtt-v1.17.1-8efae0e5.bin` | 1733776 | `eeafb59e1124d506` | `v1.17.1-dev-dutchmeshcore.nl-observer-mqtt-8efae0e5` |
| `heltec_v4_repeater_observer_mqtt.partsig` | 102 | `da10873b4117fa4a` |  |

## Flashing

- **ESP32 boards** (`.bin`): flash `<env>-...-merged.bin` at offset `0x0` for a fresh
  install (it includes bootloader + partitions + app), or the plain `<env>-...bin` at
  the app offset (`0x10000`) / via OTA to update an existing install. `<env>.partsig`
  is the partition-table signature the OTA manifest generator uses.
- **nRF52 boards** (`.uf2` / `.zip`): drag the `.uf2` onto the board's UF2 drive, or
  flash the `.zip` with `adafruit-nrfutil dfu serial`.

The hash in each filename is the source commit the binary was built from; the commit
that adds these files necessarily comes after it.
