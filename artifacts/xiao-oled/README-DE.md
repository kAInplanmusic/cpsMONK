# cpsMONK – XIAO ESP32-S3 + SSD1306 128×32 + INMP441

Dieses zusätzliche Firmware-Profil ist für die drei tatsächlich vorhandenen Bauteile bestimmt. Die alte TFT-Firmware unter `src/`, `include/` und `platformio.ini` bleibt unverändert. **Immer `-c platformio-oled.ini` angeben.** Ohne dieses Argument wird die alte, nicht zu deinem OLED passende Firmware gebaut!

## 1. Teile und Grenzen

Vorhanden: normaler Seeed XIAO ESP32-S3 (SKU 113991114, NICHT Sense), 0.91-Zoll SSD1306-I²C-OLED 128×32 mit GND/VCC/SCL/SDA, externes INMP441-I²S-Mikrofon.
Zusätzlich zum Aufbau: USB-C-Datenkabel, kurze isolierte Leitungen, ggf. eingelötete Stiftleisten/Breadboard, die zum XIAO gehörende WLAN-Antenne. Empfohlen: 100 nF Keramikkondensator direkt zwischen INMP441 VDD/GND, sofern auf dem Modul nicht bereits vorhanden; ggf. 10 µF parallel an der Versorgung. Nicht zwingend: optionaler 100-kΩ-Pulldown SD→GND, wenn das Mikrofonmodul diesen nicht besitzt; hält die Datenleitung in hochohmigen I²S-Zeiten definiert. Keine SD-Karte, Kamera, Zusatzbuttons, Cloud oder API-Keys erforderlich.

Das normale XIAO ESP32-S3 besitzt ebenfalls 8 MB PSRAM. Die Firmware nutzt diesen Speicher für 500 Kurven und eine Audioaufnahme. Bei fehlender PSRAM-Erkennung wird nicht mit halber Funktion weitergemessen.

**Akustik ist keine Kraftmessung.** CPS sind hier erkannte akustische Impulse/s. Nur wenn ein solcher Impuls genau einem mechanischen Zyklus entspricht, entspricht das den tatsächlichen Maschinen-CPS. Vor-/Rückschlag, Kontaktfunken, Motorgeräusche und Resonanzen können andere Ereignisse liefern. Formähnlichkeit ist akustische Wiederholbarkeit, keine Bewertung von Nadeltiefe, Schlagkraft oder Tattooqualität. Auch 100 % Ähnlichkeit können bei einer konstant falsch eingestellten Maschine entstehen.

## 2. Verbindlicher Verkabelungsplan

USB abziehen, bevor Leitungen umgesteckt oder gelötet werden. Nur die aufgedruckten Pin-NAMEN verwenden, nicht die Position nach irgendeinem Modulbild vermuten. Das gelieferte Displayfoto wurde in dieser Sitzung nicht überprüft; SSD1306/128×32 ist die vom Nutzer genannte Hardwareannahme.

### OLED → XIAO

| OLED-Pin | XIAO-Aufdruck | ESP32-GPIO | Funktion |
|---|---|---|---|
| GND | GND | – | gemeinsame Masse |
| VCC | 3V3 | – | 3,3-V-Versorgung |
| SCL | D5 | GPIO6 | I²C-Takt |
| SDA | D4 | GPIO5 | I²C-Daten |

### INMP441 → XIAO

| Mikrofon-Pin | XIAO-Aufdruck | ESP32-GPIO | Funktion |
|---|---|---|---|
| VDD | 3V3 | – | 3,3-V-Versorgung |
| GND | GND | – | gemeinsame Masse |
| SCK | D8 | GPIO7 | I²S Bit Clock, vom ESP32 zum Mikrofon |
| WS | D9 | GPIO8 | I²S Word Select/LRCLK, vom ESP32 zum Mikrofon |
| SD | D10 | GPIO9 | Audiodaten, vom Mikrofon zum ESP32 |
| L/R | GND | – | linker I²S-Kanal; NICHT offen lassen |

