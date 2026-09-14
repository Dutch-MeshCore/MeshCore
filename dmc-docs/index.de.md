# DutchMeshCore Firmware

DutchMeshCore liefert Repeater-Firmware für das niederländische MeshCore-Netzwerk in
drei Varianten: Repeater mit Filtern (`dmc-dev`), Repeater mit Filtern und PacketLog
(`dmc-dev-packetlog`) und Observer mit On-Device-MQTT und OTA (`dmc-observer-dev`).

## Ich möchte

- Ein Board auswählen: [Boards](boards.md)
- Firmware flashen: [Releases](releases.md)
- Von älterer Firmware aktualisieren: [Migration](migration.md)
- Über den Browser konfigurieren: [Webpanel](web-panel.md)
- Automatisieren oder skripten: [Repeater-API](api.md)
- Firmware selbst bauen: [Lokale Builds](local-builds.md)
- DMC-spezifische Befehle: [Custom CLI](custom-cli.md)

## Endanwender-Anleitungen

Beginne bei [Boards](boards.md), um Hardware zu wählen, flashe über
[Releases](releases.md), [migriere](migration.md) ein vorhandenes Gerät und
konfiguriere es über das [Webpanel](web-panel.md).

## Entwicklerhinweise

Baue Firmware lokal mit PlatformIO: siehe [Lokale Builds](local-builds.md). Die Befehle,
die DMC zusätzlich zu Upstream bereitstellt, stehen in [Custom CLI](custom-cli.md).

## Aktueller Umfang

DutchMeshCore ist ein reiner Downstream-Fork von
[meshcore-dev/MeshCore](https://github.com/meshcore-dev/MeshCore). Änderungen fließen von
Upstream ein, nie umgekehrt. Der Basis-Branch ist `dmc-dev`, aktuell mit der
Upstream-Firmware `v1.17.1` und DMC-Patchstand `.01` synchronisiert (Builds tragen die
Kennung `v1.17.1.01-dutchmeshcore.nl-<hash>`). Diese Seite dokumentiert die
DMC-Ergänzungen; allgemeine MeshCore-Konzepte findest du im Upstream-Projekt.

## DutchMeshCore-Verweise

- [MeshWiki](https://meshwiki.nl) (Wiki und Regionsübersicht)
- [Discord](https://discord.dutchmeshcore.nl) (Community)
- [dutchmeshcore.nl](https://dutchmeshcore.nl) (Hauptseite)
- [Toolbox](https://toolbox.dutchmeshcore.nl) (Kanäle, MQTT, Flasher und mehr)
