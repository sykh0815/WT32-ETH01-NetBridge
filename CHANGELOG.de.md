# Änderungsprotokoll

🇬🇧 [English](CHANGELOG.md) | 🇩🇪 **Deutsch**

Alle wichtigen Änderungen an diesem Projekt stehen in dieser Datei.

Die Versionsnummern folgen dem Schema **MAJOR.MINOR.PATCH**:

- **MAJOR**: inkompatible Änderungen (z. B. Einstellungen müssen neu eingegeben werden)
- **MINOR**: neue Funktionen
- **PATCH**: nur Fehlerbehebungen (geschrieben z. B. als `2.2.1`; fehlt die dritte Stelle, ist `.0`
  gemeint)

Die aktuelle Version steht in `src/main.cpp` (`FIRMWARE_VERSION`) und wird im Webinterface und in
der seriellen Ausgabe angezeigt.

## [3.2] – 06.10.2026

### Neu
- **Optionaler Passwortschutz für das Webinterface** (Abschnitt „Passwort für dieses Webinterface“).
  Ist ein Passwort gesetzt, fragt jede Seite auf einer Anmeldeseite danach; danach bleibt der Browser
  über ein zufälliges Sitzungs-Cookie angemeldet (24 Stunden ohne Nutzung, Knopf „Abmelden“).
  Gespeichert wird nur ein gesalzener SHA-256-Hash des Passworts. Nach 5 Fehlversuchen ist die
  Anmeldung eine Minute gesperrt.
- In den Access-Point-Betriebsarten weist ein Hinweis oben auf der Seite auf ein fehlendes Passwort
  hin, weil das Webinterface dort auch aus dem Heimnetz erreichbar ist.

### Geändert
- Der **Notfall-Reset** (3-mal Strom aus/an) entfernt jetzt auch das Passwort des Webinterface.

## [3.1] – 06.10.2026

### Neu
- **Mehr Infos zu verbundenen Geräten** (WLAN-Geräte in den Access-Point-Betriebsarten, Gerät am
  LAN-Port im NAT- und Bridge-Modus):
  - **Gerätename** (z. B. „Thorstens-iPhone“), gelesen aus der DHCP-Anfrage des Geräts (Option 12
    bzw. 81)
  - **Hersteller** anhand des MAC-Präfixes (eingebaute Tabelle mit rund 8.700 Präfixen gängiger
    Heimnetz-Hersteller); zufällige Adressen erscheinen als **„Private MAC“**
  - **Verbunden seit** (WLAN-Geräte), **Signalbalken** und **WLAN-Standard** (802.11b/g/n)
  - **Restlaufzeit der DHCP-Vergabe**: bei NAT und AP-NAT vom eigenen DHCP-Server, in den
    Bridge-Modi aus der Bestätigung des Routers
  - AP-Bridge: Die vom Router vergebene IP-Adresse wird jetzt ebenfalls angezeigt
- Die Auswahl für die MAC-Liste der Firewall zeigt die Gerätenamen.
- `tools/gen_oui.py` erzeugt die Herstellertabelle neu aus der offiziellen IEEE-Liste.

### Geändert
- **Projektname ist jetzt WT32-ETH01 NetBridge** (statt „WT32 NetBridge“), damit man das Projekt bei
  der Suche nach dem Board leichter findet. Titel im Webinterface, serielle Ausgabe, Dokumentation,
  Banner und Vorschaubild verwenden den neuen Namen.
- **Das Einrichtungs-WLAN heißt jetzt `WT32-ETH01-NetBridge-Setup`**, der Name im Router
  `wt32-eth01-netbridge` (z. B. `http://wt32-eth01-netbridge.fritz.box`). Wer den WLAN-Namen nie
  geändert hat, sieht das Einrichtungs-WLAN nach dem Update unter dem neuen Namen; ein selbst
  gewählter Name bleibt erhalten.
- Kombinierte Firmware-Datei in Releases: `wt32-eth01-netbridge-vX.Y-full.bin`.

