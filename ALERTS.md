# Fault Alerts (Group Channel)

This document describes MeshCore repeater fault alerts, including configuration, CLI commands, and operational behavior.

The repeater can broadcast a one-line fault notification on a configured group channel when WiFi or any active MQTT slot has been disconnected longer than a configurable threshold.

The alert is sent over **LoRa** as a `PAYLOAD_TYPE_GRP_TXT` flood packet on the configured channel (with sender = device name) - *not* over MQTT. This is intentional: the MQTT path is what's broken, so the only working delivery is the mesh itself. Anyone in radio range subscribed to the same channel/hashtag in their companion app will see the alert inline with normal channel chat.

> **A small list of community channels is intentionally NOT supported.** Fault alerts are operator-infrastructure noise - broadcasting them on shared community channels would spam every node in the area (and on `#test` / `#bot` would amplify via well-known auto-responders). The currently banned destinations are:
>
> - The well-known **Public** group PSK (`izOH6cXN6mrJ5e26oRXNcg==`)
> - **`#test`** (`sha256("#test")[0..15]`)
> - **`#bot`** (`sha256("#bot")[0..15]`)
>
> The list lives in `BANNED_ALERT_CHANNELS[]` in [src/helpers/AlertReporter.cpp](src/helpers/AlertReporter.cpp); adding a new entry is one line (label + 32 hex chars). The matcher runs at both the CLI validation step (`set alert.psk`, `set alert.hashtag`) and the alert-send path, so a saved-config bypass is still refused at runtime. You must point alerts at a **private PSK** (`set alert.psk`) or a non-banned **hashtag channel** (`set alert.hashtag`) before alerts can fire.

## Scope and routing

Alert floods ride the **repeater's default scope** by default (the same TransportKey used for adverts and channel broadcasts - set via `region default ...`). Operators can override on a per-alert-feature basis with `set alert.region <name>`:

- If `alert.region` is set and the name resolves via `RegionMap`, that region's TransportKey is used.
- If `alert.region` is unset, or the name doesn't resolve, the repeater's `default_scope` is used.
- If both are null, the alert is sent unscoped (matches the pre-scoped firmware's behavior).

`alert.region` is stored as-is - it does **not** create the region. Use `region put <name>` first if it doesn't exist.

## What triggers an alert

- **WiFi**: continuously down for at least `alert.wifi` minutes (default 30)
- **MQTT slot N**: enabled, has connected at least once since boot, and has been disconnected for at least `alert.mqtt` minutes (default 240, i.e. 4 h)

A "recovered" message is sent once when the underlying connection comes back. After firing, a fault is rate-limited by `alert.interval` (default 60 minutes) before it can re-fire - this prevents flapping links from spamming the channel.

## Defaults

| Setting | Default | Notes |
|---------|---------|-------|
| `alert` | `off` | Master enable for automatic fault alerts |
| `alert.psk` | *(unset)* | Private channel secret as **32 hex chars** (16-byte channel key) - the same format the mobile app's "Share Channel" emits, and what every other secret-shaped CLI command (e.g. `prv.key`) uses. |
| `alert.hashtag` | *(unset)* | Informational only; set via `set alert.hashtag` to pre-derive `alert.psk` from `sha256("#name")[0..15]`. Cleared when `alert.psk` is set directly. |
| `alert.region` | *(unset)* | Optional region name; overrides the repeater's `default_scope` for alert sends only. Empty = use `default_scope`. Looked up lazily via `RegionMap`; unknown names silently fall back to `default_scope`. |
| `alert.wifi` | `30` (min) | 0 disables WiFi alerts |
| `alert.mqtt` | `240` (min) | 0 disables MQTT alerts |
| `alert.interval` | `60` (min) | Minutes between repeat alerts of the same fault. **Hard floor of 60 min** so a flapping link can't spam the mesh; the CLI rejects lower values and AlertReporter clamps stale prefs at runtime. |

> `alert.psk` is unset on a fresh flash. **Alerts cannot fire and `alert test` will refuse to send until you configure either `alert.psk` directly or `alert.hashtag` (which derives one).** The sender shown on outgoing alert messages is always the node name (`set name ...`); there is no separate `alert.name`.

## CLI

Get:
- `get alert` - master on/off
- `get alert.psk` - the active 32-hex-char PSK (or `(unset)`) (**serial console only**)
- `get alert.hashtag` - the originating hashtag (or `(unset)`, e.g. after `set alert.psk` overrides the hashtag-derived key)
- `get alert.region` - alert-only scope override (or `(unset, using default scope)`)
- `get alert.wifi` / `get alert.mqtt` / `get alert.interval`

Set:
- `set alert on` / `set alert off`
- `set alert.psk <hex>` - 32 hex chars (16-byte channel secret); rejects banned channels (Public, `#test`, `#bot`). Paste the mobile app's "Share Channel" output as-is. Clears `alert.hashtag` since the new key is operator-supplied.
- `set alert.psk` (no argument) - clears both `alert.psk` and `alert.hashtag`
- `set alert.hashtag <name>` - derives the 16-byte key from `sha256("#name")` *once*, stores it as `alert.psk`, and remembers the hashtag for `get alert.hashtag`. `#` prefix is added if omitted (so `alerts` and `#alerts` are equivalent). Refuses banned hashtag names.
- `set alert.hashtag` (no argument) - clears both `alert.psk` and `alert.hashtag`
- `set alert.region <name>` - alert-only scope override (no region-map mutation; unknown names silently fall back to `default_scope`)
- `set alert.region` (no argument) - clear override, use `default_scope`
- `set alert.wifi <minutes>` (0-1440; 0 = disabled)
- `set alert.mqtt <minutes>` (0-10080; 0 = disabled)
- `set alert.interval <minutes>` (60-10080; 60-minute floor to protect mesh airtime)

Action:
- `alert test` - send a one-off `[test] alert channel ok` immediately on the configured channel; ignores `alert on/off` so operators can verify the channel before enabling fault firing. Returns an error if no channel is configured.
- `alert test <message>` - send a custom test message: `[test] <message>`.

## Example: dedicated hashtag channel (recommended for operator groups)

```bash
set alert.hashtag ops-alerts   # stored as "#ops-alerts"; key = sha256("#ops-alerts")[0..15]
set alert.wifi 10              # tighter for ops monitoring
set alert.mqtt 60
set alert on
alert test
```

Anyone running a companion app and subscribed to the `#ops-alerts` hashtag channel will see the alerts inline.

## Example: dedicated alerts channel with a private PSK

Generate a 16-byte random PSK as 32 hex chars (`openssl rand -hex 16`), or use the companion app's "Add channel" feature and copy the "Share Channel" output. Then:

```bash
set alert.psk <32_hex_chars>   # 16-byte channel secret; mobile "Share Channel" pastes in directly
set alert.wifi 10
set alert.mqtt 60
set alert on
alert test
```

Subscribers running a MeshCore companion app should add a channel with the same PSK; alerts will appear in that channel's chat view. (Pick any local name for it - the sender of incoming alert messages is the repeater's node name.)

## Sample messages

```
MyObserver: WiFi down 47m (reason 201)
MyObserver: WiFi recovered after 1h3m
MyObserver: MQTT slot 1 (analyzer-us) down 4h12m
MyObserver: MQTT slot 1 (analyzer-us) recovered after 4h45m
```

## Notes

- A reboot during an outage resets the timer; the alert won't double-fire because `millis()` starts at 0 at boot. The fault must persist `alert.wifi` / `alert.mqtt` minutes from boot.
- Fault state is stored in RAM only - no persistence across reboots.
- The MQTT-slot watcher uses a separate per-slot `current_outage_started_ms` field that is reset on each reconnect, distinct from the `first_disconnect_time` shown in `mqttN.diag` (which remains a "first disconnect since boot" counter for diagnostics).
- WiFi-down alerts can only be delivered if the LoRa radio is up. There is no fallback path.
- Banned channels (Public, `#test`, `#bot`) are **rejected** at both `set alert.psk` / `set alert.hashtag` and at the alert-send path, so even if you somehow set one via a saved config file, the firmware will silently refuse to broadcast on it. To add another banned channel, append a row to `BANNED_ALERT_CHANNELS[]` in [src/helpers/AlertReporter.cpp](src/helpers/AlertReporter.cpp); the format is `{ "label", "32-lowercase-hex-chars" }` (compute as `printf '#name' | openssl dgst -sha256 | cut -c1-32`).
- Alerts are sent via `sendFlood` with the resolved TransportKey codes attached, so they appear on the configured scope just like other broadcast traffic. Operators monitoring a specific region need to be subscribed to that region's scope to hear alerts.