```text
XIAO 3V3 ─┬── OLED VCC
          └── INMP441 VDD
XIAO GND ─┬── OLED GND
          ├── INMP441 GND
          └── INMP441 L/R
XIAO D4 / GPIO5 ─── OLED SDA
XIAO D5 / GPIO6 ─── OLED SCL
XIAO D8 / GPIO7 ─── INMP441 SCK
XIAO D9 / GPIO8 ─── INMP441 WS
XIAO D10/GPIO9 ─── INMP441 SD
USB-C ─── PC / separate geeignete USB-Versorgung
```

**D4 ist GPIO5, nicht GPIO4. D8 ist GPIO7, nicht GPIO8.** Pins D0…D3, D6/D7 bleiben für diesen Aufbau frei. Keine Rückseitenpads erforderlich. Das Mikrofon ist I²S, nicht I²C; SCK des Mikrofons darf nicht an OLED-SCL.

Beide Module an 3V3, niemals INMP441 an 5V! Damit bleiben auch vorhandene OLED-I²C-Pullups auf 3,3 V. Falls dein OLED-Modul wider Erwarten nur an 5 V funktioniert, nicht einfach umstecken: zuerst Modulschaltung/Pullup-Spannung prüfen. Bei fehlenden I²C-Pullups je 4,7 kΩ von SDA/SCL nach 3V3 ergänzen; übliche OLED-Breakouts besitzen sie bereits.

Keine elektrische Verbindung zur Tattoomaschinen-Versorgung, Spule oder Nadel herstellen. Messung ausschließlich akustisch. Kurze Leitungen (möglichst 5–10 cm), GND nahe bei Takt-/Datenleitungen, Abstand zu Spule und Leistungskabeln. Keine offene Elektronik im Behandlungsbereich; erste Tests auf der Werkbank ohne Anwendung an Menschen. Mikrofonöffnung freihalten, nicht zukleben oder mit Flussmittel/Lötzinn verschließen. Mikrofon starr positionieren, aber nicht an die vibrierende Maschine schrauben: Körperschall verändert die Messbedingungen.

WLAN-Antenne vorsichtig senkrecht auf den U.FL-Stecker drücken, nicht schräg hebeln. Vor WLAN-Betrieb anschließen.

## 3. Software bauen und flashen

Im Projektverzeichnis:

```sh
cd /home/patrick/cpsMONK
pio run -c platformio-oled.ini -e xiao-oled
pio device list
# /dev/ttyACM0 ist ein Beispiel: tatsächlichen USB-Port einsetzen!
pio run -c platformio-oled.ini -e xiao-oled -t upload --upload-port /dev/ttyACM0
pio device monitor --port /dev/ttyACM0 --baud 115200
```

PlatformIO lässt sich bei Bedarf mit `pipx install platformio` installieren; `~/.local/bin` muss im PATH sein. Abhängigkeiten lädt PlatformIO beim ersten Build. Das Profil pinnt Espressif32 7.1.1, Arduino-ESP32 2.0.17 über diese Plattform, SSD1306 2.5.13, Adafruit GFX 1.12.1 und ArduinoJson 6.21.5. UI ist eingebettet: **kein uploadfs notwendig**.

Falls USB nicht erscheint: Datenkabel prüfen, BOOT gedrückt halten, RESET kurz drücken, BOOT loslassen; Portliste erneut ansehen und flashen. Nach Upload RESET drücken. Keine fremden /dev/ttyS*-Ports auf Verdacht auswählen. Unter Linux können serielle Zugriffsrechte bzw. Gruppenmitgliedschaft fehlen. Während des Uploads keinen zweiten Serial-Monitor offenhalten.

`firmware.bin` alleine gehört nicht an Adresse 0. Der reguläre PlatformIO-Upload schreibt Bootloader, Partitionstabelle und App an die richtigen Adressen. Das vorbereitete Release-Bundle enthält außerdem ein zusammengeführtes `cpsmonk-xiao-oled-merged.bin` für Flash-Adresse 0; siehe dessen FLASH.txt. Ein Upload ersetzt die vorhandene Firmware des angeschlossenen Boards.

## 4. Start und Bedienung

