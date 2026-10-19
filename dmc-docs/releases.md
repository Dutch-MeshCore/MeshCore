# Releases

DMC-firmware wordt uitgebracht als kant-en-klare binaries per board en track. De
eenvoudigste manier om te flashen is de webflasher.

## Aan de slag

1. Open de [Toolbox-flasher](https://toolbox.dutchmeshcore.nl/#/flasher).
2. Kies je firmwaretype (track) en board.
3. Kies een versie (standaard de nieuwste).
4. Kies een image-type: **Update** of **Full Flash**.
5. Flash, en stel het apparaat daarna in via het [Webpaneel](web-panel.md) of de
   seriële console.

## Firmware-tracks

DMC kent drie tracks, elk gebaseerd op een branch. Alle drie zijn repeater-firmware; DMC
levert geen companion-firmware. Zoek je een companion met ontvangstfilter, SNMP en OTA, kijk
dan naar [Meshcomod (EV Fork)](https://github.com/Elektr0Vodka/meshcomod) (pre-release, staat ook in de Toolbox-flasher).

| Track (branch) | Wat het is | Bevat |
| --- | --- | --- |
| `dmc-dev` | Officiële repeater-firmware | Repeater + filters |
| `dmc-dev-packetlog` | Repeater-firmware met packetlog | Repeater + filters + packet logging |
| `dmc-observer-dev` | Observer-firmware | Repeater + filters + on-device MQTT + OTA (geen packetlog) |

In de observer-track krijgen repeaters alles; room servers krijgen wel MQTT en OTA maar
geen filter, omdat een room server per definitie niet repeat.

## Stabiele en dev-releases

- **Stabiel**: een release per versie, bijvoorbeeld `dmc-repeater-v1.17.1`.
- **Dev**: doorlopende pre-releases die vanaf de branch ter plekke opnieuw worden gebouwd:
  `dmc-repeater-dev`, `dmc-repeater-packetlog-dev` en `observer-mqtt-dev`. Ze draaien nu op
  `v1.18.0` (upstream MeshCore v1.18.0, samengevoegd op 2026-10-15). Een dev-build meldt
  `v1.18.0-dev-dutchmeshcore.nl-<hash>` in `ver`; de bestandsnaam heeft geen `-dev`. De
  Toolbox-flasher toont ze als `1.18.0-dev`.

## Bestandskeuze

- `*.bin`: incrementele update voor een apparaat dat al DMC-firmware draait.
- `*-merged.bin`: volledige flash-image vanaf adres `0x0` voor een schone installatie.

## Setup na het flashen

- **`dmc-dev` (repeater)**: configureer via seriële console of het
  [Webpaneel](web-panel.md).
- **`dmc-dev-packetlog`**: zoals de repeater; geen extra configuratie voor packetlog.
- **`dmc-observer-dev` (observer)**: stel wifi in en `set mqtt.iata <code>`, kies een
  broker-preset. Zie [Custom CLI](custom-cli.md).
