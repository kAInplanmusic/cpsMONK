#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ArduinoJson.h>
#include <driver/i2s.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <new>
#include <cmath>
#include "ImpactAnalyzer.h"
#include "web_ui.h"

// Board silk labels are NOT GPIO numbers. Normal XIAO ESP32-S3, not Sense.
constexpr int OLED_SDA = 5;  // D4
constexpr int OLED_SCL = 6;  // D5
constexpr int MIC_BCLK = 7;  // D8
constexpr int MIC_WS = 8;    // D9
constexpr int MIC_SD = 9;    // D10; INMP441 L/R tied to GND
// Warm-up + 20 s window + calibration, at 32 kHz PCM16, in PSRAM.
constexpr size_t RAW_SECONDS = 26;
constexpr size_t RAW_CAPACITY = cps::SAMPLE_RATE * RAW_SECONDS;
static cps::ImpactAnalyzer* analyzer = nullptr;
static int16_t* rawPcm = nullptr;
static size_t rawCount = 0;
static uint32_t captureEpoch = 0;
static SemaphoreHandle_t guard;
static QueueHandle_t i2sEvents;
static bool audioReady = false;
static bool oledReady = false;
// Held true by the 'f' console command: a full white screen that the normal UI
// must not paint over. A panel that answers on I2C and accepts the init
// sequence can still show nothing (wrong controller or geometry), and "all
// white" is the only pattern that separates "not driven" from "wrong content".
static bool oledTest = false;
// WLAN ist standardmaessig aus; das Geraet soll ohne AP benutzbar sein.
static bool wifiEnabled = false;
// Adresse, auf der das Panel geantwortet hat - fuer die Gesundheitspruefung.
static uint8_t oledAddr = 0;
// Statuszeile, ausserhalb des Mutex gefuellt und gedruckt (siehe loop()).
static char statusLine[256];
static char apPassword[13];
static char apName[24];
static WebServer server(80);
static Adafruit_SSD1306 oled(128, 32, &Wire, -1);