1. Verkabelung im stromlosen Zustand prüfen; USB einschalten.
2. OLED wird auf 0x3C, danach 0x3D gesucht. Es ist monochrom; frühere TFT-Farbcodes sind hier nicht möglich. Bei fehlendem OLED bleibt USB/WLAN verwendbar.
3. Das OLED zeigt abwechselnd SSID `cpsMONK-XXXX` und ein zufälliges WLAN-Passwort. Beides wird zusätzlich auf USB-Serial ausgegeben. Passwort wird bei jedem Neustart neu erzeugt.
4. Smartphone/Computer mit diesem WLAN verbinden. „Kein Internet“ ist normal: Verbindung beibehalten, ggf. automatische Mobilfunk-/WLAN-Umschaltung deaktivieren.
5. Im Browser ausdrücklich **http://192.168.4.1/** öffnen. Keine Internetverbindung und keine App-Installation nötig. Die bisherige GitHub-Pages-Webapp ist davon unabhängig und wird nicht verändert.
6. Maschine AUS, Umgebung ruhig, Mikrofonposition fixieren. „Messung starten“ drücken. Eine Sekunde lang wird der Ruhepegel aufgenommen.
7. Erst wenn Browser/OLED „Aufnahme läuft / Motor AN“ zeigen, Maschine einschalten. 500 erkannte Impulse werden gesammelt; danach Ergebnis eingefroren. Spätestens nach 60 Sekunden Aufnahme ohne 500 Impulse erfolgt Abbruch statt einer erfundenen Messzahl.
8. Maschine ausschalten, CPS, Formähnlichkeit, Timing-/Amplitudenstreuung und Warnungen prüfen. Alle Kurven überlagern, Einzelimpulse mit Schieberegler auswählen.
9. JSON, CSV und WAV herunterladen, BEVOR eine neue Messung/Reset/Neustart erfolgt. Daten liegen nur im RAM und werden nicht dauerhaft gespeichert.

USB-Befehle: `s` startet, `r` verwirft/reset, `?` gibt Status aus. Diese Befehle funktionieren ohne zusätzliche Taster. Gerät führt selbst keine Motorsteuerung aus.

WLAN ist ein lokaler passwortgeschützter Access Point, keine Internet-/Remoteverwaltung. Alle verbundenen Clients dürfen die Messung bedienen. Auf maximal zwei Clients begrenzt. HTTP innerhalb dieses WLANs ist nicht Ende-zu-Ende verschlüsselt; keine Portfreigaben einrichten. Ein zweiter Client oder USB kann eine Messung ändern; für reproduzierbare Bedienung nur eine Steuerquelle nutzen.

## 5. Wie gemessen wird

