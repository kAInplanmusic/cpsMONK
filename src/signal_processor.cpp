#include "signal_processor.h"
#include <esp_log.h>
#include <math.h>
#include <dsp_common.h>
#include "dsps_fft2r.h"
#include "dsps_wind_hann.h"

static const char* TAG = "SignalProcessor";

SignalProcessor::SignalProcessor()
    : _fftInput(nullptr), _fftOutput(nullptr), _window(nullptr),
      _waveformBuffer(nullptr), _waveformWritePos(0),
      _rmsEnergy(0.0f), _peakToPeak(0.0f), _noiseFloor(-60.0f),
      _lastImpactTime(0), _qualityScore(0) {
    memset(&_filterState, 0, sizeof(_filterState));
}

SignalProcessor::~SignalProcessor() {
    if (_fftInput) free(_fftInput);
    if (_fftOutput) free(_fftOutput);
    if (_window) free(_window);
    if (_waveformBuffer) free(_waveformBuffer);
}

bool SignalProcessor::init() {
    ESP_LOGI(TAG, "Initializing signal processor...");
    
    // Allocate FFT buffers (ESP-DSP: interleaved re/im -> 2*FFT_SIZE floats)
    _fftInput = (float*)malloc(2 * FFT_SIZE * sizeof(float));
    _fftOutput = (float*)malloc(2 * FFT_SIZE * sizeof(float));
    _window = (float*)malloc(FFT_SIZE * sizeof(float));
    _waveformBuffer = (int16_t*)malloc(RINGBUFFER_SIZE * sizeof(int16_t));
    
    if (!_fftInput || !_fftOutput || !_window || !_waveformBuffer) {
        ESP_LOGE(TAG, "Failed to allocate FFT buffers");
        return false;
    }
    
    // Generate Hann window
    for (size_t i = 0; i < FFT_SIZE; i++) {
        float n = (float)i / (FFT_SIZE - 1);
        _window[i] = 0.5f * (1.0f - cosf(2.0f * M_PI * n));
    }
    
    // Initialize spectrum data vectors
    _spectrumData.magnitude.resize(FFT_BIN_COUNT, 0.0f);
    _spectrumData.frequency.resize(FFT_BIN_COUNT, 0.0f);
    
    // Initialize frequency axis
    for (size_t i = 0; i < FFT_BIN_COUNT; i++) {
        _spectrumData.frequency[i] = binIndexToFrequency(i);
    }
    
    // Initialize bandpass filter (default: 100-8000 Hz)
    updateBandpassFilter(FREQ_MIN_BAND, FREQ_MAX_BAND, 1.0f);
    
    ESP_LOGI(TAG, "Signal processor initialized (FFT size: %d)", FFT_SIZE);
    return true;
}

void SignalProcessor::processSamples(const int16_t* samples, size_t count) {
    if (!samples || count == 0) {
        return;
    }
    
    // Update waveform buffer (circular)
    for (size_t i = 0; i < count; i++) {
        _waveformBuffer[_waveformWritePos] = samples[i];
        _waveformWritePos = (_waveformWritePos + 1) % RINGBUFFER_SIZE;
    }
    
    // Calculate RMS energy and peak-to-peak
    float sum_squared = 0.0f;
    int16_t min_val = samples[0];
    int16_t max_val = samples[0];
    
    for (size_t i = 0; i < count; i++) {
        float normalized = (float)samples[i] / 32768.0f;
        sum_squared += normalized * normalized;
        if (samples[i] < min_val) min_val = samples[i];
        if (samples[i] > max_val) max_val = samples[i];
    }
    
    _rmsEnergy = sqrt(sum_squared / count);
    _peakToPeak = (float)(max_val - min_val);
}

