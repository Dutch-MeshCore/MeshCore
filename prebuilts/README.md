# Prebuilt firmware (test builds)

Non-MQTT DMC repeater test builds of the packet-filter enhancements (advert origin window, path-prefix block, dry-run, saved airtime, sender/text rules with throttle and dosing, watch list). Built from commit `18341f13` of this branch. The `-dev` marker sits inside the version because this branch's build.sh has no DMC_CHANNEL support; these bins are not OTA-capable, so the filename form does not matter here.

Branch: `enhancement/dmc-dev-filtering`

Built locally with:

```bash
FIRMWARE_VERSION=v1.17.1-dev bash build.sh build-firmware <env>
```

| File | Bytes | sha256 (first 16) | Embedded version |
| --- | ---: | --- | --- |
| `Heltec_v3_repeater-v1.17.1-dev-18341f13-merged.bin` | 1254592 | `fc09f5759de93fb9` |  |
| `Heltec_v3_repeater-v1.17.1-dev-18341f13.bin` | 1189056 | `3e40c033e561c940` | `v1.17.1-dev-dutchmeshcore.nl-18341f13` |
| `RAK_4631_repeater-v1.17.1-dev-18341f13.uf2` | 933376 | `347183273fb3df8d` | `v1.17.1-dev-dutchmeshcore.nl-18341f13` |
| `RAK_4631_repeater-v1.17.1-dev-18341f13.zip` | 467372 | `7df0ad8c4157647e` |  |

## Flashing

- **ESP32 boards** (`.bin`): flash `<env>-...-merged.bin` at offset `0x0` for a fresh
  install (it includes bootloader + partitions + app), or the plain `<env>-...bin` at
  the app offset (`0x10000`) / via OTA to update an existing install. `<env>.partsig`
  is the partition-table signature the OTA manifest generator uses.
- **nRF52 boards** (`.uf2` / `.zip`): drag the `.uf2` onto the board's UF2 drive, or
  flash the `.zip` with `adafruit-nrfutil dfu serial`.

The hash in each filename is the source commit the binary was built from; the commit
that adds these files necessarily comes after it.