- INMP441: Standard-I²S, linker Kanal, 24 gültige Audiobits in 32-Bit-Slots, 32 kHz. Der ESP-IDF-Treiber taktet auch bei ONLY_LEFT zwei Slots: 64 Bitclocks je Stereo-Frame. Erwartet: WS 32 kHz, SCK 2,048 MHz. Keine MCLK-Leitung erforderlich.
- Normierung der vorzeichenbehafteten 32-Bit-Wörter auf ca. −1…+1. DC-Blocker mit Koeffizient 0,98 (etwa 103-Hz-Eckfrequenz). Das ist Filterung der Audio-Trägerschwingung, nicht ein Hochpass auf der CPS-Ereignisfolge.
- Eine Sekunde Ruhekalibrierung, erste 10 ms Einschwingzeit ausgenommen. Erkennungsschwelle = max(Mindestschwelle, Rausch-RMS × Rauschfaktor).
- Betragshüllkurve, Hysterese, Sperrzeit gegen Doppelerkennung. Defaultbereich 20–200 CPS; Sperrzeit ist 0,75 / maxCps Sekunden. Das Minimum ist eine Plausibilitätsgrenze, kein Filter, der langsamere Geräusche automatisch ausschließt.
- Nach jeder Schwellüberschreitung wird während 1 ms die stärkste absolute Spitze gesucht. Festes Fenster von −1,5 bis +1,5 ms um diese Spitze: 97 native Samples, linear auf 128 Darstellungs-/Vergleichspunkte gebracht. Das sind keine 128 unabhängigen Originalsamples. Kein periodennormiertes Zeitstrecken, kein zusätzlicher Lag-Suchlauf.
- Jede Kurve wird mittelwertbereinigt und auf eigene Absolutspitze normiert. Ähnlichkeit: vorzeichenbehaftete normierte Korrelation jedes Impulses gegen die anderen 499. Der Mittelwert wird für die Anzeige auf 0–100 % begrenzt. Eine Vektorsummenformel berechnet den exakten Paarmittelwert effizient; kein willkürlicher „Qualitäts-KI-Score“.
- CPS = **499 × 32000 / (Sampleindex_500 − Sampleindex_1)**. 500 Impulse spannen 499 vollständige Intervalle auf. Kalibrierung und Wartezeit vor dem ersten Impuls gehören nicht in den CPS-Nenner.
- Timing-CV: Standardabweichung der 499 Intervalle / Mittelwert × 100. Amplituden-CV analog über die 500 gefilterten Audiopeaks. Kraft lässt sich daraus nicht ableiten.
- Timingwarnung u.a. bei Intervallen außerhalb der eingestellten Grenzen, >15 % Abweichung vom mittleren Intervall oder >5 % Timing-CV; diese heuristischen Grenzen sind nicht für deine Maschine validiert. Clippingwarnung bei Eingangssamples nahe digitalem Vollpegel. Analoge Überlastung/Modulfehler unterhalb dieser Schwelle sind dadurch nicht ausgeschlossen.
- DMA-Überlaufereignis oder I²S-Lesefehler invalidiert die Aufnahme. Reale lückenlose Datenerfassung muss am Board unter Last verifiziert werden; ein Build beweist das nicht.

500 Kurven beschreiben hier jeweils den zentralen Schlagtransienten, nicht den kompletten mechanischen Zyklus. Um Nachschwingen außerhalb des 3-ms-Fensters und doppelte Ereignisse zu prüfen, gibt es die kontinuierliche Rohaufnahme.

### Exporte

- `/api/result`: JSON mit Messwerten, Einstellungen und allen 500 normierten 128-Punkt-Kurven.
- `/api/impacts.csv`: Index, Sampleindex, Zeit, Intervall, unnormierte HP-Audioamplitude und Formähnlichkeit. Erstes Intervall 0 bedeutet „kein Vorgänger“, nicht echte Periodendauer 0.
- `/api/raw.wav`: mono PCM16, 32 kHz, unfilterte Rohaufnahme ab Kalibrierungsbeginn. Von den I²S-Wörtern auf 16 Bit reduziert, keine automatische Verstärkung; leises WAV ist bei MEMS-Nennpegeln normal. Bei fehlgeschlagener Messung ebenfalls verfügbar, sofern Audio aufgenommen wurde; enthält dann ggf. die fehlerhafte Aufnahme und ist kein gültiger CPS-Nachweis.
- Sampleindizes der Impulse beziehen sich auf den Aufnahmestart INKLUSIVE Ruhekalibrierung; sie sind damit zur WAV-Datei zuordenbar. Änderungen/Reset verwerfen beides.

Maximal reserviert: 65 Sekunden PCM16 (4.160.000 Bytes) plus ca. 265 KB Analyzer in PSRAM. Aufnahme stoppt vor dieser Grenze regulär bei 500 Impulsen bzw. nach einer Sekunde Kalibrierung plus 60 Sekunden Aufnahme. Keine Aufzeichnung im Flash, keine Cloudübertragung.

## 6. Erstvalidierung und Fehlersuche