### Behoben
- Keine Fehlermeldungen `nvs_get_blob ... NOT_FOUND` mehr beim Start, solange keine Firewall-Regeln gespeichert sind.

## [3.0] – 06.10.2026

### Geändert
- **Neuer Projektname: WT32 NetBridge** (vorher „WT32-ETH01 Ethernet-WLAN-Bridge“), weil die
  Firmware inzwischen in beide Richtungen arbeitet (WLAN-Client und Access Point).
  GitHub-Repository: `WT32-ETH01-NetBridge`.
- Webinterface-Titel, serielle Ausgabe und Dokumentation verwenden den neuen Namen.
- **Standardname des Einrichtungs-WLANs ist jetzt `WT32-NetBridge-Setup`**, der Gerätename im Router
  `wt32-netbridge` (z. B. `http://wt32-netbridge.fritz.box`). Wer den WLAN-Namen nie geändert hat,
  sieht das Einrichtungs-WLAN nach dem Update unter dem neuen Namen; ein selbst vergebener Name bleibt.
- Zusammengefügte Firmware-Datei in den Releases: `wt32-netbridge-vX.Y-full.bin`.

## [2.9.1] – 06.10.2026

### Geändert
- In den Access-Point-Betriebsarten entfällt der separate Abschnitt „WLAN (Access Point)“ unten; Name
  und Passwort stehen (und ändern sich) nur noch bei der Betriebsart unter „WLAN des Access Points“.
  Die Links in den Warnhinweisen springen dorthin. In den Client-Betriebsarten bleibt der Abschnitt
  „Einrichtungs-WLAN“ erhalten.

## [2.9] – 06.10.2026

### Neu
- **Captive Portal** für das Einrichtungs-WLAN: Nach dem Verbinden öffnen Handys und Laptops die
  Einrichtungsseite automatisch (iOS-Anmeldefenster, Android „Im Netzwerk anmelden“,
  Windows/macOS-Hinweis). Die Bridge beantwortet im Einrichtungs-WLAN alle DNS-Anfragen mit
  192.168.4.1 und leitet die Verbindungstests der Betriebssysteme auf die Einrichtungsseite um.
- Nur in den Client-Betriebsarten (NAT, Bridge); in den Access-Point-Betriebsarten ist das WLAN ein
  normales Netz, dort funktioniert DNS wie gewohnt.
- Diagnosezeile im seriellen Log alle 10 s (Betriebsart, Router-Verbindung und Verbindungsversuche,
  verbundene WLAN-Geräte, Kanal, freier Speicher).

## [2.8] – 06.10.2026

### Neu
- Option **„Offenes WLAN ohne Passwort“** als eigener Haken, sowohl in den WLAN-Einstellungen als
  auch direkt bei den Access-Point-Betriebsarten. Ist er gesetzt, verschwinden die Passwortfelder und
  eine rote Warnung erklärt die Risiken (jeder kann sich verbinden, unverschlüsselt, im Bridge-Modus
  Zugriff aufs Heimnetz) mit dem Tipp, die Firewall einzuschalten.
- Solange das WLAN bewusst offen ist, bleibt oben im Webinterface ein dauerhafter (nicht blinkender)
  roter Hinweis sichtbar.

### Geändert
- Die Access-Point-Betriebsarten laufen jetzt auch mit offenem WLAN, aber nur, wenn es ausdrücklich so
  gewählt wurde. Wird später ein Passwort gesetzt, ist das offene WLAN wieder aus.

## [2.7] – 06.10.2026

### Neu
- WLAN-Name und Passwort des Access Points lassen sich direkt bei der Auswahl einer
  Access-Point-Betriebsart eintragen (die Felder erscheinen unter den Karten); ein separater Schritt
  ist nicht mehr nötig.
- **Webinterface im Modus Access Point (Bridge)**: Die Bridge holt sich jetzt per DHCP eine eigene
  Adresse vom Router und bleibt darüber erreichbar – aus dem Heimnetz und aus ihrem WLAN (z. B.
  `http://wt32-bridge.fritz.box`). Frames an die Bridge selbst gehen an ihren eigenen TCP/IP-Stack,
  alle anderen werden weiterhin durchgereicht.
