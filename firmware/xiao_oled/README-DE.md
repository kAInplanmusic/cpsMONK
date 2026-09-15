# XIAO S3 – Hardware‑Pinlayout

Die folgenden Pins gelten ausschließlich für die **seeed XIAO ESP32‑S3** (SKU 113991114) mit dem **OLED‑128×32‑I²C** und dem **INMP441‑I²S‑Mikrofon**.

| Pin | Function | Comment | Belegt
|-----|----------|---------|--------
| 3   | I2S_SCK  | Audio‑Clock | Ja
| 4   | I2S_WS   | Word‑Select | Ja
| 2   | I2S_SD   | Audio‑Data | Ja
| 5   | TFT_CS   | OLED Chip‑Select | Ja
| 6   | TFT_DC   | OLED Data/Command | Ja
| 7   | TFT_RST  | OLED Reset | Ja
| 8   | TFT_BL   | OLED Back‑Light (PWM) | Ja
| 11  | MOSI     | SPI‑Flash Data out | Ja
| 12  | SCK      | SPI‑Flash Clock | Ja
| 13  | MISO     | SPI‑Flash Data in | Ja
| 4   | –        | PMK‑Pin 3? (nicht genutzt) | Belegt: 3
| 5   | –        | PMK‑Pin 4? (analog?) | Belegt: 4
| 8   | –        | PMK‑Pin 8? | Belegt: 8
| 9   | –        | PMK‑Pin 9? | **belegt (versatile)**
| 10  | –        | PMK‑Pin 10? | **belegt**
| 14  | –        | PMK‑Pin 14? | **Belegt** (z. B. UART RX)
| 15  | –        | PMK‑Pin 15? | **Belegt**
| 16  | –        | PMK‑Pin 16? | **Belegt**

**Anmerkung** – Nur die aufgeführten Pins sind realistisch belegt. Alle „PMK‑Pins“ (X‑Morph‑Pins) gelten als belegt bzw. existieren nicht im Board‑Layout.

---

**Grafik** (Pin‑belegung nach EAGLE/PCB‑Design)

> 👉 Für weitere Details siehe `firmware/xiao_oled/README‑DE.md` – dort sind Grafiken / Schematics definiert.

---

*Hinweis:* Dieser Board‑Pin‑Out ist **nicht** identisch mit dem grafischen **Pin‑Assignment** der alten TFT‑Firmware. Du solltest daher das aktuellste Dokumentation‑File nutzen.
