# cpsMONK — Hardware-Inventar

Stand: 14.09.2026 · Pflege: bei jedem neuen Bauteil ergänzen.
Zweck: Grundlage für weitere cpsMONK-Varianten und Iterationen.

Legende Eignung: **A** = direkt verwendbar für cpsMONK-Kern · **B** = nur für konkreten Zusatzbedarf · **C** = für hochwertige Audioverarbeitung ungeeignet · **?** = Identifikation noch offen.

---

## 1. Kern-Bauteile (aktueller cpsMONK-Aufbau)

| # | Bauteil | Schnittstelle | Spannung | Eignung | Status |
|---|---|---|---|---|---|
| 01 | Seeed XIAO ESP32-S3 (SKU 113991114, **nicht** Sense) | USB-C, I²S, I²C, SPI | 3V3 | A | im Einsatz |
| 13 | INMP441 I²S MEMS-Mikrofon | I²S (SCK/WS/SD/L/R) | 3V3 | A | im Einsatz |
| 03 | GoldenMorning GME12864-41 OLED 0.96", 128×64, SSD1315Z | I²C oder 3-/4-Wire SPI, 30-Pin-Flex | 3V3 | A | verfügbar |

### Aktuelle Pinbelegung (verbindlich, aus README-DE.md)
```
OLED  → XIAO:  GND=GND · VCC=3V3 · SCL=D5/GPIO6 · SDA=D4/GPIO5
INMP441 → XIAO: VDD=3V3 · GND=GND · SCK=D8/GPIO7 · WS=D9/GPIO8 · SD=D10/GPIO9 · L/R=GND
```
Board-Aufdruck (D4, D8 …) ist **nicht** die GPIO-Nummer. D4 = GPIO5, D8 = GPIO7.

---

## 2. Weitere verfügbare Bauteile

### 02 · 0.66" OLED (Aufdruck "0.66 OLED")
- **Auflösung**: vermutlich 64×48 — **ungesichert**
- **I²C-Adressen laut Platine**: 0x3C, 0x3D
- **Anschlüsse**: RST, A0, D0, D1–D8, GND, 5V, 3V3 → stark ungewöhnliche Beschaltung, offenbar **mehrere Interface-Varianten** (I²C *und* SPI gemultiplext)
- **Eignung**: B/C — sehr klein, Controller unklar
- **Fallstrick**: A0/D0–D8 deuten auf SPI-Variante hin; ohne Datenblatt ist nicht sicher, ob I²C überhaupt nutzbar ist. Vor Einsatz Display-Controller auslesen (0x3C/0x3D-Scan reicht **nicht** als Beweis — SSD1306, SSD1315, SH1106 und CH1116 haben überlappende Adressen und unterschiedliche Init-Sequenzen).
- **TODO**: Controller bestimmen, dann Adafruit_SSD1306 vs. U8g2 entscheiden.

### 04 · W25Q128JV SPI-Flash-Modul (Aufdruck "SPI FLASH W25Q128JV")
- **Chip**: Winbond W25Q128JV, 128 Mbit = **16 MB**
- **Pins**: VIN, GND, 3V3, SCK, MOSI, MISO, CS
- **Eignung**: A (für Logging/Archiv), aber **nicht** als Firmware-Speicher
- **Nutzen für cpsMONK**: dauerhafte Ablage der Messreihen (WAV, CSV, JSON), wenn der RAM-Only-Ansatz nicht mehr reicht (aktuell max. 65 s PCM16 in PSRAM).
- **Fallstricke**:
  - **3V3 strikt**: W25Q128JV ist kein 5-V-Teil. Die Platine hat VIN *und* 3V3 — welcher Pin der Versorgungseingang ist, ist ohne Messung unklar. **Vor Anschluss mit Multimeter prüfen**, ob VIN in Reihe zu einem Regler/LDO liegt oder direkt am Chip anliegt.
  - SPI-Bus muss mit dem OLED geteilt werden können → im XIAO-Aufbau sind D0/D1/D2/D3/D6/D7 noch frei; D8/D9/D10 sind vom INMP441 belegt.
  - Der Chip **ist 3,3 V, aber 5-V-tolerant nur an einigen Pins** — Datenleitungen nicht vom 3V3-ESP32 auf 5-V-Pegel kreuzen.
  - Für 16 MB braucht es Sektorverwaltung (4 KB Sektoren) → eigene Log-Schicht, nicht einfach `File.write`.