bool SignalProcessor::computeFFT() {
    if (!_fftInput || !_fftOutput || !_window) {
        return false;
    }
    
    // Convert waveform to float and apply window
    size_t start_pos = (_waveformWritePos >= FFT_SIZE) ? 
                       (_waveformWritePos - FFT_SIZE) : 0;
    
    for (size_t i = 0; i < FFT_SIZE; i++) {
        size_t idx = (start_pos + i) % RINGBUFFER_SIZE;
        float sample = (float)_waveformBuffer[idx] / 32768.0f;
        _fftInput[i] = sample * _window[i];
    }
    
    // Perform real FFT using ESP-DSP (S3-Hardwareoptimiert, In-Place, interleaved re/im)
    static bool fft_initialized = false;
    if (!fft_initialized) {
        esp_err_t ret = dsps_fft2r_init_fc32(NULL, CONFIG_DSP_MAX_FFT_SIZE);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "ESP-DSP FFT init failed: %d", ret);
            return;
        }
        fft_initialized = true;
    }
    // ESP-DSP arbeitet in-place: Output ueberschreibt Input, interleaved [re0,im0,re1,im1,...]
    dsps_fft2r_fc32(_fftInput, FFT_SIZE);
    dsps_bit_rev_fc32(_fftInput, FFT_SIZE);
    // _fftOutput zeigt auf dieselben interleaved Daten
    memcpy(_fftOutput, _fftInput, FFT_SIZE * 2 * sizeof(float));
    
    // Convert to magnitude spectrum in dB
    float peak_mag = 0.0f;
    size_t peak_idx = 0;
    
    for (size_t k = 0; k < FFT_BIN_COUNT; k++) {
        // Real and imaginary parts are interleaved
        float real = _fftOutput[2 * k];
        float imag = _fftOutput[2 * k + 1];
        
        float magnitude = sqrt(real * real + imag * imag);
        magnitude = 2.0f * magnitude / FFT_SIZE;  // Normalize
        
        float mag_db = (magnitude > 0) ? 20.0f * log10(magnitude) : -80.0f;
        _spectrumData.magnitude[k] = mag_db;
        
        if (mag_db > peak_mag) {
            peak_mag = mag_db;
            peak_idx = k;
        }
    }
    
    _spectrumData.peakMagnitude = peak_mag;
    _spectrumData.peakFrequency = binIndexToFrequency(peak_idx);
    
    // Calculate spectral centroid
    float sum_freq_mag = 0.0f;
    float sum_mag = 0.0f;
    for (size_t k = 0; k < FFT_BIN_COUNT; k++) {
        float mag_linear = pow(10.0f, _spectrumData.magnitude[k] / 20.0f);
        sum_freq_mag += _spectrumData.frequency[k] * mag_linear;
        sum_mag += mag_linear;
    }
    
    _spectrumData.spectralCentroid = (sum_mag > 0) ? (sum_freq_mag / sum_mag) : 0.0f;
    
    return true;
}

bool SignalProcessor::detectPeaks(size_t minDistance, float threshold) {
    bool peaks_detected = false;
    unsigned long current_time = micros();
    
    // Find peaks in time domain
    int16_t max_sample = 0;
    size_t max_idx = 0;
    
    for (size_t i = 1; i < RINGBUFFER_SIZE - 1; i++) {
        int16_t prev = abs(_waveformBuffer[(i - 1) % RINGBUFFER_SIZE]);
        int16_t curr = abs(_waveformBuffer[i]);
        int16_t next = abs(_waveformBuffer[(i + 1) % RINGBUFFER_SIZE]);
        
        if (curr > prev && curr > next && curr > max_sample) {
            max_sample = curr;
            max_idx = i;
        }
    }
    
    // Check if peak is significant and far from last impact
    float threshold_linear = pow(10.0f, threshold / 20.0f);
    if (max_sample > threshold_linear * 32767.0f && 
        (current_time - _lastImpactTime) > (minDistance * 1000000UL / SAMPLE_RATE)) {
        
        ImpactEvent impact;
        impact.timestamp = current_time;
        impact.peakAmplitude = (float)max_sample / 32767.0f;
        impact.peakAmplitudeDB = 20.0f * log10(impact.peakAmplitude + 1e-6f);
        impact.fundamentalFreq = _spectrumData.peakFrequency;
        impact.totalEnergy = _rmsEnergy;
        impact.noiseFloor = _noiseFloor;
        
        _impactEvents.push_back(impact);
        _lastImpactTime = current_time;
        peaks_detected = true;
        
        // Limit history size
        if (_impactEvents.size() > QUALITY_HISTORY_SIZE) {
            _impactEvents.erase(_impactEvents.begin());
        }
    }
    
    return peaks_detected;
}

size_t SignalProcessor::getWaveformData(int16_t* buffer, size_t samples) const {
    if (!buffer || samples == 0) {
        return 0;
    }
    
    size_t to_copy = (samples < RINGBUFFER_SIZE) ? samples : RINGBUFFER_SIZE;
    memcpy(buffer, _waveformBuffer, to_copy * sizeof(int16_t));
    return to_copy;
}

float SignalProcessor::calculateCPS(float windowSec) {
    if (_impactEvents.empty()) {
        return 0.0f;
    }
    
    unsigned long window_us = (unsigned long)(windowSec * 1000000.0f);
    unsigned long current_time = micros();
    unsigned long cutoff_time = current_time - window_us;
    
    int count = 0;
    for (const auto& event : _impactEvents) {
        if (event.timestamp > cutoff_time) {
            count++;
        }
    }
    
    return (float)count / windowSec;
}

