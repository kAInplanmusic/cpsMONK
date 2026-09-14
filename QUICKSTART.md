# cpsMONK — Quick Start

> Dieses Repository enthält **zwei** Firmware-Stände. Verbindlich für die aktuell
> verbaute Hardware ist ausschließlich der **OLED-Aufbau**. Die alte
> 3,5-Zoll-TFT-Firmware unter `src/`, `include/` und `platformio.ini` liegt
> weiterhin im Repo, passt aber nicht zu deinem Board.

Maßgeblich und vollständig (Bauteile, Verkabelung, Messverfahren, Bedienung,
Fehlersuche):
**[`firmware/xiao_oled/README-DE.md`](firmware/xiao_oled/README-DE.md)**

## Hardware in einem Satz

Seeed XIAO ESP32-S3 (SKU 113991114, **nicht** Sense) + 0,91-Zoll-SSD1306-I²C-OLED
(128×32) + externes INMP441-I²S-Mikrofon — beide Module an 3V3, versorgt über USB-C.

Die **verbindliche Pinbelegung** steht in Abschnitt 2 der oben verlinkten Datei. Sie
wird hier absichtlich nicht wiederholt: Zwei Listen laufen auseinander, eine nicht.
Der Board-Aufdruck (`D4`, `D8`, …) ist **nicht** die GPIO-Nummer.

## Bauen und flashen

Immer `-c platformio-oled.ini` angeben — ohne dieses Argument baut PlatformIO die
alte TFT-Firmware:

```sh
cd /home/patrick/cpsMONK
pio run -c platformio-oled.ini -e xiao-oled
pio device list                       # tatsächlichen Port ermitteln
pio run -c platformio-oled.ini -e xiao-oled -t upload --upload-port /dev/ttyACM0
```

Den Port im letzten Befehl einsetzen. Während des Uploads darf kein zweites Programm
auf dem Port geöffnet sein.

## Bedienung (ohne zusätzliche Taster)

| Taste | Wirkung |
|---|---|
| `s` | Messung starten — 1 s Ruhekalibrierung, danach Auto-Start ab 4 stabilen Impulsen |
| `r` | Messung verwerfen und zurücksetzen |
| `?` | Statuszeile: Zustand, CPS, Formähnlichkeit, Pegeleinschätzung, Rauschen, Fehler |

Alternativ über den Browser: nach dem Start sendet das Gerät den WLAN-AP
`cpsMONK-XXXX`. Zugangsdaten stehen auf dem OLED und auf USB-Serial; im Browser
ausdrücklich `http://192.168.4.1/` öffnen.

Für die Konsolenabfragen gibt es einen Helfer:

```sh
python3 tools/cpsmonk_serial.py status
```

## Sicherheit

- **Keine** elektrische Verbindung zur Tattoomaschinen-Versorgung, zur Spule oder zur
  Nadel. Die Messung ist rein akustisch.
- Beide Module an 3V3 — das INMP441 **niemals** an 5 V.
- Kurze Leitungen, Abstand zu Spule und Leistungskabeln, erste Tests auf der Werkbank
  ohne Anwendung an Menschen.

## Was dieses Gerät nicht tut

**Akustik ist keine Kraftmessung.** CPS sind erkannte akustische Impulse pro Sekunde;
Formähnlichkeit ist akustische Wiederholbarkeit. Beides sagt nichts über Nadeltiefe,
Schlagkraft oder Tattooqualität aus. Auch 100 % Ähnlichkeit können bei einer konstant
falsch eingestellten Maschine entstehen. Das Gerät steuert die Maschine nicht.
