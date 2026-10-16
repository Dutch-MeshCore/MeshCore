# Releases

DMC firmware ships as ready-to-flash binaries per board and track. The easiest way to
flash is the web flasher.

## Getting started

1. Open the [Toolbox flasher](https://toolbox.dutchmeshcore.nl/#/flasher).
2. Select your firmware type (track) and board.
3. Choose a version (latest by default).
4. Choose an image type: **Update** or **Full Flash**.
5. Flash, then set the device up over the [Web Panel](web-panel.md) or the serial console.

## Firmware tracks

DMC has three tracks, each based on a branch. All three are repeater firmware; DMC does
not ship companion firmware. For a companion with a receive filter, SNMP and OTA, see
[Meshcomod (EV Fork)](https://github.com/Elektr0Vodka/meshcomod) (pre-release, also in the Toolbox flasher).

| Track (branch) | What it is | Includes |
| --- | --- | --- |
| `dmc-dev` | Official repeater firmware | Repeater + filters |
| `dmc-dev-packetlog` | Repeater firmware with packetlog | Repeater + filters + packet logging |
| `dmc-observer-dev` | Observer firmware | Repeater + filters + on-device MQTT + OTA (no packetlog) |

In the observer track, repeaters get everything; room servers get MQTT and OTA but no
filter, because a room server does not repeat by definition.

## Stable and dev releases

- **Stable**: one release per version, for example `dmc-repeater-v1.17.1`.
- **Dev**: rolling pre-releases rebuilt in place from the branch: `dmc-repeater-dev`,
  `dmc-repeater-packetlog-dev` and `observer-mqtt-dev`. They now run `v1.18.0` (upstream
  MeshCore v1.18.0, merged on 2026-10-15). A dev build reports
  `v1.18.0-dev-dutchmeshcore.nl-<hash>` in `ver`; the file name has no `-dev`. The
  Toolbox flasher lists them as `1.18.0-dev`.

## File selection

- `*.bin`: incremental update for a device already running DMC firmware.
- `*-merged.bin`: full flash image from address `0x0` for a clean install.

## Post-flash setup

- **`dmc-dev` (repeater)**: configure over the serial console or the
  [Web Panel](web-panel.md).
- **`dmc-dev-packetlog`**: same as the repeater; no extra configuration for packetlog.
- **`dmc-observer-dev` (observer)**: set WiFi and `set mqtt.iata <code>`, pick a broker
  preset. See [Custom CLI](custom-cli.md).
