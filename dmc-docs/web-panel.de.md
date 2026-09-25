# Webpanel

Auf ESP32-Observer-Builds stellt die Firmware ein lokales Konfigurations-Webpanel bereit
(`WebConfigServer`). Das Panel ist RAM-only: es wird kein Webzustand gespeichert, und
Änderungen laufen über die CLI-`set`-Handler.

## Was es ist

Ein lokaler Webserver auf dem Gerät, mit dem du ohne externe Verbindung konfigurieren und
den Status prüfen kannst. Nur auf ESP32-Observer-Builds verfügbar.

## Zwei Modi

- **SETUP**: SoftAP mit Captive Portal. Wird beim ersten Start ohne WiFi automatisch
  geöffnet oder manuell mit `start webconfig ap`. Dient zur Eingabe der ersten
  WiFi-Zugangsdaten.
- **LAN**: an die Stationsverbindung gebunden, sobald WiFi aktiv ist, über
  `start webconfig`, mit Anmeldung per Admin-Passwort.

## Zugriff

1. Finde die IP-Adresse mit `get wifi.status`.
2. Öffne das Panel im Browser.
3. Melde dich mit dem Admin-Passwort an.

## Was es bearbeitet

Gerätename, WiFi, MQTT-Identität und -Broker sowie weitere Observer-Einstellungen, die die
CLI bereitstellt.

## OTA-Updates

### Lokale Web-OTA

Lade selbst eine Binary über den Browser hoch (ElegantOTA):

- `start ota`: bedient die Stations-IP, wenn du im WiFi bist, sonst wird ein
  `MeshCore-OTA`-Hotspot aufgebaut.
- `start ota ap`: immer über einen Hotspot.

### OTA über einen Update-Kanal (Observer/MQTT-Boards)

WiFi-Observer-Builds können eine Build vom Update-Server holen und sich selbst flashen,
ohne manuellen Upload:

- `ota check`: verfügbare Build melden.
- `ota branch`: aktuellen Kanal anzeigen.
- `ota branch dev`: künftig vom `dev`-Kanal holen.
- `ota branch stable`: künftig vom `stable`-Kanal holen.
- `ota branch default`: Kanal-Override löschen.
- `ota update`: verfügbare Build herunterladen und flashen.

Diese `ota`-Befehle sind nur auf Observer-Builds verfügbar.

## Hinweise

- Ein selbstsigniertes HTTPS-Zertifikat ist normal; der Browser warnt davor.
- Schalte das Panel aus, wenn du fertig bist, um Speicher für MQTT freizugeben.