std::vector<float> SignalProcessor::getImpactIntervalStats() const {
    std::vector<float> stats = {0.0f, 0.0f, 0.0f, 0.0f};  // mean, stddev, min, max
    
    if (_impactEvents.size() < 2) {
        return stats;
    }
    
    _impactIntervals.clear();
    for (size_t i = 1; i < _impactEvents.size(); i++) {
        float interval_ms = (float)(_impactEvents[i].timestamp - 
                            _impactEvents[i-1].timestamp) / 1000.0f;
        _impactIntervals.push_back(interval_ms);
    }
    
    if (_impactIntervals.empty()) {
        return stats;
    }
    
    // Calculate mean
    float sum = 0.0f;
    for (float interval : _impactIntervals) {
        sum += interval;
    }
    stats[0] = sum / _impactIntervals.size();
    
    // Calculate standard deviation
    float sum_sq = 0.0f;
    for (float interval : _impactIntervals) {
        float diff = interval - stats[0];
        sum_sq += diff * diff;
    }
    stats[1] = sqrt(sum_sq / _impactIntervals.size());
    
    // Min and Max
    stats[2] = *std::min_element(_impactIntervals.begin(), _impactIntervals.end());
    stats[3] = *std::max_element(_impactIntervals.begin(), _impactIntervals.end());
    
    return stats;
}

void SignalProcessor::updateBandpassFilter(float freqLow, float freqHigh, float Q) {
    float wc1 = 2.0f * M_PI * freqLow / SAMPLE_RATE;
    float wc2 = 2.0f * M_PI * freqHigh / SAMPLE_RATE;
    float wc = sqrt(wc1 * wc2);
    float bw = wc2 - wc1;
    
    // Compute filter coefficients (simplified second-order Butterworth)
    float d = bw / Q;
    _filterState.a1 = -2.0f * cos(wc) / (1.0f + d);
    _filterState.a2 = (1.0f - d) / (1.0f + d);
    _filterState.b0 = d / (1.0f + d);
    _filterState.b1 = 0.0f;
    _filterState.b2 = -d / (1.0f + d);
}

void SignalProcessor::applyBandpassFilter(const int16_t* samples, int16_t* output, size_t count) {
    for (size_t i = 0; i < count; i++) {
        float x = (float)samples[i] / 32768.0f;
        float y = _filterState.b0 * x + _filterState.b1 * _filterState.x1 + 
                  _filterState.b2 * _filterState.x2 - _filterState.a1 * _filterState.y1 - 
                  _filterState.a2 * _filterState.y2;
        
        _filterState.x2 = _filterState.x1;
        _filterState.x1 = x;
        _filterState.y2 = _filterState.y1;
        _filterState.y1 = y;
        
        output[i] = (int16_t)(y * 32767.0f);
    }
}

std::vector<float> SignalProcessor::analyzeHarmonics() {
    std::vector<float> harmonics;
    
    if (_impactEvents.empty()) {
        return harmonics;
    }
    
    float fundamental = _spectrumData.peakFrequency;
    if (fundamental < 1.0f) {
        return harmonics;
    }
    
    // Find harmonics (multiples of fundamental)
    for (int h = 1; h <= 5; h++) {
        float harmonic_freq = fundamental * h;
        size_t bin = (size_t)(harmonic_freq * FFT_SIZE / SAMPLE_RATE);
        
        if (bin < _spectrumData.magnitude.size()) {
            harmonics.push_back(_spectrumData.magnitude[bin]);
        }
    }
    
    return harmonics;
}

uint8_t SignalProcessor::getQualityScore() {
    return _qualityScore;
}

void SignalProcessor::applyHannWindow(float* buffer, size_t count) {
    for (size_t i = 0; i < count; i++) {
        float n = (float)i / (count - 1);
        float window = 0.5f * (1.0f - cosf(2.0f * M_PI * n));
        buffer[i] *= window;
    }
}

std::vector<size_t> SignalProcessor::findSpectrumPeaks(const std::vector<float>& magnitudes, 
                                                      float threshold) {
    std::vector<size_t> peaks;
    
    if (magnitudes.size() < 3) {
        return peaks;
    }
    
    for (size_t i = 1; i < magnitudes.size() - 1; i++) {
        if (magnitudes[i] > magnitudes[i-1] && magnitudes[i] > magnitudes[i+1] &&
            magnitudes[i] > threshold) {
            peaks.push_back(i);
        }
    }
    
    return peaks;
}

float SignalProcessor::binIndexToFrequency(size_t binIndex) const {
    return (float)binIndex * SAMPLE_RATE / FFT_SIZE;
}

void SignalProcessor::updateQualityScore() {
    auto stats = getImpactIntervalStats();
    if (stats[0] < 0.1f) {
        _qualityScore = 0;
        return;
    }
    
    // Calculate consistency score (0-100)
    float cv = (stats[1] / stats[0]) * 100.0f;  // Coefficient of variation
    float consistency_score = 100.0f - std::min(cv, 50.0f);
    
    // Calculate spectral score
    float spectral_score = 50.0f;
    if (_spectrumData.peakMagnitude > -10.0f) {
        spectral_score = 100.0f;
    } else if (_spectrumData.peakMagnitude > -30.0f) {
        spectral_score = 75.0f;
    } else if (_spectrumData.peakMagnitude > -50.0f) {
        spectral_score = 50.0f;
    }
    
    _qualityScore = (uint8_t)((consistency_score * 0.5f + spectral_score * 0.5f));
    _qualityScore = std::min((uint8_t)100, _qualityScore);
}
