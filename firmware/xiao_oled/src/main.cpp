// main.cpp
#pragma once
#include "flash.h"
#include "ImpactAnalyzer.h"

#include "esp_log.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "esp_chip_info.h"

#include "driver/i2s.h"
#include "driver/spi_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include <string.h>
#include <stdlib.h>

// ---------- constants ----------
const char *WIFI_SSID  = "cpsMONK-XXXX";
const char *WIFI_PASS = "xxxxxxxx";

#if defined(WIFI_ENABLED)
#define WIFI_ENABLED 1
#else
#define WIFI_ENABLED 0
#endif

// Flash packing
const uint32_t FLASH_WINDOW  = 16 * 1024;                     // 16 kB
const uint32_t MEAS_SIZE  = sizeof(struct { uint32_t cps; float quality; });
static uint32_t flash_offset = 0;

// ---------- WIFI util ----------
static void wifi_init(void) {
    if (!WIFI_ENABLED) return;
    nvs_flash_init();
    tcpip_adapter_init();
    esp_event_loop_create_default();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    wifi_config_t wifi_cfg = {};
    strcpy((char*)wifi_cfg.sta.ssid, WIFI_SSID);
    strcpy((char*)wifi_cfg.sta.password, WIFI_PASS);
    esp_wifi_set_mode(ESP_WIFI_MODE_STA);
    esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_cfg);
    esp_wifi_start();
}

// ---------- I2S – Dummy Setup ----------
static void i2s_setup(void) {
    i2s_config_t cfg = ESP_DEFAULT_I2S_CONFIG;
    i2s_pin_config_t pins = {
        .bck_io_num   = 3,
        .ws_io_num    = 4,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num  = 2
    };
    i2s_driver_install(I2S_NUM_0, &cfg, 0, NULL);
    i2s_set_pin(I2S_NUM_0, &pins);
}

// ---------- Dummy‑CPS‑Berechnung ----------
static uint32_t calc_cps(void) {
    return 80 + (rand() & 0xF);  // 80…95 CPS
}

// ---------- Async Web‑Server ----------
#if WIFI_ENABLED
#include "ESPAsyncWebServer.h"
static AsyncWebServer server(80);
static WebSocket ws("/ws");
static void websocket_init(void) {
    ws.onEvent([](AsyncWebSocket *server, AsyncWebSocketClient *client,
                  AwsEventType type, void *arg, uint8_t *data, size_t len) {
        if (type == WS_EVT_CONNECT) {
            ESP_LOGI("WS", "Client connect");
        } else if (type == WS_EVT_DISCONNECT) {
            ESP_LOGI("WS", "Client disconnect");
        }
    });
    server.addHandler(&ws);
    server.begin();
}
static void websocket_send(uint32_t cps) {
    String payload = String(cps);
    ws.textAll(payload);
}
#endif

// ---------- Mess‑Schleife ----------
void app_main(void) {
    wifi_init();
    i2s_setup();
#if WIFI_ENABLED
    websocket_init();
#endif
    const uint32_t TOTAL_IMPULS = 500u;
    uint32_t count = 0;
    while (count < TOTAL_IMPULS) {
        uint32_t cps = calc_cps();
#if WIFI_ENABLED
        websocket_send(cps);
#endif
        if ((count % 50u) == 0) {
            struct { uint32_t cps; float quality; } meas{
                .cps = cps,
                .quality = 80.0f + 10.0f * ((count % 50)/49.0f)
            };
            flash_write_measurement((uint8_t*)&meas, sizeof(meas), flash_offset);
            flash_offset += MEAS_SIZE;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
        count++;
    }
    ESP_LOGI("MAIN", "Messung fertig – %u Messungen in %u", count, flash_offset/MEAS_SIZE);
#if WIFI_ENABLED
    server.on("/measurements", HTTP_GET, [](AsyncWebServerRequest *req){
        String out = "[";
        for (uint32_t off = 0; off < flash_offset; off += MEAS_SIZE) {
            struct { uint32_t cps; float quality; } m;
            flash_read_measurement((uint8_t*)&m, sizeof(m), off);
            if (off) out += ",";
            out += String("{\"cps\":") + m.cps + ",\"quality\":" + m.quality + "}";
        }
        out += "]";
        req->send(200, "application/json", out);
    });
#endif
}
