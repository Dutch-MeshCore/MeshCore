# Web Panel

On ESP32 observer builds the firmware serves a local configuration web panel
(`WebConfigServer`). The panel is RAM-only: no web state is persisted, and changes go
through the CLI `set` handlers.

## What it is

A local web server on the device that lets you configure and check status without an
external connection. Available only on ESP32 observer builds.

## Two modes

- **SETUP**: SoftAP with a captive portal. Raised automatically on first boot with no WiFi,
  or manually with `start webconfig ap`. Used to enter the first WiFi credentials.
- **LAN**: bound to the station connection once WiFi is up, via `start webconfig`, with
  admin-password login.

## Access

1. Find the IP address with `get wifi.status`.
2. Open the panel in a browser.
3. Log in with the admin password.

## What it edits

Device name, WiFi, MQTT identity and brokers, and other observer settings that the CLI
exposes.

## OTA updates

### Local web OTA

Upload a binary yourself through the browser (ElegantOTA):

- `start ota`: serves on the station IP when on WiFi, otherwise raises a `MeshCore-OTA`
  hotspot.
- `start ota ap`: always via a hotspot.

### OTA over an update channel (observer/MQTT boards)

WiFi observer builds can pull a build from the update server and flash themselves, without
a manual upload:

- `ota check`: report the available build.
- `ota branch`: show the current channel.
- `ota branch dev`: pull from the `dev` channel from now on.
- `ota branch stable`: pull from the `stable` channel from now on.
- `ota branch default`: clear the channel override.
- `ota update`: download the available build and flash it.

These `ota` commands are available on observer builds only.

## Notes

- A self-signed HTTPS certificate is expected; the browser will warn about it.
- Turn the panel off when done to free memory for MQTT.
