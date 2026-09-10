#include "bluetooth_manager.h"
#include <esp_log.h>

static const char* TAG = "BluetoothManager";

const char* BluetoothManager::SERVICE_UUID = "180A";  // Device Information Service
const char* BluetoothManager::CHARACTERISTIC_UUID_CPS = "2A58";     // CPS Value
const char* BluetoothManager::CHARACTERISTIC_UUID_QUALITY = "2A19";  // Battery Level (reused for quality)
const char* BluetoothManager::CHARACTERISTIC_UUID_POINT = "2A37";    // Heart Rate (reused for points)
const char* BluetoothManager::CHARACTERISTIC_UUID_CMD = "2A39";      // Alert Level (reused for commands)

BluetoothManager::BluetoothManager()
    : _service(nullptr), _cps_characteristic(nullptr),
      _quality_characteristic(nullptr), _point_characteristic(nullptr),
      _command_characteristic(nullptr), _initialized(false),
      _connected(false), _last_update(0) {
}

BluetoothManager::~BluetoothManager() {
}

bool BluetoothManager::init(const char* device_name) {
    if (!BLE.begin()) {
        ESP_LOGE(TAG, "Failed to initialize BLE");
        return false;
    }
    
    BLE.setLocalName(device_name);
    BLE.setAdvertisedService(SERVICE_UUID);
    
    setupBLEService();
    
    startAdvertising();
    
    _initialized = true;
    ESP_LOGI(TAG, "Bluetooth LE initialized as '%s'", device_name);
    return true;
}

void BluetoothManager::setupBLEService() {
    // Create service
    _service = new BLEService(SERVICE_UUID);
    
    // Create characteristics (read/write/notify)
    _cps_characteristic = new BLECharacteristic(CHARACTERISTIC_UUID_CPS, 
                                               BLERead | BLENotify, 4);
    _quality_characteristic = new BLECharacteristic(CHARACTERISTIC_UUID_QUALITY,
                                                   BLERead | BLENotify, 1);
    _point_characteristic = new BLECharacteristic(CHARACTERISTIC_UUID_POINT,
                                                 BLERead | BLENotify, 20);
    _command_characteristic = new BLECharacteristic(CHARACTERISTIC_UUID_CMD,
                                                   BLEWrite, 4);
    
    // Add characteristics to service
    _service->addCharacteristic(*_cps_characteristic);
    _service->addCharacteristic(*_quality_characteristic);
    _service->addCharacteristic(*_point_characteristic);
    _service->addCharacteristic(*_command_characteristic);
    
    // Add service
    BLE.addService(*_service);
    
    // Set initial values
    uint8_t cps_value[4] = {0, 0, 0, 0};
    _cps_characteristic->writeValue(cps_value, 4);
    
    uint8_t quality_value = 0;
    _quality_characteristic->writeValue(quality_value);
    
    ESP_LOGI(TAG, "BLE Service and Characteristics configured");
}

void BluetoothManager::startAdvertising() {
    BLE.advertise();
    ESP_LOGI(TAG, "Advertising started");
}

void BluetoothManager::stopAdvertising() {
    BLE.stopAdvertise();
    ESP_LOGI(TAG, "Advertising stopped");
}

void BluetoothManager::update() {
    if (!_initialized) return;
    
    // Check for new connections
    BLEDevice central = BLE.central();
    
    if (central) {
        _connected = true;
        ESP_LOGI(TAG, "Connected to: %s", central.address().c_str());
        
        // Handle incoming commands
        handleBLECommands();
    } else {
        if (_connected) {
            _connected = false;
            ESP_LOGI(TAG, "Disconnected");
            startAdvertising();
        }
    }
}

