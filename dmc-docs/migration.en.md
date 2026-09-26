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

## Last resort

If it will not run stably, do a full erase and flash the `*-merged.bin` clean from `0x0`,
then redo the setup.
