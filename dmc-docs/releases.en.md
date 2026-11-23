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

DMC has three tracks, each based on a branch. All three build repeater firmware; two of
them also build a companion:

- **Companion Filter** (`dmc-dev`): the companion radio with a receive filter, plus SNMP
  and OTA on ESP32 boards with WiFi. See [Custom CLI](custom-cli.md#companion-filter).
- **Companion Observer** (`dmc-observer-dev`): a WiFi companion radio and an MQTT observer
  in one image. See [Companion Observer](#companion-observer) below.

The [Meshcomod (EV Fork)](https://github.com/Elektr0Vodka/meshcomod) companion is in the
Toolbox flasher too (pre-release).

| Track (branch) | What it is | Includes |
| --- | --- | --- |
| `dmc-dev` | Official repeater firmware | Repeater + filters; Companion Filter |
| `dmc-dev-packetlog` | Repeater firmware with packetlog | Repeater + filters + packet logging |
| `dmc-observer-dev` | Observer firmware | Repeater + filters + on-device MQTT + OTA (no packetlog); Companion Observer |

In the observer track, repeaters get everything; room servers get MQTT and OTA but no
filter, because a room server does not repeat by definition.

## Stable and dev releases

- **Stable**: one release per version, for example `dmc-repeater-v1.17.1`.
- **Dev**: rolling pre-releases rebuilt in place from the branch: `dmc-repeater-dev`,
  `dmc-repeater-packetlog-dev` and `observer-mqtt-dev`. They now run `v1.18.0` (upstream
  MeshCore v1.18.0, merged on 2026-10-15). A dev build reports
  `v1.18.0-dev-dutchmeshcore.nl-<hash>` in `ver`; the file name has no `-dev`. The
  Toolbox flasher lists them as `1.18.0-dev`.
- **Companions**: `dmc-companion-dev` (Companion Filter) and `dmc-companion-observer-dev`
  (Companion Observer) are rolling dev releases too; versioned releases are named
  `dmc-companion-vX.Y.Z` and `dmc-companion-observer-vX.Y.Z`. The Companion Observer
  reports `vX.Y.Z[-dev]-dutchmeshcore.nl-comp-obs-<hash>`.

## File selection

- `*.bin`: incremental update for a device already running DMC firmware.
- `*-merged.bin`: full flash image from address `0x0` for a clean install.

## Post-flash setup

- **`dmc-dev` (repeater)**: configure over the serial console or the
  [Web Panel](web-panel.md).
- **`dmc-dev-packetlog`**: same as the repeater; no extra configuration for packetlog.
- **`dmc-observer-dev` (observer)**: set WiFi and `set mqtt.iata <code>`, pick a broker
  preset. See [Custom CLI](custom-cli.md).
- **Companion Observer**: see the next section.

## Companion Observer

A companion radio and an MQTT observer in one image. The MeshCore app connects over WiFi
(TCP port 5000) while the packets the node hears go to the configured MQTT brokers.
**Not yet tested on hardware.**

| Board | Env |
| --- | --- |
| Heltec V3 | `Heltec_v3_companion_observer_wifi` |
| Heltec V4 | `heltec_v4_companion_observer_wifi` |
| Heltec V4 expansion kit | `heltec_v4_expansionkit_companion_observer_wifi` |
| Xiao S3 WIO | `Xiao_S3_WIO_companion_observer_wifi` |
| Waveshare S3 LR1121 | `Waveshare_S3_LR1121_companion_observer_wifi` |

- **Setup**: over the USB serial console (115200 baud) run `set wifi.ssid <ssid>`,
  `set wifi.pwd <pwd>`, `set mqtt.iata <code>` and `set mqtt1.preset <name>`, then reboot.
  The same observer commands also work from the app's command line and from remote admin.
- **Shared with the repeater observer**: both firmwares read the same `/mqtt.json` and
  identity, so reflashing either way keeps brokers, slots, IATA, WiFi, timezone and public
  key. After the companion has saved its settings, a repeater flashed back starts with its
  own settings at build defaults, **including the admin password**: set it again. Run
  `set repeat on` if the repeater should forward.
- **Receive filter**: the DMC companion filter (`filter ...`) is included. It only changes
  what the app shows and what is repeated; the observer still uploads every heard packet.
- **Not included**: pull OTA, SNMP, the web portal, fault alerts, MQTT self-heal and
  neighbour publishing. Update over USB.
- **Heltec V3** (no PSRAM): at most 150 contacts and 2 active MQTT slots. PSRAM boards keep
  350 contacts.

More in
[`MQTT_IMPLEMENTATION.md`](https://github.com/Dutch-MeshCore/MeshCore/blob/dmc-observer-dev/MQTT_IMPLEMENTATION.md#companion-observer).

## Building a release (maintainers)

Releases are built on GitHub with the **DMC Firmware Release** button: Actions → DMC
Firmware Release → Run workflow. It checks out the right branch itself.

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

- **Channel `dev`** rebuilds the rolling pre-release `dmc-<firmware>-dev` in place
  (`observer-mqtt-dev` for `observer-mqtt`) with `DMC_CHANNEL=dev`. `repeater-mqtt` and
  `room-server-mqtt` are release-only. `dmc-companion-observer-dev` only feeds the Toolbox
  flasher, never the OTA server, because the Companion Observer has no pull OTA.
- **Channel `release`** creates the draft pre-release `dmc-<firmware>-<version>`; review it
  and publish it by hand. An existing tag is refused.
- Each board builds on its own runner. A board that fails is listed in the run summary and
  left out; the rest is still published.

Fallbacks for the Companion Observer: the dispatch-only **Build Companion Observer
Firmwares** workflow (Run workflow, *Use workflow from* `dmc-observer-dev`, enter the
version) builds every board in one job and creates the draft
`dmc-companion-observer-<version>`. Or build and publish locally, see
[Local Builds](local-builds.md#firmware-sets).
