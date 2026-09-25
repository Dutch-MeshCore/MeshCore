# DutchMeshCore Firmware

DutchMeshCore provides repeater firmware for the Dutch MeshCore network in three
flavours: repeater with filters (`dmc-dev`), repeater with filters and packet logging
(`dmc-dev-packetlog`), and observer with on-device MQTT and OTA (`dmc-observer-dev`).

## I want to

- Choose a board: [Boards](boards.md)
- Flash firmware: [Releases](releases.md)
- Upgrade from older firmware: [Migration](migration.md)
- Configure over the browser: [Web Panel](web-panel.md)
- Automate or script: [Repeater API](api.md)
- Build firmware myself: [Local Builds](local-builds.md)
- DMC-specific commands: [Custom CLI](custom-cli.md)

## End-user guides

Start at [Boards](boards.md) to pick hardware, flash via [Releases](releases.md),
[migrate](migration.md) an existing device, and configure it through the
[Web Panel](web-panel.md).

## Developer notes

Build firmware locally with PlatformIO: see [Local Builds](local-builds.md). The
commands DMC adds on top of upstream are in [Custom CLI](custom-cli.md).

## Current scope

DutchMeshCore is a downstream-only fork of
[meshcore-dev/MeshCore](https://github.com/meshcore-dev/MeshCore). Changes flow in from
upstream, never the other way. The base branch is `dmc-dev`, currently synced to upstream
firmware `v1.17.1` with DMC patch revision `.01` (builds carry the brand
`v1.17.1.01-dutchmeshcore.nl-<hash>`). This site documents the DMC additions; for general
MeshCore concepts, see the upstream project.

## DutchMeshCore references

- [MeshWiki](https://meshwiki.nl) (wiki and region overview)
- [Discord](https://discord.dutchmeshcore.nl) (community)
- [dutchmeshcore.nl](https://dutchmeshcore.nl) (main site)
- [Toolbox](https://toolbox.dutchmeshcore.nl) (channels, MQTT, flasher, and more)
