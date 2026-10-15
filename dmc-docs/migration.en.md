# Migration

Upgrading to DMC (observer) from upstream MeshCore or older DMC firmware.

## Quick answer

Flash the matching DMC release over your existing install. Settings do not fully migrate,
so reapply them after flashing.

## Before you start

- Note your current settings (region, frequency, WiFi, MQTT).
- Pick the right track and board in [Releases](releases.md).
- For a normal upgrade you do not need to erase; use the `*.bin` update image.

## After flashing (required)

Reapply the core settings over the CLI (see [Custom CLI](custom-cli.md)):

```text
set wifi.ssid <your-ssid>
set wifi.pwd <your-password>
set mqtt.iata <code>
```

Then pick a broker preset and reboot the device.

## Optional settings

- Owner metadata: `set mqtt.owner <64-hex>`, `set mqtt.email <email>`
- TX publishing: `set mqtt.tx on`
- Filter and region-gating tuning (see [Custom CLI](custom-cli.md))

## Verify

```text
get wifi.status
get mqtt.status
```

Optionally enable the [Web Panel](web-panel.md) to confirm the status.

## From meshcomod (companion)

A companion moving from meshcomod to the DMC companion firmware keeps its identity, contacts,
channels and settings, including the CAD toggle and the WiFi network. On the first boot the DMC
firmware reads meshcomod's settings once and stores them in its own format.

- Flash the **update image** (`*.bin`, at `0x10000`), not `*-merged.bin`: the merged image wipes
  the NVS area where meshcomod keeps the WiFi credentials. Everything else survives either way.
- If meshcomod ever updated itself over the air, it may be running from the second app slot. Clear
  the boot selector as well, or the device keeps booting meshcomod:

```text
esptool.py --chip esp32s3 erase_region 0xe000 0x2000
esptool.py --chip esp32s3 write_flash 0x10000 <firmware>.bin
```

- Data that meshcomod keeps on an SD card (Launcher installs) is not picked up.

## Last resort

If it will not run stably, do a full erase and flash the `*-merged.bin` clean from `0x0`,
then redo the setup.
