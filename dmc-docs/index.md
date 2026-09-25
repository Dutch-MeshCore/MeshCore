# DutchMeshCore Firmware

DutchMeshCore levert repeater-firmware voor het Nederlandse MeshCore-netwerk in drie
smaken: repeater met filters (`dmc-dev`), repeater met filters en packetlog
(`dmc-dev-packetlog`), en observer met on-device MQTT en OTA (`dmc-observer-dev`).

## Wat wil je doen

- Een board kiezen: [Boards](boards.md)
- Firmware flashen: [Releases](releases.md)
- Upgraden vanaf oudere firmware: [Migratie](migration.md)
- Instellen via de browser: [Webpaneel](web-panel.md)
- Automatiseren of scripten: [Repeater-API](api.md)
- Zelf firmware bouwen: [Lokaal bouwen](local-builds.md)
- DMC-specifieke commando's: [Custom CLI](custom-cli.md)

## Handleidingen voor eindgebruikers

Begin bij [Boards](boards.md) om hardware te kiezen, flash via [Releases](releases.md),
[migreer](migration.md) een bestaand apparaat en configureer het via het
[Webpaneel](web-panel.md).

## Ontwikkelaarsnotities

Bouw firmware lokaal met PlatformIO: zie [Lokaal bouwen](local-builds.md). De
commando's die DMC bovenop upstream toevoegt staan in [Custom CLI](custom-cli.md).

## Huidige scope

DutchMeshCore is een downstream-only fork van
[meshcore-dev/MeshCore](https://github.com/meshcore-dev/MeshCore). Wijzigingen komen
vanuit upstream binnen, nooit andersom. De basisbranch is `dmc-dev`, momenteel
gesynchroniseerd met upstream firmware `v1.17.1` met DMC-patchrevisie `.01` (builds
dragen het merk `v1.17.1.01-dutchmeshcore.nl-<hash>`). Deze site documenteert de
DMC-toevoegingen; voor algemene MeshCore-concepten verwijzen we naar het
upstream-project.

## DutchMeshCore-verwijzingen

- [MeshWiki](https://meshwiki.nl) (wiki en regio-overzicht)
- [Discord](https://discord.dutchmeshcore.nl) (community)
- [dutchmeshcore.nl](https://dutchmeshcore.nl) (hoofdsite)
- [Toolbox](https://toolbox.dutchmeshcore.nl) (kanalen, MQTT, flasher en meer)
