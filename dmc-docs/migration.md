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

## Vanaf meshcomod (companion)

Een companion die van meshcomod naar de DMC-companionfirmware gaat, houdt zijn identiteit,
contacten, kanalen en instellingen, inclusief de CAD-schakelaar en het WiFi-netwerk. Bij de eerste
start leest de DMC-firmware de meshcomod-instellingen één keer en slaat ze op in het eigen formaat.

- Flash het **update-image** (`*.bin`, op `0x10000`), niet `*-merged.bin`: het merged image wist
  het NVS-gebied waar meshcomod de WiFi-gegevens bewaart. De rest blijft hoe dan ook staan.
- Heeft meshcomod zichzelf ooit over-the-air bijgewerkt, dan draait het mogelijk vanuit het tweede
  app-slot. Wis dan ook de bootkeuze, anders blijft het apparaat meshcomod starten:

```text
esptool.py --chip esp32s3 erase_region 0xe000 0x2000
esptool.py --chip esp32s3 write_flash 0x10000 <firmware>.bin
```

- Gegevens die meshcomod op een SD-kaart bewaart (Launcher-installaties) worden niet overgenomen.

## Laatste redmiddel

Werkt het niet stabiel, doe dan een volledige wis en flash de `*-merged.bin` schoon vanaf
`0x0`. Voer daarna de setup opnieuw uit.