- Startabstand z.B. 5–10 cm, dann konstant halten. Zu laut: weiter weg, Übersteuerung vermeiden. Zu leise: näher heran, Mindestschwelle vorsichtig senken. Ein Vergleich zwischen Messungen ist nur bei konstantem Abstand, Winkel, Untergrund, Maschine/Bestückung und Last sinnvoll.
- Keine Erkennung: 3V3/GND prüfen, L/R wirklich an GND, SCK/WS nicht vertauscht, Mikrofonöffnung frei. Ruhekalibrierung mit laufender Maschine setzt die Schwelle fälschlich zu hoch. Roh-WAV nach Timeout ansehen.
- Etwa doppelte CPS: Vor-/Rückschlag oder Ringing werden getrennt erkannt. WAV ansehen, Frequenzbereich plausibel enger stellen, Schwelle anpassen. Keine automatische Halbierung: ohne unabhängige Referenz wäre das geraten.
- Halbe CPS: fehlende/leise Impulse, zu hohe Schwelle oder zu lange Sperrzeit. Erwartetes Maximum erhöhen (bis 200) bzw. Sensorposition prüfen.
- Gleichförmige Fremdgeräusche können als valide Reihe erscheinen. Auch ohne Timingwarnung ist deshalb ein Vergleich mit unabhängigem optischem/elektrischem Referenzsensor oder geeignetem Tachometer nötig. Der Sensor ist hier nicht enthalten.
- Wirklich belastbare Prüfung: mindestens drei Serien unter identischen Bedingungen; anschließend eine gezielte sichere Einstelländerung, Ergebnis/WAV vergleichen. Motoranlauf ist Bestandteil der 500 Impulse und kann die Statistik verändern; für stationäre Messungen ist später eine separate Anlauf-/Triggerphase sinnvoll.
- OLED schwarz: GND/VCC nicht vertauschen, 0x3C/0x3D, 128×32 und SSD1306 prüfen. Ein SH1106-/SPI-Modul braucht andere Software. Der Aufdruck „0.91 OLED“ allein garantiert den Controller nicht.
- CPS falsch trotz sauberer Kurven: WS-Abtastrate mit Logikanalysator prüfen; Sampleclock-Toleranz beeinflusst die absolute CPS-Genauigkeit. Kein Genauigkeitsversprechen ohne Kalibrierung.
- PSRAM-Fehler: normales XIAO ESP32-S3 mit PSRAM, korrektes Boardprofil und qio_opi verwenden. Sense-Beispiele mit eingebautem Mikrofon sind für diesen Aufbau falsch.

## 7. Entwicklertests und Stand

```sh
g++ -std=c++11 -Wall -Wextra -Werror -O2 firmware/xiao_oled/tests/test_analyzer.cpp -o /tmp/cpsmonk-test
/tmp/cpsmonk-test
# Optional mit Speicher-/Undefined-Behavior-Prüfung:
g++ -std=c++11 -O1 -g -fsanitize=address,undefined firmware/xiao_oled/tests/test_analyzer.cpp -o /tmp/cpsmonk-test-asan
/tmp/cpsmonk-test-asan
# UI ändern -> Header neu erzeugen -> Firmware neu bauen:
python3 firmware/xiao_oled/web/generate_header.py
# Browser-Test mit installiertem Playwright/Chromium:
PLAYWRIGHT_MODULE=/absoluter/pfad/node_modules/playwright node firmware/xiao_oled/tests/test_ui.cjs
```

Tests verwenden ausdrücklich synthetische Impulse bzw. eine simulierte HTTP-API. Sie beweisen keine mechanische Zuordnung oder Hardwarefunktion. `VERIFICATION.txt` im Release-Bundle enthält reale Build-/Testausgaben. In dieser Sitzung war kein XIAO-USB-Gerät sichtbar; Firmware wurde nicht geflasht und nicht auf echter Hardware geprüft. GitHub-Push/öffentlicher Deployment erfolgte nicht.

## Quellen

- Seeed Pinbelegung/PSRAM: https://wiki.seeedstudio.com/xiao_esp32s3_getting_started/
- INMP441-Datenblatt: https://www.mouser.com/datasheet/2/400/INMP441-1112508.pdf
- ESP-IDF I²S: https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32s3/api-reference/peripherals/i2s.html
- Treiber (64-BCLK/Frames, Overrun-Ereignisse): https://github.com/espressif/esp-idf/blob/v4.4.7/components/driver/i2s.c
- Projekt: https://github.com/kAInplanmusic/cpsMONK
