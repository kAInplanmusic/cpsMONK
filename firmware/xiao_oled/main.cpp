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
constexpr int MIC_SD = 9;   // D10; INMP441 L/R tied to GND
constexpr size_t RAW_CAPACITY = cps::SAMPLE_RATE * 65U;
static cps::ImpactAnalyzer* analyzer = nullptr;
static int16_t* rawPcm = nullptr;
static size_t rawCount = 0;
static uint32_t captureEpoch = 0;
static SemaphoreHandle_t guard;
static QueueHandle_t i2sEvents;
static bool audioReady = false;
static bool oledReady = false;
static char apPassword[13];
static char apName[24];
static WebServer server(80);
static Adafruit_SSD1306 oled(128, 32, &Wire, -1);

static bool active() {
    return analyzer->state() == cps::State::Calibrating || analyzer->state() == cps::State::Recording;
}
static const char* stateName(cps::State s) {
    switch (s) {
        case cps::State::Idle: return "idle";
        case cps::State::Calibrating: return "calibrating";
        case cps::State::Recording: return "recording";
        case cps::State::Complete: return "complete";
        default: return "failed";
    }
}
static bool lock() {
    if (xSemaphoreTake(guard, pdMS_TO_TICKS(100)) == pdTRUE) return true;
    server.send(503, "application/json", "{\"error\":\"Auswertung beschaeftigt; erneut versuchen\"}");
    return false;
}
static void unlock() { xSemaphoreGive(guard); }
static void statusInto(JsonObject j) {
    const auto& r = analyzer->result();
    j["state"] = stateName(analyzer->state());
    j["count"] = analyzer->count(); j["target"] = cps::TARGET; j["runId"] = captureEpoch;
    j["cps"] = r.cps; j["shapeSimilarity"] = r.shapeSimilarity;
    j["periodCvPercent"] = r.periodCvPercent;
    j["amplitudeCvPercent"] = r.amplitudeCvPercent;
    j["clippedPercent"] = r.clippedPercent;
    j["noiseRms"] = r.noiseRms; j["threshold"] = r.threshold;
    j["rejected"] = r.rejected; j["timingWarning"] = r.timingWarning;
    j["clippingWarning"] = r.clippingWarning;
    j["error"] = analyzer->error();
    j["minCps"] = analyzer->config.minCps; j["maxCps"] = analyzer->config.maxCps;
    j["thresholdMultiplier"] = analyzer->config.thresholdMultiplier;
    j["thresholdFloor"] = analyzer->config.thresholdFloor;
    j["rawSamples"] = rawCount; j["sampleRate"] = cps::SAMPLE_RATE;
    j["oledDetected"] = oledReady;
}
static void getStatus() {
    if (!lock()) return;
    StaticJsonDocument<1536> d;
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
static void configure() {
    if (!lock()) return;
    if (active()) { unlock(); server.send(409, "application/json", "{\"error\":\"Erst Messung beenden\"}"); return; }
    auto c = analyzer->config;
    bool ok = parseArg("minCps", c.minCps, 20, 199) && parseArg("maxCps", c.maxCps, 21, 200)
        && parseArg("thresholdMultiplier", c.thresholdMultiplier, 3, 30)
        && parseArg("thresholdFloor", c.thresholdFloor, 0.00001f, 0.5f) && c.minCps < c.maxCps;
    if (ok) { analyzer->config = c; analyzer->reset(); rawCount = 0; ++captureEpoch; }
    unlock(); server.send(ok ? 200 : 400, "application/json", ok ? "{\"ok\":true}" : "{\"error\":\"Ungueltige Messparameter\"}");
}
// The loop task is the only API/reset writer. Keep completed data immutable during
// downloads; audio task only calls process() while active, so no long-held lock.
static bool requireComplete() {
    if (!lock()) return false;
    bool ok = analyzer->state() == cps::State::Complete;
    unlock();
    if (!ok) server.send(409, "application/json", "{\"error\":\"500 Impulse noch nicht vollstaendig\"}");
    return ok;
}
static void sendWaveforms(bool withStatus) {
    if (!requireComplete()) return;
    server.sendHeader("Cache-Control", "no-store");
    if (withStatus) server.sendHeader("Content-Disposition", "attachment; filename=cpsmonk-result.json");
    server.setContentLength(CONTENT_LENGTH_UNKNOWN); server.send(200, "application/json", "");
    String first = "{\"sampleRate\":" + String(cps::SAMPLE_RATE) + ",\"wavePoints\":" + String(cps::WAVE_POINTS);
    if (withStatus) {
        StaticJsonDocument<1536> d; statusInto(d.to<JsonObject>());
        String s; serializeJson(d, s); first += ",\"status\":" + s;
    }
    first += ",\"windowDurationMs\":3.0,\"impacts\":["; server.sendContent(first);
    for (size_t i = 0; i < analyzer->count(); ++i) {
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
            for (size_t i = 0; i < bytes / sizeof(int32_t) && active(); ++i) {
                if (rawCount >= RAW_CAPACITY) { analyzer->fail("Aufnahmespeicher voll"); break; }
                rawPcm[rawCount++] = int16_t(samples[i] >> 16);
                analyzer->process(float(samples[i]) / 2147483648.0f);
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
    c.dma_buf_count = 8; c.dma_buf_len = 256;
    c.use_apll = false;
    if (i2s_driver_install(I2S_NUM_0, &c, 16, &i2sEvents) != ESP_OK) return false;
    i2s_pin_config_t p = {};
    p.mck_io_num = I2S_PIN_NO_CHANGE; p.bck_io_num = MIC_BCLK;
    p.ws_io_num = MIC_WS; p.data_out_num = I2S_PIN_NO_CHANGE; p.data_in_num = MIC_SD;
    if (i2s_set_pin(I2S_NUM_0, &p) != ESP_OK) { i2s_driver_uninstall(I2S_NUM_0); return false; }
    return xTaskCreatePinnedToCore(audioTask, "cps-audio", 16384, nullptr, 3, nullptr, 1) == pdPASS;
}
static void drawOled() {
    if (!oledReady || xSemaphoreTake(guard, pdMS_TO_TICKS(5)) != pdTRUE) return;
    auto s = analyzer->state(); auto count = analyzer->count();
    float cpsValue = analyzer->result().cps, sim = analyzer->result().shapeSimilarity;
    xSemaphoreGive(guard);
    oled.clearDisplay(); oled.setTextColor(SSD1306_WHITE); oled.setTextSize(1); oled.setCursor(0, 0);
    if (s == cps::State::Idle) {
        if ((millis() / 5000) % 2) { oled.println("WLAN Passwort:"); oled.println(apPassword); oled.println("http://192.168.4.1"); }
        else { oled.println(apName); oled.println("Motor AUS -> Start"); oled.println("WLAN/USB bereit"); }
    } else if (s == cps::State::Calibrating) {
        oled.println("Ruhe messen..."); oled.println("Motor AUS lassen!");
    } else if (s == cps::State::Recording) {
        oled.println("Motor AN / Aufnahme"); oled.setTextSize(2); oled.printf("%u/500", unsigned(count));
    } else if (s == cps::State::Complete) {
        oled.setTextSize(2); oled.printf("%.1f CPS", cpsValue); oled.setTextSize(1); oled.setCursor(0, 23); oled.printf("Form %.1f%%", sim);
    } else { oled.println("Messfehler"); oled.println("Details im Browser"); oled.println("USB r = Reset"); }
    oled.display();
}
static void fatal(const char* message) {
    Serial.println(message);
    if (oledReady) { oled.clearDisplay(); oled.setTextColor(SSD1306_WHITE); oled.setTextSize(1); oled.setCursor(0, 0); oled.println(message); oled.display(); }
    for (;;) delay(1000);
}
void setup() {
    Serial.begin(115200);
    Wire.begin(OLED_SDA, OLED_SCL); Wire.setClock(400000); Wire.setTimeOut(25);
    for (uint8_t addr : {uint8_t(0x3c), uint8_t(0x3d)}) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) { oledReady = oled.begin(SSD1306_SWITCHCAPVCC, addr, false, false); break; }
    }
    guard = xSemaphoreCreateMutex(); if (!guard) fatal("Mutex fehlt");
    if (!psramFound()) fatal("PSRAM fehlt: qio_opi pruefen");
    void* memory = heap_caps_malloc(sizeof(cps::ImpactAnalyzer), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    rawPcm = static_cast<int16_t*>(heap_caps_malloc(RAW_CAPACITY * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!memory || !rawPcm) fatal("PSRAM Speicher fehlt");
    analyzer = new(memory) cps::ImpactAnalyzer();
    WiFi.mode(WIFI_AP);
    uint64_t mac = ESP.getEfuseMac();
    snprintf(apName, sizeof(apName), "cpsMONK-%04X", unsigned(mac & 0xffff));
    snprintf(apPassword, sizeof(apPassword), "%08lx", (unsigned long)esp_random());
    if (!WiFi.softAP(apName, apPassword, 1, 0, 2)) fatal("WLAN Start fehlgeschlagen");
    Serial.printf("\ncpsMONK OLED: SSID %s | Passwort %s | http://192.168.4.1\n", apName, apPassword);
    Serial.println("s=Start (1s Ruhe, danach Motor AN), r=Reset, ?=Status. Keine Kraftmessung.");
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
    audioReady = initAudio();
    if (!audioReady) { xSemaphoreTake(guard, portMAX_DELAY); analyzer->fail("I2S Initialisierung fehlgeschlagen"); unlock(); }
}
void loop() {
    server.handleClient();
    while (Serial.available()) {
        char c = Serial.read();
        if (xSemaphoreTake(guard, pdMS_TO_TICKS(100)) == pdTRUE) {
            if (c == 's' && audioReady && !active()) { rawCount = 0; ++captureEpoch; analyzer->start(); }
            if (c == 'r') { analyzer->reset(); rawCount = 0; ++captureEpoch; }
            if (c == '?') Serial.printf("state=%s impacts=%u cps=%.3f form=%.2f error=%s\n", stateName(analyzer->state()), unsigned(analyzer->count()), analyzer->result().cps, analyzer->result().shapeSimilarity, analyzer->error());
            unlock();
        }
    }
    static uint32_t lastDraw = 0;
    if (millis() - lastDraw > 250) { lastDraw = millis(); drawOled(); }
    delay(1);
}