### 05 · Iduino DS18B20 (Platinenkennung K845752)
- **Sensor**: DS18B20, digitaler 1-Wire
- **Pins**: DAT, VCC, GND
- **Eignung**: A (nur für thermische Kontextdaten)
- **Nutzen für cpsMONK**: nicht Teil der Akustikmessung. Sinnvoll als **Kontext-/Drift-Messung**: Wenn du Maschinentyp, Spulenalter oder Umgebung vergleichen willst, ist ein Temperaturwert pro Messreihe ein ehrlicher Zusatzkontext.
- **Fallstricke**: 1-Wire braucht **4,7 kΩ Pullup** von DAT nach 3V3 (auf dem Modul meist vorhanden — prüfen). Bei 3,3 V Versorgung funktioniert das Modul, oft aber mit dem Widerstand für 5 V bestückt.
- **Wichtig**: Temperatur sagt **nichts** über Schlagkraft oder Nadeltiefe aus. Nicht als Qualitätsindikator in die Formähnlichkeit hineinrechnen.

### 06 · Iduino MOS Module 140C07 (MOSFET-Leistungsschalter)
- **Aufdruck**: MOS Module 140C07 · großer TO-220-MOSFET · **wahrscheinlich IRF520**
- **Pins**: SIG, VCC, GND (Steuerseite) · VIN, GND, V+, V− (Lastseite)
- **Eignung**: B
- **WARNUNG (schon notiert, gilt weiter)**: Ein klassischer IRF520 ist **kein Logic-Level-MOSFET**. Bei 3,3 V Gate-Spannung ist er nicht sicher durchgesteuert → er bleibt teils im linearen Bereich → **hohe Verlustleistung, Hitze, ggf. Zerstörung**, und die Schaltung verhält sich nicht wie geplant.
- **Regel**: Nur für kleine Lasten, und dann Gate-Widerstand 100 Ω + Pulldown 10 kΩ vorsehen. Für 12-/24-V-Lasten **nicht** blind verwenden.
- **Nutzen für cpsMONK**: **Gerät führt selbst keine Motorsteuerung aus** (siehe README). Das bleibt so. Das Modul ist also derzeit **nicht** Teil des Messaufbaus — es wäre nur interessant, falls du später z. B. eine LED/Marker/Motorfreigabe steuern willst.
- **Alternative, falls Lastschaltung wirklich nötig wird**: IRLZ44N oder IRL540N (Logic-Level) statt IRF520.

### 07 · DFRobot Gravity Analog 20A Current Sensor (SKU SEN0214)
- **Basis**: ACS712, Hall-Effekt, **galvanisch getrennt**
- **Bereich**: 0…±20 A DC, bis ~17 A RMS AC
- **Versorgung**: **5 V** · Analogausgang · Gravity-3-Pin + Hochstrom-Schraubklemmen
- **Eignung**: A (als unabhängiger Referenzsensor) — genau der Sensor, der im README als fehlend benannt ist!
- **Nutzen für cpsMONK**: Das README sagt: „Gleichförmige Fremdgeräusche können als valide Reihe erscheinen. Auch ohne Timingwarnung ist deshalb ein Vergleich mit unabhängigem optischem/elektrischem Referenzsensor nötig. Der Sensor ist hier nicht enthalten." → **Dieser Sensor ist genau das.** Die Spule der Tattoomaschine zieht im Schlagzyklus gepulsten Strom. Damit hast du eine **elektrische** Ereignisreferenz zur akustischen Erkennung → Doppel-/Fehlerkennungen werden nachweisbar statt vermutet.
- **Fallstricke**:
  - **5-V-Versorgung, aber Analogausgang ist nicht auf 3,3 V begrenzt!** ACS712-Ausgang schwingt um Vcc/2 = 2,5 V. Am ESP32-S3-ADC (max ~3,1 V messbar, Referenz standardmäßig 3,3 V mit Abschwächung) ist der Bereich **grenzwertig**: ohne Signal 2,5 V sind messbar, Spitzen bis 5 V würden den ADC zerstören. **Spannungsteiler oder Level-Shifter zwingend.**
  - ACS712-20A hat 100 mV/A → Rauschen relativ hoch; für feine Auflösung weniger gut. Als Trigger/Referenz für „da war ein Schlag" aber völlig ausreichend.
  - Der Sensor misst die **Spulenspannung/den Maschinenstrom**, nicht den mechanischen Schlag. Er ist eine **Korrelation**, kein Kraftbeweis.