- Gerätename `wt32-bridge` im Router (Client- und Access-Point-Betriebsarten).

### Geändert
- Die Warnung beim Modus Access Point (Bridge) erklärt jetzt, wo das Webinterface zu finden ist,
  statt zu sagen, dass es nicht mehr erreichbar ist.

## [2.6.1] – 06.10.2026

### Behoben
- Das Einrichtungs-WLAN war kaum erreichbar (Paketverlust, Webinterface lud nicht), solange das
  Router-WLAN nicht gefunden wurde: Die Bridge versuchte pausenlos neu zu verbinden, und jeder
  Versuch durchsucht mit demselben Funkteil alle WLAN-Kanäle. Die Verbindungsversuche kommen jetzt mit
  wachsendem Abstand (10 s, 20 s, 40 s … bis 120 s) und suchen, sobald der Router bekannt ist, nur auf
  dessen Kanal; jeder dritte Versuch sucht auf allen Kanälen, falls der Router den Kanal gewechselt
  hat.

### Neu
- Das Webinterface zeigt, warum die Verbindung zum Router scheitert (nicht gefunden / Passwort falsch)
  und wann der nächste Versuch folgt.

## [2.6] – 05.10.2026

### Neu
- **Einfache Firewall** (Abschnitt „Firewall“ im Webinterface, gilt sofort):
  - Schalter: „Nur Internet, kein Heimnetz“, „Webinterface der Bridge sperren“, „WLAN-Geräte
    voneinander trennen“ (Access-Point-Betriebsarten).
  - Bis zu 16 eigene Regeln: sperren/erlauben, Protokoll, Ziel-IP/-Netz, Port/Portbereich,
    Trefferzähler.
  - Grundregel für alles andere: erlauben oder sperren.
  - MAC-Liste für die Access-Point-Betriebsarten; verhindert das Selbstaussperren.
  - IPv6 wird bei aktiver Firewall gesperrt; Zähler für gesperrte Pakete.
- In den Bridge-Betriebsarten werden Router und DNS aus DHCP gelernt, damit sie bei „Nur Internet“
  erreichbar bleiben.

## [2.5] – 05.10.2026

### Neu
- **Access Point – eigenes WLAN-Netz (NAT)**: LAN-Port als Uplink zum Router (DHCP-Client), eigenes
  WLAN mit DHCP und NAT für bis zu ca. 8 Geräte, der DNS-Server des Routers wird weitergegeben. Das
  Webinterface bleibt unter `192.168.4.1` und über die IP der Bridge im Heimnetz erreichbar.
- **Access Point – WLAN direkt im Heimnetz (Bridge)**: Weiterleitung auf Ebene 2 zwischen Ethernet
  und Access Point (wie Espressifs `eth2ap`); die WLAN-Geräte bekommen ihre Adressen vom Router.
  Auffällige Warnung, dass das Webinterface danach nicht mehr erreichbar ist; das Umschalten
  verlangt eine ausdrückliche Bestätigung.
- **Notfall-Reset**: 3-mal hintereinander Strom aus/an (jeweils innerhalb von 10 s) setzt die
  Betriebsart auf NAT zurück.
- Der Name des WLANs der Bridge lässt sich im Webinterface ändern („WLAN-Name“).
- Webinterface in den Access-Point-Betriebsarten: Uplink-Infos (IP der Bridge im Heimnetz, Gateway)
  und Liste der verbundenen WLAN-Geräte (IP, MAC, Signal).

### Geändert
- Die Access-Point-Betriebsarten lassen sich erst wählen, wenn ein WLAN-Passwort gesetzt ist.
- Beim Ändern der WLAN-Einstellungen kann das Passwortfeld leer bleiben, dann bleibt das bisherige
  Passwort erhalten.

## [2.4] – 30.09.2026

