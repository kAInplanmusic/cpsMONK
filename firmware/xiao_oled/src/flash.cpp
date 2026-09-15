#include "flash.h"

#include "esp_partition.h"
#include "esp_log.h"

static const char *TAG = "flash";

int flash_write_measurement(const uint8_t *data, size_t len, uint32_t offset) {
    const esp_partition_t *part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_EXT1, "app_data");
    if (!part) {
        ESP_LOGE(TAG, "app_data partition not found");
        return -1;
    }
    if (offset + len > part->size) {
        ESP_LOGE(TAG, "write exceeds partition size");
        return -1;
    }
    esp_err_t err = esp_partition_write(part, offset, data, len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "flash write failed: %s", esp_err_to_name(err));
        return -1;
    }
    return 0;
}

int flash_read_measurement(uint8_t *data, size_t len, uint32_t offset) {
    const esp_partition_t *part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_EXT1, "app_data");
    if (!part) {
        ESP_LOGE(TAG, "app_data partition not found");
        return -1;
    }
    if (offset + len > part->size) {
        ESP_LOGE(TAG, "read exceeds partition size");
        return -1;
    }
    esp_err_t err = esp_partition_read(part, offset, data, len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "flash read failed: %s", esp_err_to_name(err));
        return -1;
    }
    return 0;
}