static bool active() {
    const auto s = analyzer->state();
    return s == cps::State::Calibrating || s == cps::State::WaitingForMotor ||
           s == cps::State::Warmup || s == cps::State::Recording;
}
// Strokes are only taken while the measurement window runs; before that the run
// is still waiting for the machine or warming up.
static bool collecting() { return analyzer->state() == cps::State::Recording; }
static const char* stateName(cps::State s) {
    switch (s) {
        case cps::State::Idle: return "idle";
        case cps::State::Calibrating: return "calibrating";
        case cps::State::WaitingForMotor: return "waiting";
        case cps::State::Warmup: return "warmup";
        case cps::State::Recording: return "recording";
        case cps::State::Complete: return "complete";
        default: return "failed";
    }
}
static const char* levelName(cps::Level l) {
    switch (l) {
        case cps::Level::Silent: return "silent";
        case cps::Level::TooQuiet: return "quiet";
        case cps::Level::Good: return "good";
        case cps::Level::Warning: return "high";
        default: return "loud";
    }
}
static bool lock() {
    if (xSemaphoreTake(guard, pdMS_TO_TICKS(100)) == pdTRUE) return true;
    server.send(503, "application/json", "{\"error\":\"Auswertung beschaeftigt; erneut versuchen\"}");
    return false;
}
static void unlock() { xSemaphoreGive(guard); }
// No button exists on this build, so a run must arm itself. Strict order:
//   1. the level indicator must CURRENTLY sit in the target band, so a run never
//      starts while the microphone is mispositioned, and
//   2. the band must have been good at least once since boot, so the firmware
//      does not start on a lucky noisy instant either.
// The second condition is a latch that survives reset() on purpose: on a board
// with no input control, clearing it would strand the device idle forever.
static bool maybeAutoStart() {
    if (!audioReady || active()) return false;
    if (!analyzer->levelWasGood()) return false;
    if (analyzer->levelAssessment() != cps::Level::Good) return false;
    // Leave a dead line and an overloaded input alone; both need the operator.
    rawCount = 0; ++captureEpoch;
    analyzer->start();
    return true;
}
static void statusInto(JsonObject j) {
    const auto& r = analyzer->result();
    j["state"] = stateName(analyzer->state());
    j["count"] = analyzer->count(); j["target"] = analyzer->target();
    j["seen"] = analyzer->seen(); j["runId"] = captureEpoch;
    j["cps"] = r.cps; j["shapeSimilarity"] = r.shapeSimilarity;
    j["periodCvPercent"] = r.periodCvPercent;
    j["amplitudeCvPercent"] = r.amplitudeCvPercent;
    j["clippedPercent"] = r.clippedPercent;
    j["noiseRms"] = r.noiseRms; j["threshold"] = r.threshold;
    j["rejected"] = r.rejected; j["timingWarning"] = r.timingWarning;
    j["clippingWarning"] = r.clippingWarning;
    j["windowTruncated"] = r.windowTruncated;
    j["provisionalCps"] = r.provisionalCps; j["windowSeconds"] = r.windowSeconds;
    j["error"] = analyzer->error();
    j["minCps"] = analyzer->config.minCps; j["maxCps"] = analyzer->config.maxCps;
    j["thresholdMultiplier"] = analyzer->config.thresholdMultiplier;
    j["thresholdFloor"] = analyzer->config.thresholdFloor;
    j["level"] = levelName(analyzer->levelAssessment());
    j["levelAmplitude"] = r.levelAmplitude; j["levelDb"] = r.levelDb;
    // Raw-input average, i.e. the DC offset on the I2S line. Combined with
    // levelAmplitude this separates a rail-stuck input from a quiet room.
    j["levelDc"] = analyzer->levelDc();
    j["levelFloor"] = cps::LEVEL_SILENT_FLOOR;
    j["autoArm"] = analyzer->levelWasGood();
    // Band edges so the UI can draw the target zone and scale the bar honestly.
    j["levelLow"] = analyzer->config.levelLow;
    j["levelHigh"] = analyzer->config.levelHigh;
    j["levelLoud"] = analyzer->config.levelLoud;
    j["levelReady"] = analyzer->levelWasGood();
    j["gateOpen"] = analyzer->gateSatisfied();
    j["autoStart"] = analyzer->config.autoStart;
    j["cpsGate"] = cps::CPS_GATE;
    j["warmupSeconds"] = cps::WARMUP_SECONDS;
    j["rawSamples"] = rawCount; j["sampleRate"] = cps::SAMPLE_RATE;
    j["wavePoints"] = cps::WAVE_POINTS; j["windowDurationMs"] = cps::WINDOW_DURATION_MS;
    j["oledDetected"] = oledReady;
}
static void getStatus() {
    if (!lock()) return;
    StaticJsonDocument<2048> d;
    statusInto(d.to<JsonObject>());
    String out; serializeJson(d, out);
    unlock();
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json", out);
}
static void beginMeasurement() {
    if (!lock()) return;
    if (!audioReady || active()) {
        unlock(); server.send(409, "application/json", "{\"error\":\"Audio nicht bereit oder Messung laeuft\"}"); return;
    }
    // The level indicator must have seen the target band at least once; a run
    // started from a badly placed microphone cannot be interpreted afterwards.
    if (!analyzer->levelWasGood()) {
        unlock(); server.send(409, "application/json",
            "{\"error\":\"Pegel war noch nicht im gruenen Bereich: Abstand/Winkel pruefen\"}"); return;
    }
    rawCount = 0; ++captureEpoch;
    analyzer->start();
    unlock(); server.send(200, "application/json", "{\"ok\":true}");
}
static void resetMeasurement() {
    if (!lock()) return;
    analyzer->reset(); rawCount = 0; ++captureEpoch;
    unlock(); server.send(200, "application/json", "{\"ok\":true}");
}
static bool parseArg(const char* name, float& value, float lo, float hi) {
    if (!server.hasArg(name)) return true;
    String s = server.arg(name); char* end = nullptr;
    float v = strtof(s.c_str(), &end);
    if (!s.length() || end != s.c_str() + s.length() || !std::isfinite(v) || v < lo || v > hi) return false;
    value = v; return true;
}
static bool parseFlag(const char* name, bool& value) {
    if (!server.hasArg(name)) return true;
    const String s = server.arg(name);
    if (s != "0" && s != "1") return false;
    value = s == "1"; return true;
}
static void configure() {
    if (!lock()) return;
    if (active()) { unlock(); server.send(409, "application/json", "{\"error\":\"Erst Messung beenden\"}"); return; }
    auto c = analyzer->config;
    // minCps floor of 50 is enforced here AND as an HTML attribute in the UI;
    // changing one without the other re-opens a limit the firmware closed.
    bool ok = parseArg("minCps", c.minCps, cps::CPS_GATE, 199) &&
        parseArg("maxCps", c.maxCps, cps::CPS_GATE + 1, 220) &&
        parseArg("thresholdMultiplier", c.thresholdMultiplier, 3, 30) &&
        parseArg("thresholdFloor", c.thresholdFloor, 0.00001f, 0.5f) &&
        parseArg("levelLow", c.levelLow, 0.0001f, 0.9f) &&
        parseArg("levelHigh", c.levelHigh, 0.001f, 0.99f) &&
        parseArg("levelLoud", c.levelLoud, 0.002f, 0.99f) &&
        parseFlag("autoStart", c.autoStart) && c.minCps < c.maxCps &&
        c.levelHigh > c.levelLow && c.levelLoud >= c.levelHigh;
    if (ok) { analyzer->config = c; analyzer->reset(); rawCount = 0; ++captureEpoch; }
    unlock(); server.send(ok ? 200 : 400, "application/json", ok ? "{\"ok\":true}" : "{\"error\":\"Ungueltige Messparameter\"}");
}
// The loop task is the only API/reset writer. Keep completed data immutable during
// downloads; audio task only calls process() while active, so no long-held lock.
static bool requireComplete() {
    if (!lock()) return false;
    bool ok = analyzer->state() == cps::State::Complete;
    unlock();
    if (!ok) server.send(409, "application/json", "{\"error\":\"Messung noch nicht abgeschlossen\"}");
    return ok;
}
static void sendWaveforms(bool withStatus) {
    if (!requireComplete()) return;
    server.sendHeader("Cache-Control", "no-store");
    if (withStatus) server.sendHeader("Content-Disposition", "attachment; filename=cpsmonk-result.json");
    server.setContentLength(CONTENT_LENGTH_UNKNOWN); server.send(200, "application/json", "");
    const size_t count = analyzer->count();
    String first = "{\"sampleRate\":" + String(cps::SAMPLE_RATE) + ",\"wavePoints\":" + String(cps::WAVE_POINTS);
    first += ",\"windowDurationMs\":" + String(cps::WINDOW_DURATION_MS, 2);
    first += ",\"count\":" + String(count);
    if (withStatus) {
        StaticJsonDocument<2048> d; statusInto(d.to<JsonObject>());
        String s; serializeJson(d, s); first += ",\"status\":" + s;
    }
    first += ",\"impacts\":["; server.sendContent(first);
    for (size_t i = 0; i < count; ++i) {
        if (!server.client().connected()) break;
        String row; row.reserve(2300);
        char indexText[32]; snprintf(indexText, sizeof(indexText), "%llu", (unsigned long long)analyzer->impactSample(i));
        row = i ? ",{\"index\":" : "{\"index\":";
        row += String(i + 1); row += ",\"sample\":"; row += indexText;
        row += ",\"amplitude\":" + String(analyzer->amplitude(i), 7);
        row += ",\"similarity\":" + String(analyzer->similarity(i), 4) + ",\"waveform\":[";
        const float* w = analyzer->waveform(i);
        for (size_t k = 0; k < cps::WAVE_POINTS; ++k) { if (k) row += ','; row += String(w[k], 6); }
        row += "]}"; server.sendContent(row); delay(1);
    }
    server.sendContent("]}"); server.sendContent("");
}
static void sendCsv() {
    if (!requireComplete()) return;
    server.sendHeader("Content-Disposition", "attachment; filename=cpsmonk-impacts.csv");
    server.setContentLength(CONTENT_LENGTH_UNKNOWN); server.send(200, "text/csv", "");
    server.sendContent("index,sample,time_s,interval_ms,amplitude_normalized,shape_similarity_percent\n");
    for (size_t i = 0; i < analyzer->count(); ++i) {
        char line[180]; uint64_t sample = analyzer->impactSample(i);
        double dt = i ? 1000.0 * (sample - analyzer->impactSample(i - 1)) / cps::SAMPLE_RATE : 0;
        snprintf(line, sizeof(line), "%u,%llu,%.7f,%.6f,%.8f,%.4f\n", unsigned(i + 1),
            (unsigned long long)sample, double(sample) / cps::SAMPLE_RATE, dt,
            analyzer->amplitude(i), analyzer->similarity(i));
        server.sendContent(line);
        if (!server.client().connected()) break;
        delay(1);
    }
    server.sendContent("");
}
static void put16(uint8_t* p, uint16_t v) { p[0] = v; p[1] = v >> 8; }
static void put32(uint8_t* p, uint32_t v) { for (int i = 0; i < 4; ++i) p[i] = v >> (8 * i); }
static void sendWav() {
    if (!lock()) return;
    bool ok = !active() && rawCount;
    size_t n = rawCount; unlock();
    if (!ok) { server.send(409, "text/plain", "Keine abgeschlossene Aufnahme vorhanden"); return; }
    uint8_t h[44] = {};
    memcpy(h, "RIFF", 4); put32(h + 4, 36 + n * 2); memcpy(h + 8, "WAVEfmt ", 8);
    put32(h + 16, 16); put16(h + 20, 1); put16(h + 22, 1); put32(h + 24, cps::SAMPLE_RATE);
    put32(h + 28, cps::SAMPLE_RATE * 2); put16(h + 32, 2); put16(h + 34, 16);
    memcpy(h + 36, "data", 4); put32(h + 40, n * 2);
    server.sendHeader("Content-Disposition", "attachment; filename=cpsmonk-raw.wav");
    server.setContentLength(44 + n * 2); server.send(200, "audio/wav", "");
    auto client = server.client(); client.write(h, 44);
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(rawPcm);
    size_t sent = 0; uint32_t lastWrite = millis();
    while (sent < n * 2 && client.connected() && millis() - lastWrite < 5000) {
        size_t want = min(size_t(4096), n * 2 - sent);
        size_t done = client.write(bytes + sent, want);
        if (done) { sent += done; lastWrite = millis(); }
        delay(1);
    }
}
static void audioTask(void*) {
    int32_t samples[256];
    for (;;) {
        xSemaphoreTake(guard, portMAX_DELAY);
        const uint32_t epochBeforeRead = captureEpoch;
        const bool collect = collecting();
        xSemaphoreGive(guard);
        size_t bytes = 0;
        esp_err_t err = i2s_read(I2S_NUM_0, samples, sizeof(samples), &bytes, pdMS_TO_TICKS(500));
        xSemaphoreTake(guard, portMAX_DELAY);
        if (epochBeforeRead != captureEpoch) {
            // Never feed a block captured across a start/reset into the new run.
            i2s_event_t stale; while (xQueueReceive(i2sEvents, &stale, 0) == pdTRUE) {}
            xSemaphoreGive(guard); continue;
        }
        if (active() && (err != ESP_OK || !bytes || bytes % sizeof(int32_t))) analyzer->fail("I2S Lesefehler/Timeout");
        i2s_event_t ev;
        while (xQueueReceive(i2sEvents, &ev, 0) == pdTRUE) {
            if (active() && (ev.type == I2S_EVENT_RX_Q_OVF || ev.type == I2S_EVENT_DMA_ERROR))
                analyzer->fail("Audio-Datenverlust: Messung ungueltig, neu starten");
        }
        if (err == ESP_OK) {
            const size_t words = bytes / sizeof(int32_t);
            for (size_t i = 0; i < words; ++i) {
                const float value = float(samples[i]) / 2147483648.0f;
                if (active()) {
                    if (collect && rawCount < RAW_CAPACITY) rawPcm[rawCount++] = int16_t(samples[i] >> 16);
                    analyzer->process(value);
                } else {
                    // Idle: only the level indicator consumes samples.
                    analyzer->monitor(value);
                    analyzer->levelTouch();
                }
            }
        }
        xSemaphoreGive(guard);
    }
}
static bool initAudio() {
    i2s_config_t c = {};
    c.mode = i2s_mode_t(I2S_MODE_MASTER | I2S_MODE_RX);
    c.sample_rate = cps::SAMPLE_RATE;
    c.bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;
    c.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
    c.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    c.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    // 16 x 256 frames at 32 kHz gives 128 ms of slack. The previous 8 x 256
    // (64 ms) was tight enough that a single blocking serial write could
    // overflow the RX queue and invalidate a run with "Audio-Datenverlust".
    // Latency is irrelevant here: every run is evaluated after it finishes.
    c.dma_buf_count = 16; c.dma_buf_len = 256;
    c.use_apll = false;
    if (i2s_driver_install(I2S_NUM_0, &c, 16, &i2sEvents) != ESP_OK) return false;
    i2s_pin_config_t p = {};
    p.mck_io_num = I2S_PIN_NO_CHANGE; p.bck_io_num = MIC_BCLK;
    p.ws_io_num = MIC_WS; p.data_out_num = I2S_PIN_NO_CHANGE; p.data_in_num = MIC_SD;
    if (i2s_set_pin(I2S_NUM_0, &p) != ESP_OK) { i2s_driver_uninstall(I2S_NUM_0); return false; }
    return xTaskCreatePinnedToCore(audioTask, "cps-audio", 16384, nullptr, 3, nullptr, 1) == pdPASS;
}
// Level band as a horizontal bar: the operator sets the distance until the bar
// sits inside the green middle region.
// The panel is monochrome, so the target zone is drawn as a marked region and
// the bar is scaled to the overload threshold (not to full scale): with a 0.12
// green ceiling a /1.0 scaling would show the whole usable range as 12 %.
static void drawLevelBar(int x, int y, int w, int h, float level, float low,
                         float high, float loud, cps::Level band) {
    const float span = loud > 0 ? loud : 1.0f;
    oled.drawRect(x, y, w, h, SSD1306_WHITE);
    const int zoneLow = int(low / span * w);
    const int zoneHigh = int(high / span * w);
    oled.drawRect(x + zoneLow, y - 2, zoneHigh - zoneLow, h + 4, SSD1306_WHITE);
    int inner = int(level / span * w);
    if (inner > w - 2) inner = w - 2;
    if (inner > 0) oled.fillRect(x + 1, y + 1, inner, h - 2, SSD1306_WHITE);
    (void)band;
}
static void drawOled() {
    if (!oledReady) return;
    if (oledTest) {
        // Refresh the test frame at the same rate as the normal UI. Sending it
        // once made the test useless as a diagnostic: a panel that lost a single
        // frame stayed dark and looked identical to a panel that lost power.
        // Refreshed and still dark means the module is really unpowered or the
        // controller keeps resetting, not that one frame went missing.
        oled.clearDisplay(); oled.fillScreen(SSD1306_WHITE); oled.display();
        return;
    }
    if (xSemaphoreTake(guard, pdMS_TO_TICKS(5)) != pdTRUE) return;
    const auto s = analyzer->state();
    const auto count = analyzer->count();
    const auto r = analyzer->result();
    const auto band = analyzer->levelAssessment();
    const bool levelReady = analyzer->levelWasGood();
    xSemaphoreGive(guard);
    oled.clearDisplay(); oled.setTextColor(SSD1306_WHITE);
    switch (s) {
        case cps::State::Idle: {
            // Frame 1: level indicator; frame 2: WLAN credentials, but only
            // while the AP actually runs. With WLAN off the level indicator
            // stays on screen permanently instead of flicking to a password
            // nobody needs.
            if (wifiEnabled && (millis() / 6000) % 2) {
                oled.setTextSize(1); oled.setCursor(0, 0); oled.println("WLAN Passwort:");
                oled.println(apPassword); oled.println("http://192.168.4.1");
                break;
            }
            oled.setTextSize(1); oled.setCursor(0, 0);
            oled.print("Pegel ");
            if (band == cps::Level::Silent) oled.print("KEIN SIGNAL");
            else if (band == cps::Level::TooQuiet) oled.print("zu leise");
            else if (band == cps::Level::Good) oled.print("OK - Abstand passt");
            else if (band == cps::Level::Warning) oled.print("grenzwertig");
            else oled.print("zu laut");
            drawLevelBar(0, 12, 127, 8, r.levelAmplitude, analyzer->config.levelLow,
                         analyzer->config.levelHigh, analyzer->config.levelLoud, band);
            oled.setCursor(0, 24);
            if (band == cps::Level::Silent) oled.print("Kabel/L-R pruefen");
            else if (band == cps::Level::Good) oled.print(levelReady ? "Auto-Start aktiv" : "Abstand einstellen");
            else if (levelReady) oled.print("Abstand korrigieren");
            else oled.print("Abstand einstellen");
            break;
        }
        case cps::State::Calibrating:
            oled.setTextSize(1); oled.println("Ruhe messen..."); oled.println("Motor AUS lassen!");
            break;
        case cps::State::WaitingForMotor:
            oled.setTextSize(1); oled.println("Auto-Start bereit"); oled.println("Motor jetzt AN");
            oled.print("Impulse: "); oled.println(analyzer->seen());
            break;
        case cps::State::Warmup:
            oled.setTextSize(1); oled.println("Vorlauf: Rate messen");
            oled.setTextSize(2);
            oled.printf("%.0f CPS", r.provisionalCps);
            oled.setTextSize(1); oled.print("Impulse "); oled.println(analyzer->seen());
            break;
        case cps::State::Recording:
            oled.setTextSize(1); oled.println("Messung laeuft");
            oled.setTextSize(2); oled.printf("%u/%u", unsigned(count), unsigned(cps::TARGET));
            oled.setTextSize(1);
            oled.printf("%.1fs Fenster", r.windowSeconds);
            break;
        case cps::State::Complete:
            oled.setTextSize(2); oled.printf("%.1f CPS", r.cps);
            oled.setTextSize(1); oled.setCursor(0, 23);
            oled.printf("Form %.1f%% n=%u", r.shapeSimilarity, unsigned(count));
            break;
        default:
            oled.setTextSize(1); oled.println("Messfehler");
            oled.println(analyzer->error());
            oled.println("USB r = Reset");
            break;
    }
    oled.display();
}
static uint32_t i2cClock = 400000;
// Every address that ACKs, so an empty bus (wiring/power) and a present but
// unsupported panel (wrong controller or geometry) can be told apart.
static uint8_t i2cScan(uint8_t* found, uint8_t maxFound) {
    uint8_t n = 0;
    for (uint8_t addr = 1; addr < 0x7f; ++addr) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0 && n < maxFound) found[n++] = addr;
    }
    return n;
}
static bool probeOled() {
    oledReady = false;
    for (uint8_t addr : {uint8_t(0x3c), uint8_t(0x3d)}) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
            oledAddr = addr;
            oledReady = oled.begin(SSD1306_SWITCHCAPVCC, addr, false, false);
            break;
        }
    }
    return oledReady;
}
// After a brownout the controller answers on I2C and still swallows frames, but
// it comes back with its display turned OFF - which looks exactly like a dead
// panel. Asking only "does it ACK" was not enough: oledReady stayed true, so
// nothing ever re-initialised it and the screen stayed dark. Re-assert the whole
// init sequence periodically instead; that is invisible here because the UI
// already clears and redraws four times a second. If the panel has left the bus
// entirely, drop oledReady so the retry path picks it up again on return.
static void checkOledHealth() {
    if (!oledReady) return;
    Wire.beginTransmission(oledAddr);
    if (Wire.endTransmission() != 0) {
        oledReady = false;
        Serial.println("OLED nicht mehr am Bus - warte auf Rueckkehr");
        return;
    }
    oled.begin(SSD1306_SWITCHCAPVCC, oledAddr, false, false);
    // begin() switches the panel off and clears its RAM. Redraw immediately so
    // the gap is milliseconds instead of up to one 250 ms UI period - that is
    // what keeps this periodic re-init invisible.
    drawOled();
}
// Diagnostics for the one part of the build that cannot be verified over USB:
// the display. Run with 'o', repeated with 'k' at the other I2C clock.
static void reportOled() {
    uint8_t found[16];
    const uint8_t n = i2cScan(found, sizeof(found));
    Serial.printf("OLED: ready=%d clock=%lu Hz Adressen:", oledReady ? 1 : 0, (unsigned long)i2cClock);
    if (!n) Serial.print(" keine (Bus leer -> Verdrahtung/Spannung)");
    for (uint8_t i = 0; i < n; ++i) Serial.printf(" 0x%02X", found[i]);
    Serial.println();
    if (!oledReady && n) Serial.println("  Panel antwortet, aber nicht als SSD1306 128x32 initialisiert.");
}
static void startWifi() {
    WiFi.mode(WIFI_AP);
    const uint64_t mac = ESP.getEfuseMac();
    snprintf(apName, sizeof(apName), "cpsMONK-%04X", unsigned(mac & 0xffff));
    snprintf(apPassword, sizeof(apPassword), "%08lx", (unsigned long)esp_random());
    if (!WiFi.softAP(apName, apPassword, 1, 0, 2)) { Serial.println("WLAN Start fehlgeschlagen"); return; }
    server.on("/", HTTP_GET, [] { server.send_P(200, "text/html; charset=utf-8", WEB_UI); });
    server.on("/api/status", HTTP_GET, getStatus);
    server.on("/api/start", HTTP_POST, beginMeasurement);
    server.on("/api/reset", HTTP_POST, resetMeasurement);
    server.on("/api/config", HTTP_POST, configure);
    server.on("/api/waveforms", HTTP_GET, [] { sendWaveforms(false); });
    server.on("/api/result", HTTP_GET, [] { sendWaveforms(true); });
    server.on("/api/impacts.csv", HTTP_GET, sendCsv);
    server.on("/api/raw.wav", HTTP_GET, sendWav);
    server.onNotFound([] { server.send(404, "text/plain", "Not found"); });
    server.begin();
    Serial.printf("WLAN AN: SSID %s | Passwort %s | http://192.168.4.1\n", apName, apPassword);
}
static void stopWifi() {
    server.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    Serial.println("WLAN AUS");
}
// CSV der erkannten Impulse ueber die Konsole. Ersetzt den HTTP-Download: ohne
// WLAN ist das der einzige Weg an die Einzelwerte. Bewusst nur Kennzahlen je
// Impuls - die 128 Punkte je Kurve sprengen eine Textzeile.
static void dumpCsv() {
    // Completed data is immutable during downloads - the same invariant the HTTP
    // sendCsv() relies on. So the mutex is taken only to check the state and the
    // row count, and released BEFORE printing: ~45 kB over 115200 baud takes
    // seconds, and holding the mutex that long starves the audio task (its DMA
    // buffers only cover 64 ms) and invalidates the next run.
    size_t count = 0;
    if (xSemaphoreTake(guard, pdMS_TO_TICKS(200)) != pdTRUE) { Serial.println("beschaeftigt, erneut versuchen"); return; }
    const bool complete = analyzer->state() == cps::State::Complete;
    if (complete) count = analyzer->count();
    xSemaphoreGive(guard);
    if (!complete) { Serial.println("keine abgeschlossene Messung"); return; }

    Serial.println("index,sample,time_s,interval_ms,amplitude,similarity");
    for (size_t i = 0; i < count; ++i) {
        const uint64_t sample = analyzer->impactSample(i);
        const double dt = i ? 1000.0 * double(sample - analyzer->impactSample(i - 1)) / cps::SAMPLE_RATE : 0.0;
        Serial.printf("%u,%llu,%.7f,%.6f,%.8f,%.4f\n", unsigned(i + 1),
            (unsigned long long)sample, double(sample) / cps::SAMPLE_RATE, dt,
            analyzer->amplitude(i), analyzer->similarity(i));
    }
    Serial.printf("CSV Ende: %u Zeilen\n", unsigned(count));
}
static void fatal(const char* message) {
    Serial.println(message);
    if (oledReady) { oled.clearDisplay(); oled.setTextColor(SSD1306_WHITE); oled.setTextSize(1); oled.setCursor(0, 0); oled.println(message); oled.display(); }
    for (;;) delay(1000);
}
void setup() {
    Serial.begin(115200);
    Wire.begin(OLED_SDA, OLED_SCL); Wire.setClock(i2cClock); Wire.setTimeOut(25);
    probeOled();
    guard = xSemaphoreCreateMutex(); if (!guard) fatal("Mutex fehlt");
    if (!psramFound()) fatal("PSRAM fehlt: qio_opi pruefen");
    void* memory = heap_caps_malloc(sizeof(cps::ImpactAnalyzer), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    rawPcm = static_cast<int16_t*>(heap_caps_malloc(RAW_CAPACITY * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!memory || !rawPcm) fatal("PSRAM Speicher fehlt");
    analyzer = new(memory) cps::ImpactAnalyzer();
    // WLAN ist standardmaessig AUS. Das Geraet ist ein eigenstaendiges
    // Messinstrument: Anzeige auf dem OLED, Bedienung ueber die serielle
    // Konsole. Ein Access Point ist fuer die Messung nicht noetig, und ein
    // Passwort, das bei jedem Neustart neu gewuerfelt wird, ist am
    // Messplatz ohnehin nur hinderlich. Mit 'w' laesst sich der AP zur
    // Laufzeit zuschalten, falls die Web-Oberflaeche oder ein Download
    // doch einmal gebraucht wird.
    if (wifiEnabled) startWifi();
    Serial.printf("Ziel %u Impulse, Fenster %u/%us, Auto-Start ab %.0f CPS.\n",
        unsigned(cps::TARGET), unsigned(cps::TARGET), unsigned(cps::WINDOW_MAX_SECONDS), cps::CPS_GATE);
    Serial.printf("OLED: ready=%d | o=Display-Scan, k=I2C-Takt, f=Vollbild-Test\n", oledReady ? 1 : 0);
    Serial.println("s=Start, r=Reset, ?=Status, c=CSV der Impulse, w=WLAN ein/aus.");
    Serial.println("Keine Kraftmessung: CPS und Formaehnlichkeit sagen nichts ueber Schlagkraft.");
    audioReady = initAudio();
    if (!audioReady) { xSemaphoreTake(guard, portMAX_DELAY); analyzer->fail("I2S Initialisierung fehlgeschlagen"); unlock(); }
}
void loop() {
    if (wifiEnabled) server.handleClient();
    // Hold one pending console character across loop iterations: dropping it on a
    // mutex miss made 's' and 'r' unreliable from a terminal.
    static char pending = 0;
    if (!pending && Serial.available()) pending = char(Serial.read());
    // Display diagnostics run OUTSIDE the mutex: an I2C bus scan takes tens of
    // milliseconds, and holding 'guard' that long would starve the audio task
    // (it re-acquires the mutex twice per 16 ms block and would drop samples).
    if (pending == 'o') {
        pending = 0;
        probeOled(); reportOled();
    } else if (pending == 'k') {
        pending = 0;
        i2cClock = (i2cClock == 400000) ? 100000 : 400000;
        Wire.setClock(i2cClock);
        probeOled(); reportOled();
    } else if (pending == 'f') {
        pending = 0;
        oledTest = !oledTest;
        Serial.printf("OLED-Vollbild: %s\n", oledTest ? "AN - erscheint das Display weiss?" : "AUS");
        if (oledReady) {
            oled.clearDisplay();
            if (oledTest) oled.fillScreen(SSD1306_WHITE);
            oled.display();
        } else {
            Serial.println("  kein Display erkannt, Muster nicht gesendet");
        }
    } else if (pending == 'c') {
        // Takes the mutex itself, so it must not run inside the guarded block.
        pending = 0;
        dumpCsv();
    } else if (pending == 'w') {
        pending = 0;
        wifiEnabled = !wifiEnabled;
        if (wifiEnabled) startWifi(); else stopWifi();
    } else if (pending && xSemaphoreTake(guard, pdMS_TO_TICKS(0)) == pdTRUE) {
        const char c = pending;
        pending = 0;
        if (c == 's' && audioReady && !active()) { rawCount = 0; ++captureEpoch; analyzer->start(); }
        if (c == 'r') { analyzer->reset(); rawCount = 0; ++captureEpoch; }
        // 'r' must also re-arm the cold start, otherwise the device would sit
        // idle forever on a build without a button.
        // The status line is FORMATTED here but PRINTED after unlocking. A serial
        // write can block on the host and the DMA only buffers 8 x 256 frames,
        // i.e. 64 ms at 32 kHz - holding the mutex across the write is enough to
        // lose audio blocks and produce "Audio-Datenverlust".
        bool haveLine = false;
        if (c == '?') {
            const auto& r = analyzer->result();
            // 'dc' is the raw-input average: after the indicator's DC blocker it
            // is the only way to tell a rail-stuck line (dc ~1, amp small) from a
            // genuinely quiet room (dc ~0, amp small). 'oled' is the display probe
            // result. Keep 'err' last, it is free text and may contain spaces.
            snprintf(statusLine, sizeof(statusLine),
                "state=%s impacts=%u seen=%u cps=%.3f form=%.2f level=%s amp=%.6f dB=%.1f "
                "noise=%.7f thr=%.6f win=%.1fs gate=%d dc=%.5f oled=%d err=%s\n",
                stateName(analyzer->state()), unsigned(analyzer->count()), unsigned(analyzer->seen()),
                r.cps, r.shapeSimilarity, levelName(analyzer->levelAssessment()),
                r.levelAmplitude, r.levelDb, r.noiseRms, r.threshold, r.windowSeconds,
                analyzer->levelWasGood() ? 1 : 0, analyzer->levelDc(), oledReady ? 1 : 0,
                analyzer->error());
            haveLine = true;
        }
        unlock();
        if (haveLine) Serial.write(statusLine);
    }
    // The single probe in setup() can lose its first transaction on a freshly
    // configured bus. It used to be the only attempt, so one failed probe left
    // the display dark for the whole session even though the panel answers on
    // 0x3C - which looks exactly like broken hardware. Retry until it answers.
    static uint32_t lastOledProbe = 0;
    if (!oledReady && millis() - lastOledProbe > 1000) {
        lastOledProbe = millis();
        if (probeOled()) Serial.println("OLED nachtraeglich erkannt, Anzeige aktiv");
    }
    maybeAutoStart();
    static uint32_t lastOledHealth = 0;
    if (millis() - lastOledHealth > 5000) { lastOledHealth = millis(); checkOledHealth(); }
    static uint32_t lastDraw = 0;
    if (millis() - lastDraw > 250) { lastDraw = millis(); drawOled(); }
    delay(1);
}