### Neu
- Komplettes Webinterface zusätzlich auf **Englisch**, einschließlich aller Ergebnis- und
  Fehlerseiten und der per JavaScript erzeugten Texte.
- Sprachumschalter mit kleinen Flaggen-Buttons (DE / EN) oben rechts. Die Wahl wird ein Jahr lang
  im Browser gespeichert (Cookie); ohne Wahl gilt die Sprache des Browsers.
- `tools/release.sh`: baut die Firmware und legt einen GitHub-Release mit den Firmware-Dateien an
  (`bootloader.bin`, `partitions.bin`, `boot_app0.bin`, `firmware.bin`, eine zusammengefügte
  `-full.bin`, falls esptool vorhanden ist, und `SHA256SUMS.txt`) sowie zweisprachigen
  Release-Notizen aus den Changelogs.

### Geändert
- `/status` meldet zusätzlich, ob das Einrichtungs-WLAN offen ist (`apOpen`).

## [2.3] – 30.09.2026

### Geändert
- Das Einrichtungs-WLAN startet jetzt **ohne Passwort** (offen), bis im Webinterface ein Passwort
  festgelegt wird. `SETUP_AP_PASSWORD` in `src/main.cpp` ist standardmäßig leer; wer selbst baut,
  kann dort weiterhin ein festes Start-Passwort eintragen.
- Der Button im Abschnitt „Einrichtungs-WLAN“ heißt „Passwort festlegen“, solange kein Passwort
  gesetzt ist.

### Neu
- Auffällige, pulsierende rote Warnung oben im Webinterface, solange das Einrichtungs-WLAN offen
  ist, mit Link zum Passwortformular. Das Formular ist ebenfalls rot hervorgehoben.
- Warnung in der seriellen Ausgabe, wenn das Einrichtungs-WLAN offen ist.

## [2.2] – 30.09.2026

### Neu
- Das Passwort des Einrichtungs-WLANs lässt sich im Webinterface ändern (Abschnitt
  „Einrichtungs-WLAN“). Es wird im Flash gespeichert und bleibt bei Firmware-Updates erhalten.
- Warnung im Webinterface und in der seriellen Ausgabe, solange noch das Standard-Passwort des
  Einrichtungs-WLANs aktiv ist.
- Prüfung der Passwortregeln: 8 bis 63 druckbare ASCII-Zeichen, doppelte Eingabe.

### Geändert
- `SETUP_AP_PASSWORD` in `src/main.cpp` ist jetzt nur noch der Standardwert. Er gilt, bis ein
  eigenes Passwort gesetzt ist, und nach dem Löschen des Flash.

## [2.1] – 30.09.2026

### Neu
- Betriebsart **Bridge**: Das LAN-Gerät bekommt seine IP-Adresse direkt vom Router
  (MAC-Umschreibung, ein Gerät, nur IPv4). Zusätzlich zu NAT im Webinterface wählbar.
- Betriebsarten als Karten mit Piktogrammen und Angabe der Datenrate.
- Infos zum Gerät am LAN-Port: IP, MAC, Link-Geschwindigkeit/Duplex, Verbindungsdauer,
  Paketzähler im Bridge-Modus.
- Versionsnummer im Webinterface, in der seriellen Ausgabe und in `/status`.
- Projektbeschreibung und Tutorial auf Deutsch und Englisch, MIT-Lizenz.

### Behoben
- Die WLAN-Suche funktioniert jetzt auch, solange noch keine Router-Verbindung besteht
  (Verbindungsversuche werden während des Scans angehalten, der Scan läuft asynchron).
- Der DHCP-Server am Ethernet-Port startete nicht (fehlendes `ESP_NETIF_FLAG_AUTOUP`).
- Der DNS-Server wurde nicht korrekt an die DHCP-Clients weitergegeben.
- Der Oszillator des LAN8720 (GPIO16) wird eingeschaltet, bevor der EMAC startet.

## [2.0] und älter

Nur NAT-Modus, nicht im Detail dokumentiert. Die letzte reine NAT-Version liegt in
`backup/main_nat_only.cpp`.
