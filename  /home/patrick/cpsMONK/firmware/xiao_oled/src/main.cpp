#include "flash.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "driver/i2s.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <string.h>
#include <stdint.h>

#define WIFI_SSID "cpsMONK-XXXX"
#define WIFI_PASS "xxxxxxxx"

// Simple state
static volatile uint32_t cycles_collected = 0;
static const uint32_t TARGET_CYCLES = 100;

static void init_wifi(bool enable) {
    if (!enable) return;
    nvs_flash_init();
    tcpip_adapter_init();
    esp_event_loop_create_default();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    wifi_config_t wifi_cfg = {};
    strcpy((char*)wifi_cfg.sta.ssid, WIFI_SSID);
    strcpy((char*)wifi_cfg.sta.password, WIFI_PASS);
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_cfg);
    esp_wifi_start();
}

static void i2s_setup() {
    i2s_config_t i2s_cfg = {
        .mode = I2S_MODE_MASTER | I2S_MODE_RX,
        .sample_rate = 44100,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_RIGHT,
        .communication_format = I2S_COMM_FORMAT_I2S_MSB,
        .intr_alloc_flags = 0,
        .dma_buf_count = 8,
        .dma_buf_len = 512,
    };
    i2s_pin_config_t i2s_pins = {
        .bck_io_num = 3,
        .ws_io_num = 4,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num = 2
    };
    i2s_driver_install(I2S_NUM_0, &i2s_cfg, 0, NULL);
    i2s_set_pin(I2S_NUM_0, &i2s_pins);
}

static void measure_cps(uint32_t *cps) {
    // Dummy: set random CPS 85±5
    *cps = 80 + (rand() % 10);
}

static void flash_store(const uint32_t cps, const float quality) {
    struct { uint32_t cps; float quality; } m = {cps, quality};
    uint32_t offset = cycles_collected * sizeof(m);
    flash_write_measurement((uint8_t*)&m, sizeof(m), offset);
}

static void app_main() {
    init_wifi(false); // no wifi for mode A
    i2s_setup();

    for (;;) {
        uint32_t cps;
        measure_cps(&cps);
        cycles_collected++;
        // store after each sample in Wi‑Fi mode, or after 100 cycles in mode A
        if (cycles_collected >= TARGET_CYCLES) {
            float quality = 90.0f; // placeholder
            flash_store(cps, quality);
            // reset
            cycles_collected = 0;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