void BluetoothManager::sendMeasurementData(const AdvancedImpactAnalyzer& analyzer) {
    if (!_connected || !_cps_characteristic) return;
    
    const auto& stats = analyzer.getStats();
    
    // Pack CPS into 4 bytes (float)
    float cps = stats.cps_calculated;
    uint8_t cps_data[4];
    memcpy(cps_data, &cps, 4);
    _cps_characteristic->writeValue(cps_data, 4);
    
    // Send quality as single byte
    uint8_t quality = stats.quality_score;
    _quality_characteristic->writeValue(quality);
    
    ESP_LOGD(TAG, "Sent measurement: CPS=%.2f, Quality=%d%%", cps, quality);
}

void BluetoothManager::sendRealtimeCPS(float cps, uint8_t quality) {
    if (!_connected) return;
    
    if (_cps_characteristic) {
        uint8_t cps_data[4];
        memcpy(cps_data, &cps, 4);
        _cps_characteristic->writeValue(cps_data, 4);
    }
    
    if (_quality_characteristic) {
        _quality_characteristic->writeValue(quality);
    }
}

void BluetoothManager::sendMeasurementPoint(const AdvancedImpactAnalyzer::MeasurementPoint& point,
                                            uint8_t point_number) {
    if (!_connected || !_point_characteristic) return;
    
    // Pack point data into 20 bytes
    uint8_t data[20];
    size_t idx = 0;
    
    // Point number (1 byte)
    data[idx++] = point_number;
    
    // Time in ms (2 bytes, scaled)
    uint16_t time_scaled = (uint16_t)(point.time_ms * 10);
    memcpy(&data[idx], &time_scaled, 2);
    idx += 2;
    
    // Amplitude linear (2 bytes, fixed point 0.01 resolution)
    uint16_t amp = (uint16_t)(point.amplitude_linear * 1000);
    memcpy(&data[idx], &amp, 2);
    idx += 2;
    
    // Amplitude dB (2 bytes, fixed point 0.1 resolution)
    int16_t amp_db = (int16_t)(point.amplitude_db * 10);
    memcpy(&data[idx], &amp_db, 2);
    idx += 2;
    
    // Slope angle (2 bytes, fixed point 0.1 resolution)
    int16_t slope = (int16_t)(point.slope_angle_deg * 10);
    memcpy(&data[idx], &slope, 2);
    idx += 2;
    
    // Estimated frequency (2 bytes)
    uint16_t freq = (uint16_t)(point.estimated_freq_hz);
    memcpy(&data[idx], &freq, 2);
    idx += 2;
    
    // Energy (2 bytes, fixed point 0.01 resolution)
    uint16_t energy = (uint16_t)(point.segment_energy * 100);
    memcpy(&data[idx], &energy, 2);
    idx += 2;
    
    // Pad remaining bytes
    while (idx < 20) {
        data[idx++] = 0;
    }
    
    _point_characteristic->writeValue(data, 20);
}

void BluetoothManager::sendStatistics(const AdvancedImpactAnalyzer::AdvancedStats& stats) {
    if (!_connected) return;
    
    // Send comprehensive stats via multiple notifications
    // (Each characteristic limited to ~20 bytes)
    
    // First notification: CPS and quality
    sendRealtimeCPS(stats.cps_calculated, stats.quality_score);
    
    delay(100);  // Small delay between notifications
    
    // Could add more characteristics for additional stats
    ESP_LOGI(TAG, "Sent statistics: CPS=%.2f, Quality=%d%%, Consistency=%.1f%%",
            stats.cps_calculated, stats.quality_score, stats.consistency_percent);
}

void BluetoothManager::handleBLECommands() {
    if (!_command_characteristic) return;
    
    if (_command_characteristic->written()) {
        const uint8_t* data = _command_characteristic->value();
        uint8_t length = _command_characteristic->valueLength();
        
        if (length > 0) {
            uint8_t command = data[0];
            ESP_LOGD(TAG, "Received BLE command: 0x%02X", command);
            
            // Command protocol:
            // 0x01 = Start measurement
            // 0x02 = Stop measurement
            // 0x03 = Reset
            // 0x04 = Calibrate
            // 0x10-0x1F = Set gain (0x10 + value)
            
            // Application should handle these commands
            // This is just the interface layer
        }
    }
}
