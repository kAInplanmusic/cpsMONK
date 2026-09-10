#include "i2s_audio.h"
#include <esp_log.h>

static const char* TAG = "I2SAudio";

// Global I2S buffer for ISR
static int16_t i2s_read_buff[I2S_DMA_BUFF_LEN];

I2SAudio::I2SAudio() 
    : _ringBuffer(nullptr), _writePos(0), _readPos(0),
      _referenceLevel(MIC_REFERENCE_LEVEL), _gainDB(MIC_GAIN_DB),
      _agcEnabled(false), _peakAmplitude(0), _lastPeakUpdate(0) {
}

I2SAudio::~I2SAudio() {
    stop();
    if (_ringBuffer) {
        free(_ringBuffer);
    }
}

bool I2SAudio::init() {
    ESP_LOGI(TAG, "Initializing I2S audio input...");
    
    // Allocate ring buffer (use PSRAM if available)
    if (USE_PSRAM && psramFound()) {
        _ringBuffer = (int16_t*)ps_malloc(RINGBUFFER_SIZE * sizeof(int16_t));
        ESP_LOGI(TAG, "Using PSRAM for ring buffer");
    } else {
        _ringBuffer = (int16_t*)malloc(RINGBUFFER_SIZE * sizeof(int16_t));
    }
    
    if (!_ringBuffer) {
        ESP_LOGE(TAG, "Failed to allocate ring buffer");
        return false;
    }
    
    memset(_ringBuffer, 0, RINGBUFFER_SIZE * sizeof(int16_t));
    
    // Configure I2S interface
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = I2S_SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = I2S_DMA_BUFF_COUNT,
        .dma_buf_len = I2S_DMA_BUFF_LEN,
        .use_apll = true,
        .tx_desc_auto_clear = false,
        .fixed_mclk = 0
    };
    
    esp_err_t ret = i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install I2S driver: %s", esp_err_to_name(ret));
        return false;
    }
    
    // Configure I2S pins
    i2s_pin_config_t pin_config = {
        .bck_io_num = I2S_SCK_PIN,
        .ws_io_num = I2S_WS_PIN,
        .data_out_num = -1,  // Not used
        .data_in_num = I2S_SD_PIN
    };
    
    ret = i2s_set_pin(I2S_PORT, &pin_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set I2S pins: %s", esp_err_to_name(ret));
        return false;
    }
    
    ESP_LOGI(TAG, "I2S initialized successfully");
    return true;
}

bool I2SAudio::start() {
    ESP_LOGI(TAG, "Starting I2S audio capture");
    esp_err_t ret = i2s_start(I2S_PORT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start I2S: %s", esp_err_to_name(ret));
        return false;
    }
    return true;
}

bool I2SAudio::stop() {
    ESP_LOGI(TAG, "Stopping I2S audio capture");
    esp_err_t ret = i2s_stop(I2S_PORT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to stop I2S: %s", esp_err_to_name(ret));
        return false;
    }
    return true;
}

size_t I2SAudio::readSamples(int16_t* buffer, size_t samples) {
    if (!buffer || samples == 0) {
        return 0;
    }
    
    // Read from I2S DMA buffer
    size_t bytes_read = 0;
    esp_err_t ret = i2s_read(I2S_PORT, (void*)i2s_read_buff, 
                             I2S_DMA_BUFF_LEN * sizeof(int16_t), 
                             &bytes_read, pdMS_TO_TICKS(100));
    
    if (ret != ESP_OK || bytes_read == 0) {
        return 0;
    }
    
    size_t samples_read = bytes_read / sizeof(int16_t);
    size_t to_copy = (samples_read < samples) ? samples_read : samples;
    
    // Copy to ring buffer
    for (size_t i = 0; i < to_copy; i++) {
        _ringBuffer[_writePos] = i2s_read_buff[i];
        _writePos = (_writePos + 1) % RINGBUFFER_SIZE;
        
        // Check for peak amplitude
        int16_t abs_val = abs(i2s_read_buff[i]);
        if (abs_val > _peakAmplitude) {
            _peakAmplitude = abs_val;
        }
    }
    
    // Copy from ring buffer to output
    size_t available = 0;
    if (_writePos >= _readPos) {
        available = _writePos - _readPos;
    } else {
        available = RINGBUFFER_SIZE - _readPos + _writePos;
    }
    
    size_t to_read = (available < samples) ? available : samples;
    for (size_t i = 0; i < to_read; i++) {
        buffer[i] = _ringBuffer[_readPos];
        _readPos = (_readPos + 1) % RINGBUFFER_SIZE;
    }
    
    return to_read;
}

size_t I2SAudio::getAvailableSamples() {
    if (_writePos >= _readPos) {
        return _writePos - _readPos;
    } else {
        return RINGBUFFER_SIZE - _readPos + _writePos;
    }
}

float I2SAudio::getAudioLevelDB() {
    float rms = calculateRMS(_ringBuffer, RINGBUFFER_SIZE);
    return amplitudeToDBFS(rms);
}

void I2SAudio::calibrateReference() {
    _referenceLevel = calculateRMS(_ringBuffer, RINGBUFFER_SIZE);
    ESP_LOGI(TAG, "Microphone calibrated. Reference level: %.2f", _referenceLevel);
}

void I2SAudio::setGain(float gainDB) {
    _gainDB = constrain(gainDB, -20.0f, 40.0f);
    ESP_LOGI(TAG, "Microphone gain set to: %.2f dB", _gainDB);
}

void I2SAudio::setAGC(bool enable) {
    _agcEnabled = enable;
    ESP_LOGI(TAG, "AGC %s", enable ? "enabled" : "disabled");
}

uint16_t I2SAudio::getPeakAmplitude() {
    uint16_t peak = _peakAmplitude;
    // Reset peak every 100ms
    if (millis() - _lastPeakUpdate > 100) {
        _peakAmplitude = 0;
        _lastPeakUpdate = millis();
    }
    return peak;
}

float I2SAudio::calculateRMS(const int16_t* buffer, size_t samples) {
    if (!buffer || samples == 0) {
        return 0.0f;
    }
    
    float sum = 0.0f;
    for (size_t i = 0; i < samples; i++) {
        float sample = (float)buffer[i] / 32768.0f;
        sum += sample * sample;
    }
    
    return sqrt(sum / samples);
}

float I2SAudio::amplitudeToDBFS(float amplitude) {
    const float MIN_DB = -80.0f;
    const float REF = 1.0f;  // Reference is 1.0 for full-scale
    
    if (amplitude <= 0.0f) {
        return MIN_DB;
    }
    
    float db = 20.0f * log10(amplitude / REF) + _gainDB;
    return (db < MIN_DB) ? MIN_DB : db;
}
