# Migration

Aktualisierung auf DMC (Observer) von Upstream-MeshCore oder älterer DMC-Firmware.

## Kurze Antwort

Flashe die passende DMC-Release über deine bestehende Installation. Einstellungen migrieren
nicht vollständig, also setze sie nach dem Flashen erneut.

## Bevor du beginnst

- Notiere deine aktuellen Einstellungen (Region, Frequenz, WiFi, MQTT).
- Wähle den richtigen Track und das Board in [Releases](releases.md).
- Für ein normales Update musst du nicht löschen; nutze das `*.bin`-Update-Image.

## Nach dem Flashen (erforderlich)

Setze die Kerneinstellungen über die CLI erneut (siehe [Custom CLI](custom-cli.md)):

```text
set wifi.ssid <deine-ssid>
set wifi.pwd <dein-passwort>
set mqtt.iata <code>
```

Wähle danach ein Broker-Preset und starte das Gerät neu.

## Optionale Einstellungen

- Eigentümerdaten: `set mqtt.owner <64-hex>`, `set mqtt.email <email>`
- TX veröffentlichen: `set mqtt.tx on`
- Filter- und Region-Gating-Abstimmung (siehe [Custom CLI](custom-cli.md))

## Überprüfen

```text
get wifi.status
get mqtt.status
```

Aktiviere bei Bedarf das [Webpanel](web-panel.md), um den Status zu bestätigen.

## Letzter Ausweg

Läuft es nicht stabil, führe eine vollständige Löschung durch und flashe die
`*-merged.bin` sauber ab `0x0`. Richte danach alles neu ein.
