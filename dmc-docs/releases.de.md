# Releases

DMC-Firmware wird als fertige Binaries pro Board und Track veröffentlicht. Am einfachsten
flasht du mit dem Web-Flasher.

## Erste Schritte

1. Öffne den [Toolbox-Flasher](https://toolbox.dutchmeshcore.nl/#/flasher).
2. Wähle deinen Firmware-Typ (Track) und dein Board.
3. Wähle eine Version (standardmäßig die neueste).
4. Wähle einen Image-Typ: **Update** oder **Full Flash**.
5. Flashe und richte das Gerät danach über das [Webpanel](web-panel.md) oder die serielle
   Konsole ein.

## Firmware-Tracks

DMC hat drei Tracks, jeweils auf einem Branch basierend. Alle drei sind Repeater-Firmware;
DMC liefert keine Companion-Firmware.

| Track (Branch) | Was es ist | Enthält |
| --- | --- | --- |
| `dmc-dev` | Offizielle Repeater-Firmware | Repeater + Filter |
| `dmc-dev-packetlog` | Repeater-Firmware mit PacketLog | Repeater + Filter + Packet-Logging |
| `dmc-observer-dev` | Observer-Firmware | Repeater + Filter + On-Device-MQTT + OTA (kein PacketLog) |

Im Observer-Track bekommen Repeater alles; Room Server bekommen MQTT und OTA, aber keinen
Filter, da ein Room Server per Definition nicht repeatet.

## Dateiauswahl

- `*.bin`: inkrementelles Update für ein Gerät, das bereits DMC-Firmware ausführt.
- `*-merged.bin`: vollständiges Flash-Image ab Adresse `0x0` für eine saubere Installation.

## Einrichtung nach dem Flashen

- **`dmc-dev` (Repeater)**: über die serielle Konsole oder das [Webpanel](web-panel.md)
  konfigurieren.
- **`dmc-dev-packetlog`**: wie der Repeater; keine zusätzliche Konfiguration für PacketLog.
- **`dmc-observer-dev` (Observer)**: WiFi einstellen und `set mqtt.iata <code>`, ein
  Broker-Preset wählen. Siehe [Custom CLI](custom-cli.md).
