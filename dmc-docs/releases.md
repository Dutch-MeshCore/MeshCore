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

DMC kent drie tracks, elk gebaseerd op een branch. Alle drie bouwen repeater-firmware; twee
ervan bouwen ook een companion:

- **Companion Filter** (`dmc-dev`): de companion-radio met een ontvangstfilter, plus SNMP en
  OTA op ESP32-boards met wifi. Zie [Custom CLI](custom-cli.md#companion-filter).
- **Companion Observer** (`dmc-observer-dev`): een wifi-companion-radio en een MQTT-observer
  in één image. Zie [Companion Observer](#companion-observer) hieronder.

De companion van [Meshcomod (EV Fork)](https://github.com/Elektr0Vodka/meshcomod) staat ook in
de Toolbox-flasher (pre-release).

| Track (branch) | Wat het is | Bevat |
| --- | --- | --- |
| `dmc-dev` | Officiële repeater-firmware | Repeater + filters; Companion Filter |
| `dmc-dev-packetlog` | Repeater-firmware met packetlog | Repeater + filters + packet logging |
| `dmc-observer-dev` | Observer-firmware | Repeater + filters + on-device MQTT + OTA (geen packetlog); Companion Observer |

In de observer-track krijgen repeaters alles; room servers krijgen wel MQTT en OTA maar
geen filter, omdat een room server per definitie niet repeat.

## Stabiele en dev-releases

- **Stabiel**: een release per versie, bijvoorbeeld `dmc-repeater-v1.17.1`.
- **Dev**: doorlopende pre-releases die vanaf de branch ter plekke opnieuw worden gebouwd:
  `dmc-repeater-dev`, `dmc-repeater-packetlog-dev` en `observer-mqtt-dev`. Ze draaien nu op
  `v1.18.0` (upstream MeshCore v1.18.0, samengevoegd op 2026-10-15). Een dev-build meldt
  `v1.18.0-dev-dutchmeshcore.nl-<hash>` in `ver`; de bestandsnaam heeft geen `-dev`. De
  Toolbox-flasher toont ze als `1.18.0-dev`.
- **Companions**: `dmc-companion-dev` (Companion Filter) en `dmc-companion-observer-dev`
  (Companion Observer) zijn ook doorlopende dev-releases; versie-releases heten
  `dmc-companion-vX.Y.Z` en `dmc-companion-observer-vX.Y.Z`. De Companion Observer meldt
  `vX.Y.Z[-dev]-dutchmeshcore.nl-comp-obs-<hash>`.

## Bestandskeuze

- `*.bin`: incrementele update voor een apparaat dat al DMC-firmware draait.
- `*-merged.bin`: volledige flash-image vanaf adres `0x0` voor een schone installatie.

## Setup na het flashen

- **`dmc-dev` (repeater)**: configureer via seriële console of het
  [Webpaneel](web-panel.md).
- **`dmc-dev-packetlog`**: zoals de repeater; geen extra configuratie voor packetlog.
- **`dmc-observer-dev` (observer)**: stel wifi in en `set mqtt.iata <code>`, kies een
  broker-preset. Zie [Custom CLI](custom-cli.md).
- **Companion Observer**: zie de volgende sectie.

## Companion Observer

Een companion-radio en een MQTT-observer in één image. De MeshCore-app verbindt via wifi
(TCP-poort 5000) terwijl de pakketten die de node hoort naar de ingestelde MQTT-brokers gaan.
**Nog niet op hardware getest.**

| Board | Env |
| --- | --- |
| Heltec V3 | `Heltec_v3_companion_observer_wifi` |
| Heltec V4 | `heltec_v4_companion_observer_wifi` |
| Heltec V4 expansion kit | `heltec_v4_expansionkit_companion_observer_wifi` |
| Xiao S3 WIO | `Xiao_S3_WIO_companion_observer_wifi` |
| Waveshare S3 LR1121 | `Waveshare_S3_LR1121_companion_observer_wifi` |

- **Instellen**: voer via de seriële USB-console (115200 baud) `set wifi.ssid <ssid>`,
  `set wifi.pwd <pwd>`, `set mqtt.iata <code>` en `set mqtt1.preset <naam>` uit en herstart.
  Dezelfde observer-opdrachten werken ook via de opdrachtregel in de app en via remote admin.
- **Gedeeld met de repeater-observer**: beide firmwares lezen dezelfde `/mqtt.json` en
  identiteit, dus bij heen en weer flashen blijven brokers, slots, IATA, wifi, tijdzone en
  publieke sleutel behouden. Nadat de companion zijn instellingen heeft opgeslagen, start een
  teruggeflashte repeater met zijn eigen instellingen op de build-standaard, **inclusief het
  admin-wachtwoord**: stel dat opnieuw in. Voer `set repeat on` uit als de repeater moet
  doorsturen.
- **Ontvangstfilter**: het DMC-companionfilter (`filter ...`) zit erin. Het verandert alleen
  wat de app toont en wat herhaald wordt; de observer uploadt nog steeds elk gehoord pakket.
- **Niet aanwezig**: pull-OTA, SNMP, het webportaal, storingsmeldingen, MQTT-self-heal en
  buurpublicatie. Updaten gaat via USB.
- **Heltec V3** (geen PSRAM): hooguit 150 contacten en 2 actieve MQTT-slots. Boards met PSRAM
  houden 350 contacten.

Meer in
[`MQTT_IMPLEMENTATION.md`](https://github.com/Dutch-MeshCore/MeshCore/blob/dmc-observer-dev/MQTT_IMPLEMENTATION.md#companion-observer).

## Een release bouwen (beheerders)

Releases worden op GitHub gebouwd met de knop **DMC Firmware Release**: Actions → DMC
Firmware Release → Run workflow. De workflow checkt zelf de juiste branch uit.

| Firmware | Branch | Envs |
| --- | --- | --- |
| `repeater` | `dmc-dev` | `*_repeater` |
| `room-server` | `dmc-dev` | `*_room_server` |
| `repeater-packetlog` | `dmc-dev-packetlog` | `*_repeater` |
| `repeater-mqtt` | `dmc-observer-dev` | `*_repeater_observer_mqtt` |
| `room-server-mqtt` | `dmc-observer-dev` | `*_room_server_observer_mqtt` |
| `observer-mqtt` | `dmc-observer-dev` | `*_observer_mqtt` |
| `companion` | `dmc-dev` | `*_companion_radio_ble`, `*_companion_radio_wifi` |
| `companion-observer` | `dmc-observer-dev` | `*_companion_observer_wifi` |

- **Kanaal `dev`** bouwt de doorlopende pre-release `dmc-<firmware>-dev` ter plekke opnieuw
  (`observer-mqtt-dev` voor `observer-mqtt`) met `DMC_CHANNEL=dev`. `repeater-mqtt` en
  `room-server-mqtt` zijn alleen voor releases. `dmc-companion-observer-dev` voedt alleen de
  Toolbox-flasher, nooit de OTA-server, omdat de Companion Observer geen pull-OTA heeft.
- **Kanaal `release`** maakt de concept-pre-release `dmc-<firmware>-<versie>`; controleer
  hem en publiceer hem met de hand. Een bestaande tag wordt geweigerd.
- Elk board bouwt op een eigen runner. Een board dat faalt, staat in de samenvatting van de
  run en wordt weggelaten; de rest wordt toch gepubliceerd.

Alternatieven voor de Companion Observer: de workflow **Build Companion Observer Firmwares**
(alleen handmatig: Run workflow, *Use workflow from* `dmc-observer-dev`, versie invullen)
bouwt alle boards in één job en maakt de concept-release `dmc-companion-observer-<versie>`.
Of bouw en publiceer lokaal, zie [Lokale builds](local-builds.md#firmwaresets).
