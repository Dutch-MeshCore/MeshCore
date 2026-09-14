# Repeater-API

Die DMC-"API" ist die Repeater/Observer-CLI, erreichbar über mehrere Transports. Es gibt
**kein REST-`/api/*`-Interface** in DMC.

## Was es ist

Jeder `get`/`set`-Befehl, den du auf der seriellen Konsole eingibst, lässt sich auch
programmatisch senden. Skripte und Dashboards, die Status auslesen oder Optionen setzen,
sprechen mit derselben CLI.

## Transports

- **USB-Seriell** mit 115200 Baud.
- **TCP-CLI auf Port 23** für `_ethernet`-Builds (RAK4631). Status über `eth.status`.

## Antwortvertrag

Befehle liefern eine Textantwort. Der Antwortpuffer ist fest **160 Bytes**. Behandle die
Antwortstrings als stabile Abnehmer-Schnittstelle: unter anderem mc2mqtt, CoreScope,
core-hunter und terminal.js parsen sie.

## Beispiele

Seriell (Befehl senden und Antwort lesen):

```text
get mqtt.status
```

TCP auf einem Ethernet-Build:

```bash
# Mit der CLI auf Port 23 verbinden und Status abfragen
nc <repeater-ip> 23
get mqtt.status
```

## Häufige Anwendungsfälle

- Dashboards und Skripte, die den aktuellen Status auslesen.
- Automatisierung von `set`-Befehlen beim Ausrollen von Repeatern.