- **TODO**: Mit dem Analog Voltage Divider V2 (Pos. 11) kombinieren — der ist genau dafür da.

### 08 · OpenELAB DC/DC Step-Down (Buck Converter)
- **Ein**: 4,5–28 V · **Aus**: 0,8–20 V · max 3 A · **wahrscheinlich MP1584**
- **Eignung**: A (Versorgung)
- **Nutzen für cpsMONK**: Versorgung des Messboards aus dem Maschinen-Netzteil — **aber nur, wenn galvanische Trennung gewährleistet ist**. Sonst holst du dir Störungen und Potenzialverschiebungen direkt ins Audiosystem.
- **Fallstricke**: Schaltregler sind **Störquellen** (typisch 500 kHz Schaltfrequenz + Harmonische). Für ein Akustikmessgerät gilt: nicht in die Nähe des INMP441, Ausgang gut filtern (Ferrit + 10 µF + 100 nF), und **das aktuelle Setup läuft absichtlich über USB** (README: „USB-C — PC / separate geeignete USB-Versorgung"). Von der Maschinenversorgung zu speisen ist ein **Rückschritt**, solange nicht sauber getrennt.

### 09 · KY-028 Temperatur-Sensor-Modul
- **NTC + LM393-Komparator**, Potentiometer für Schwelle
- **Pins**: A0 (analog), G, +, D0 (digital/Schwellwert)
- **Eignung**: C (für cpsMONK-Messung) · B (als grober Thermostat)
- **Fallstricke**: NTC ist **nicht linear** und **nicht kalibriert** — die absolute Temperatur ist unbrauchbar, ohne eigene Kalibrierung. Der DS18B20 (Pos. 05) liefert echte Zahlen → KY-028 nur nutzen, wenn du einen simplen Schwellwert brauchst.
- **Nicht** für die Messung relevant.

### 10 · ACS712 Stromsensor-Modul (blau)
- **Hall-Effekt, galvanisch getrennt, Analogausgang, typisch 5 V**
- **Variante offen**: 05 A / 20 A / 30 A → **nicht** aus der Platine bestimmbar, IC-Aufdruck nötig
- **Eignung**: A, sobald die Variante bekannt ist
- **Identifikations-Hinweis**: Der ACS712-IC ist bei **allen drei** größen gleich beschriftet (nur die letzte Zeile nennt 05B/20A/30A). Ein Foto ohne IC-Aufdruck reicht nicht.
- **Fallstricke**: Siehe Pos. 07 — gleiche 5-V-/ADC-Problematik, gleicher zwingender Spannungsteiler.
- **Falls 05A**: wesentlich besser für feine Stromauflösung (185 mV/A) als die 20A-Version.

### 11 · Analog Voltage Divider V2
- **Eingang < 25 V** · Schraubklemme → JST zum Controller
- **Eignung**: A (Zubehör, sofort nützlich)
- **Nutzen für cpsMONK**: **Genau das fehlende Bindeglied für Pos. 07 und 10.** Der ACS712-/SEN0214-Ausgang (bis 5 V) wird damit auf einen ESP32-sicheren Pegel gebracht, und/oder die Maschinenversorgung (12/24 V) kann überwacht werden.
- **Fallstricke**: Das Ding teilt nur die Spannung — es ist **kein** Schutz gegen Überspannung über dem Teiler-Nennbereich. Bei 25 V Nenneingang nicht mit Spannungsspitzen aus einer Spule rechnen lassen. Vor dem ADC noch eine Schottky-Diode gegen GND und einen Serienwiderstand setzen, dann ist der ADC robust.

### 12 · Iduino Sound Sensor 140C001
- **Elektret-Kondensatormikrofon + LM393**, Potentiometer, digitaler Schaltausgang
- **Eignung**: **C**
- **Begründung (fachlich, wichtig für die Entscheidung)**: Der Ausgang ist ein **Komparator-Schwellwert**, kein Audiosignal. Du bekommst ein „laut/leise"-Bit, aber **keine Wellenform** — und die Wellenform ist der Kern von cpsMONK (Formähnlichkeit über 128 Punkte). Keine Samplerate, keine Amplitudenauflösung, kein Timing auf Sample-Ebene.
- **Nur sinnvoll für**: grobe „ist es laut"-Anzeige, z. B. als **dritter, redundanter Pegelwächter** neben dem INMP441-RMS — falls du eine unabhängige Bestätigung willst, dass überhaupt Schall da ist. Auch dafür ist der INMP441-RMS besser.
- **Nicht** als Ersatz oder Ergänzung des Audiosignals einsetzen.

### 14 · Mehrfach-DIP-/Konfigurationsschalter (rotes PCB)
- **Sichtbar**: mehrere Schiebeschalter, Beschriftungen VCC, GND, D0, A0 …
- **Variante offen** — Platine im Foto nur teilweise sichtbar
- **Eignung**: ? (unbekannt)
- **Falls Klarheit gewünscht**: vollständiges Foto beider Seiten + Beschriftung. Ohne das keine Aussage.

---

## 3. Nicht als Kraftmesser missverstehen

Aus dem README, hier wiederholt, weil es bei jedem zusätzlichen Sensor gilt:

> Akustik ist keine Kraftmessung. Formähnlichkeit ist akustische Wiederholbarkeit, keine Bewertung von Nadeltiefe, Schlagkraft oder Tattooqualität.

Das gilt auch für **Strom** (Pos. 07/10) und **Temperatur** (Pos. 05/09): Es sind **Kontext- und Korrelationsdaten**. Sie können bestätigen, dass ein Ereignis stattgefunden hat, aber sie messen nicht die mechanische Wirkung auf die Haut.

---

## 4. Konkrete nächste Ausbaustufen aus diesem Bestand

| Variante | Bauteile | Nutzen | Aufwand |
|---|---|---|---|
| **V1 · Referenzsensor** | 07 SEN0214 + 11 Voltage Divider | elektrische Ereignisreferenz → Doppel-/Fehlerkennung nachweisbar statt vermutet. **Direkt Umsetzung des im README benannten offenen Punktes.** | mittel |
| **V2 · Größeres Display** | 03 GME12864-41 (128×64) | doppelte Höhe vs. 128×32 → Pegelbalken + Klartext + CPS gleichzeitig darstellbar. Nur SSD1315-Init prüfen. | klein |
| **V3 · Persistente Messreihen** | 04 W25Q128JV | Messungen archivieren statt RAM-only, Verlauf über Serien hinweg vergleichbar. | groß (eigene Log-Schicht) |
| **V4 · Nur Live-Pegel** | 01 + 13 + 02 | minimaler Aufbau, sehr kleines Display, wenn nur der Abstandsindikator gebraucht wird. | klein |
| **V5 · Kontext-Logging** | 05 DS18B20 | Temperatur pro Messreihe als Kontext, nicht als Qualität. | klein |

---

## 5. Offene Identifikationen

- [ ] Pos. 02 — echter Controller des 0.66"-OLED (SSD1306? SSD1315? CH1116? SH1106?)
- [ ] Pos. 10 — ACS712-Variante (05B/20A/30A) → IC-Aufdruck fotografieren
- [ ] Pos. 14 — genaue Modellnummer des DIP-Schalter-Moduls
- [ ] Pos. 04 — ist VIN des W25Q128JV-Moduls ein Reglereingang oder direkt der Chip-Pin?
- [ ] Pos. 03 — GME12864-41: I²C-Modus auf der Platine aktiv oder SPI? Jumper/Lötbrücken prüfen.
