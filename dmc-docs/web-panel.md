# Webpaneel

Op ESP32 observer-builds serveert de firmware een lokaal configuratie-webpaneel
(`WebConfigServer`). Het paneel is RAM-only: er wordt geen webstatus bewaard, en
wijzigingen lopen via de CLI-`set`-handlers.

## Wat het is

Een lokale webserver op het apparaat waarmee je zonder externe verbinding kunt
configureren en controleren. Alleen beschikbaar op ESP32-observer-builds.

## Twee modi

- **SETUP**: SoftAP met captive portal. Wordt automatisch geopend bij de eerste keer
  opstarten zonder wifi, of handmatig met `start webconfig ap`. Bedoeld om de eerste
  wifigegevens in te voeren.
- **LAN**: gebonden aan de stationsverbinding zodra wifi actief is, via `start webconfig`,
  met login op het admin-wachtwoord.

## Toegang

1. Zoek het IP-adres met `get wifi.status`.
2. Open het paneel in de browser.
3. Log in met het admin-wachtwoord.

## Wat het bewerkt

Apparaatnaam, wifi, MQTT-identiteit en -brokers, en andere observer-instellingen die de
CLI beschikbaar stelt.

## OTA-updates

### Lokale web-OTA

Upload zelf een binary via de browser (ElegantOTA):

- `start ota`: serveert op het stations-IP als je op wifi zit, anders wordt een
  `MeshCore-OTA` hotspot opgezet.
- `start ota ap`: altijd via hotspot.

### OTA via update-kanaal (observer/MQTT-boards)

WiFi-observer-builds kunnen een build ophalen van de update-server en zichzelf flashen,
zonder handmatige upload:

- `ota check`: rapporteer de beschikbare build.
- `ota branch`: toon het huidige kanaal.
- `ota branch dev`: haal voortaan van het `dev`-kanaal.
- `ota branch stable`: haal voortaan van het `stable`-kanaal.
- `ota branch default`: wis de kanaaloverride.
- `ota update`: download de beschikbare build en flash die.

Deze `ota`-commando's zijn alleen beschikbaar op observer-builds.

## Opmerkingen

- Een zelfondertekend HTTPS-certificaat is normaal; de browser waarschuwt daarvoor.
- Schakel het paneel uit als je klaar bent, om geheugen vrij te maken voor MQTT.
