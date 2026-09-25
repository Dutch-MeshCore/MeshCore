# Migratie

Upgraden naar DMC (observer) vanaf upstream MeshCore of oudere DMC-firmware.

## Kort antwoord

Flash de bijpassende DMC-release over je bestaande installatie. Instellingen migreren niet
volledig mee: zet ze na het flashen opnieuw.

## Voordat je begint

- Noteer je huidige instellingen (regio, frequentie, wifi, MQTT).
- Kies de juiste track en board in [Releases](releases.md).
- Voor een gewone upgrade hoef je niet te wissen; gebruik de `*.bin` update-image.

## Na het flashen (verplicht)

Zet de kerninstellingen opnieuw via de CLI (zie [Custom CLI](custom-cli.md)):

```text
set wifi.ssid <jouw-ssid>
set wifi.pwd <jouw-wachtwoord>
set mqtt.iata <code>
```

Kies daarna een broker-preset en herstart het apparaat.

## Optionele instellingen

- Eigenaarsgegevens: `set mqtt.owner <64-hex>`, `set mqtt.email <email>`
- TX publiceren: `set mqtt.tx on`
- Filter- en region-gating-afstemming (zie [Custom CLI](custom-cli.md))

## Controleren

```text
get wifi.status
get mqtt.status
```

Schakel eventueel het [Webpaneel](web-panel.md) in om de status te bevestigen.

## Laatste redmiddel

Werkt het niet stabiel, doe dan een volledige wis en flash de `*-merged.bin` schoon vanaf
`0x0`. Voer daarna de setup opnieuw uit.
