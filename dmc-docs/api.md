# Repeater-API

De DMC-"API" is de repeater/observer-CLI, bereikbaar via verschillende transports. Er is
**geen REST-`/api/*`-interface** in DMC.

## Wat het is

Elk `get`/`set`-commando dat je op de seriële console typt, kun je ook programmatisch
sturen. Scripts en dashboards die de status uitlezen of instellingen zetten, praten met
diezelfde CLI.

## Transports

- **USB-serieel** op 115200 baud.
- **TCP-CLI op poort 23** voor `_ethernet`-builds (RAK4631). Status via `eth.status`.

## Antwoordcontract

Commando's geven een tekstueel antwoord terug. De antwoordbuffer is vast **160 bytes**.
Behandel de antwoordstrings als een stabiele afnemersinterface: onder meer mc2mqtt,
CoreScope, core-hunter en terminal.js parsen ze.

## Voorbeelden

Serieel (verstuur een commando en lees het antwoord):

```text
get mqtt.status
```

TCP op een ethernet-build:

```bash
# Verbind met de CLI op poort 23 en vraag de status op
nc <repeater-ip> 23
get mqtt.status
```

## Veelvoorkomende toepassingen

- Dashboards en scripts die de actuele status uitlezen.
- Automatisering van `set`-commando's bij het uitrollen van repeaters.
