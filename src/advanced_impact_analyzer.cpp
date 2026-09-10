#include "advanced_impact_analyzer.h"
#include <esp_log.h>
#include <algorithm>
#include <numeric>

static const char* TAG = "AdvancedImpactAnalyzer";

AdvancedImpactAnalyzer::AdvancedImpactAnalyzer()
    : _max_points(12), _microphone_distance_mm(100.0f), _microphone_angle_deg(0.0f),
      _measuring(false), _measurement_start_time(0),
      _impulse_buffer_pos(0), _impulse_active(false), _impulse_start_idx(0),
      _last_sample(0), _rising_edge_detected(false) {
    memset(&_lastWaveform, 0, sizeof(_lastWaveform));
    memset(&_stats, 0, sizeof(_stats));
}

AdvancedImpactAnalyzer::~AdvancedImpactAnalyzer() {
    _waveforms.clear();
    _impulse_buffer.clear();
    _segment_buffer.clear();
}

bool AdvancedImpactAnalyzer::init(uint8_t max_points) {
    ESP_LOGI(TAG, "Initializing Advanced Impact Analyzer...");
    ESP_LOGI(TAG, "  Max measurement points: %d", max_points);
    
    _max_points = constrain(max_points, 6, 20);
    
    _impulse_buffer.resize(WAVEFORM_BUFFER_SIZE);
    memset(_impulse_buffer.data(), 0, WAVEFORM_BUFFER_SIZE * sizeof(int16_t));
    
    _segment_buffer.reserve(2048);
    
    reset();
    
    ESP_LOGI(TAG, "Advanced Impact Analyzer initialized (0-250 CPS, multi-point analysis)");
    return true;
}

void AdvancedImpactAnalyzer::startMeasurement() {
    _measuring = true;
    _measurement_start_time = millis();
    _waveforms.clear();
    _stats.samples_collected = 0;
    _stats.measurement_complete = false;
    _stats.quality_score = 0;
    
    ESP_LOGI(TAG, "Measurement started - collecting %d impact waveforms...", MEASUREMENT_CYCLES_MODE2);
}

void AdvancedImpactAnalyzer::processSample(int16_t sample, uint32_t sample_index) {
    if (!_measuring) {
        return;
    }
    
    // Detect rising edge (start of impulse)
    if (!_rising_edge_detected && _last_sample < 0 && sample >= 0) {
        _impulse_active = true;
        _impulse_start_idx = sample_index;
        _impulse_buffer_pos = 0;
        _rising_edge_detected = true;
        _segment_buffer.clear();
    }
    
    // Buffer impulse samples
    if (_impulse_active && _impulse_buffer_pos < WAVEFORM_BUFFER_SIZE) {
        _impulse_buffer[_impulse_buffer_pos++] = sample;
        _segment_buffer.push_back(sample);
    }
    
    // Detect end of impulse (return to baseline, ~100ms after start)
    if (_impulse_active && _impulse_buffer_pos > (SAMPLE_RATE / 100)) {  // 10ms minimum
        // Check if returned to near-baseline
        float current_level = abs(sample) / 32767.0f;
        if (current_level < 0.05f) {
            // End of impulse detected
            _impulse_active = false;
            
            // Extract waveform points
            if (extractWaveformPoints()) {
                if (_waveforms.size() >= MEASUREMENT_CYCLES_MODE2) {
                    stopMeasurement();
                }
            }
            
            _rising_edge_detected = false;
        }
    }
    
    _last_sample = sample;
}

bool AdvancedImpactAnalyzer::extractWaveformPoints() {
    if (_segment_buffer.size() < 100) {
        return false;
    }
    
    ImpactWaveform waveform;
    waveform.cycle_number = _waveforms.size() + 1;
    waveform.timestamp_ms = millis();
    
    // Find all extrema (peaks and valleys)
    std::vector<uint32_t> extrema_indices = findExtrema();
    
    if (extrema_indices.empty()) {
        return false;
    }
    
    // Add baseline point at start
    waveform.points.push_back(createMeasurementPoint(0, BASELINE));
    
    // Add rising edge point
    waveform.points.push_back(createMeasurementPoint(0, RISING_EDGE));
    
    // Add peaks and valleys (limited by _max_points)
    bool is_peak = (_segment_buffer[extrema_indices[0]] > 0);  // First is peak if positive
    
    for (size_t i = 0; i < extrema_indices.size() && waveform.points.size() < _max_points; i++) {
        uint32_t idx = extrema_indices[i];
        PointType type = is_peak ? PEAK : VALLEY;
        
        MeasurementPoint point = createMeasurementPoint(idx, type);
        waveform.points.push_back(point);
        
        is_peak = !is_peak;  // Alternate between peak and valley
    }
    
    // Add end impulse point
    if (waveform.points.size() < _max_points) {
        waveform.points.push_back(
            createMeasurementPoint(_segment_buffer.size() - 1, END_IMPULSE)
        );
    }
    
    waveform.point_count = waveform.points.size();
    
    // Calculate overall characteristics
    waveform.total_energy = 0.0f;
    waveform.peak_amplitude = 0.0f;
    waveform.primary_peak_idx = 0;
    
    for (size_t i = 0; i < waveform.points.size(); i++) {
        waveform.total_energy += waveform.points[i].segment_energy;
        
        if (waveform.points[i].type == PEAK) {
            if (waveform.points[i].amplitude_linear > waveform.peak_amplitude) {
                waveform.peak_amplitude = waveform.points[i].amplitude_linear;
                waveform.peak_amplitude_db = waveform.points[i].amplitude_db;
                if (waveform.primary_peak_idx == 0) {
                    waveform.primary_peak_idx = i;
                }
            }
        }
    }
    
    // Calculate harmonics, decay, and quality
    calculateHarmonics(waveform);
    calculateDecayRate(waveform);
    calculateQualityScore(waveform);
    
    // Store waveform
    _lastWaveform = waveform;
    _waveforms.push_back(waveform);
    _stats.samples_collected = _waveforms.size();
    
    return true;
}

bool AdvancedImpactAnalyzer::stopMeasurement() {
    if (!_measuring || _waveforms.size() < 10) {
        ESP_LOGW(TAG, "Measurement incomplete: only %d waveforms", _waveforms.size());
        return false;
    }
    
    _measuring = false;
    
    // Compute statistics
    computeStatistics();
    updateOverallQuality();
    
    _stats.measurement_complete = true;
    
    ESP_LOGI(TAG, "Measurement complete!");
    ESP_LOGI(TAG, "  CPS: %.2f", _stats.cps_calculated);
    ESP_LOGI(TAG, "  Quality: %d%%", _stats.quality_score);
    ESP_LOGI(TAG, "  Consistency: %.1f%%", _stats.consistency_percent);
    ESP_LOGI(TAG, "  Harmonic Content: %.1f%%", _stats.mean_harmonic_2_1 * 100.0f);
    
    return true;
}

uint16_t AdvancedImpactAnalyzer::getQualityColor() const {
    uint8_t quality = _stats.quality_score;
    
    if (quality >= 95) return COLOR_GREEN;
    if (quality >= 86) return COLOR_GREEN;
    if (quality >= 75) return COLOR_YELLOW;
    if (quality >= 61) return COLOR_ORANGE;
    return COLOR_RED;
}

uint8_t AdvancedImpactAnalyzer::getMeasurementProgress() const {
    if (!_measuring) return 0;
    return (uint8_t)((_waveforms.size() * 100) / MEASUREMENT_CYCLES_MODE2);
}

void AdvancedImpactAnalyzer::setMicrophonePosition(float distance_mm, float angle_deg) {
    _microphone_distance_mm = constrain(distance_mm, 10.0f, 500.0f);
    _microphone_angle_deg = constrain(angle_deg, -180.0f, 180.0f);
    ESP_LOGI(TAG, "Microphone: %.0f mm, %.0f°", _microphone_distance_mm, _microphone_angle_deg);
}

void AdvancedImpactAnalyzer::getMicrophonePosition(float& distance_mm, float& angle_deg) const {
    distance_mm = _microphone_distance_mm;
    angle_deg = _microphone_angle_deg;
}

void AdvancedImpactAnalyzer::reset() {
    _waveforms.clear();
    _measuring = false;
    _impulse_buffer_pos = 0;
    _impulse_active = false;
    _rising_edge_detected = false;
    memset(&_stats, 0, sizeof(_stats));
    memset(&_lastWaveform, 0, sizeof(_lastWaveform));
}

std::vector<uint32_t> AdvancedImpactAnalyzer::findExtrema() {
    std::vector<uint32_t> extrema;
    
    if (_segment_buffer.size() < 5) return extrema;
    
    // Find peaks and valleys
    for (size_t i = 2; i < _segment_buffer.size() - 2; i++) {
        int16_t current = _segment_buffer[i];
        int16_t prev1 = _segment_buffer[i - 1];
        int16_t prev2 = _segment_buffer[i - 2];
        int16_t next1 = _segment_buffer[i + 1];
        int16_t next2 = _segment_buffer[i + 2];
        
        // Peak: current > neighbors
        if (current > prev1 && current > prev2 && current > next1 && current > next2) {
            extrema.push_back(i);
        }
        // Valley: current < neighbors
        else if (current < prev1 && current < prev2 && current < next1 && current < next2) {
            extrema.push_back(i);
        }
    }
    
    return extrema;
}

AdvancedImpactAnalyzer::MeasurementPoint AdvancedImpactAnalyzer::createMeasurementPoint(
    uint32_t sample_idx, PointType type) {
    
    MeasurementPoint point;
    point.type = type;
    point.sample_index = sample_idx;
    point.time_ms = (float)sample_idx * 1000.0f / SAMPLE_RATE;
    
    if (sample_idx < _segment_buffer.size()) {
        int16_t raw = _segment_buffer[sample_idx];
        point.raw_sample = raw;
        point.amplitude_linear = abs(raw) / 32767.0f;
        point.amplitude_db = 20.0f * log10(point.amplitude_linear + 1e-6f);
    }
    
    // Calculate slope angle if we have previous point
    if (sample_idx > 1) {
        point.slope_angle_deg = calculateSlopeAngle(sample_idx - 1, sample_idx);
    }
    
    // Calculate segment energy
    if (sample_idx > 0) {
        point.segment_energy = calculateSegmentEnergy(sample_idx - 1, sample_idx);
    }
    
    // Estimate local frequency
    point.estimated_freq_hz = estimateLocalFrequency(sample_idx);
    
    return point;
}

float AdvancedImpactAnalyzer::calculateSlopeAngle(uint32_t idx1, uint32_t idx2) {
    if (idx2 >= _segment_buffer.size() || idx1 >= _segment_buffer.size()) {
        return 0.0f;
    }
    
    float dy = _segment_buffer[idx2] - _segment_buffer[idx1];
    float dx = 1.0f;  // One sample
    
    float slope = dy / dx;
    float angle_rad = atan(slope / 32767.0f);  // Normalize
    float angle_deg = angle_rad * 180.0f / M_PI;
    
    return angle_deg;
}

float AdvancedImpactAnalyzer::calculateSegmentEnergy(uint32_t idx1, uint32_t idx2) {
    if (idx2 >= _segment_buffer.size() || idx1 >= _segment_buffer.size()) {
        return 0.0f;
    }
    
    // Trapezoid rule for area under curve
    float energy = 0.0f;
    for (uint32_t i = idx1; i < idx2 && i < _segment_buffer.size(); i++) {
        energy += abs(_segment_buffer[i]) / 32767.0f;
    }
    
    return energy / (idx2 - idx1 + 1);  // Average
}

float AdvancedImpactAnalyzer::estimateLocalFrequency(uint32_t point_idx) {
    if (point_idx < 10 || point_idx >= _segment_buffer.size() - 10) {
        return 0.0f;
    }
    
    // Find zero crossings around this point
    uint32_t zero_crossing_count = 0;
    for (uint32_t i = point_idx - 10; i < point_idx + 10; i++) {
        if (_segment_buffer[i] * _segment_buffer[i + 1] < 0) {
            zero_crossing_count++;
        }
    }
    
    // Estimate period from zero crossings
    if (zero_crossing_count > 0) {
        float period_samples = 20.0f / zero_crossing_count;
        float freq = SAMPLE_RATE / period_samples;
        return constrain(freq, 0.0f, SAMPLE_RATE / 2.0f);
    }
    
    return 0.0f;
}

void AdvancedImpactAnalyzer::calculateHarmonics(ImpactWaveform& waveform) {
    if (waveform.points.size() < 3) return;
    
    // Find peak amplitudes
    float peak1 = 0.0f, peak2 = 0.0f, peak3 = 0.0f;
    uint8_t peak_count = 0;
    
    for (const auto& point : waveform.points) {
        if (point.type == PEAK) {
            peak_count++;
            if (peak_count == 1) peak1 = point.amplitude_linear;
            else if (peak_count == 2) peak2 = point.amplitude_linear;
            else if (peak_count == 3) peak3 = point.amplitude_linear;
        }
    }
    
    // Calculate ratios
    if (peak1 > 0.01f) {
        waveform.harmonic_ratio_2_1 = peak2 / peak1;
        waveform.harmonic_ratio_3_1 = peak3 / peak1;
        waveform.harmonic_content_percent = (peak2 + peak3) / peak1 * 100.0f;
    }
}

void AdvancedImpactAnalyzer::calculateDecayRate(ImpactWaveform& waveform) {
    if (waveform.points.size() < 2) return;
    
    // Find first and second peaks
    std::vector<const MeasurementPoint*> peaks;
    for (const auto& point : waveform.points) {
        if (point.type == PEAK) {
            peaks.push_back(&point);
        }
    }
    
    if (peaks.size() >= 2) {
        float a1 = peaks[0]->amplitude_linear;
        float a2 = peaks[1]->amplitude_linear;
        float t1 = peaks[0]->time_ms;
        float t2 = peaks[1]->time_ms;
        
        if (t2 > t1 && a1 > 0.01f) {
            // Exponential decay: a2 = a1 * exp(-decay_rate * dt)
            float dt = t2 - t1;
            waveform.decay_rate = -log(a2 / a1) / dt;
            
            // Q-factor approximation
            waveform.quality_factor_q = M_PI / waveform.decay_rate;
        }
    }
    
    // Envelope area (energy integral)
    for (const auto& point : waveform.points) {
        waveform.envelope_area += point.segment_energy;
    }
}

void AdvancedImpactAnalyzer::calculateQualityScore(ImpactWaveform& waveform) {
    // Quality based on:
    // - Cleanliness: Peak ratio (should be close to expected values)
    // - Consistency: Comparison to first waveform (if available)
    // - Overall: Energy, decay rate, harmonic content
    
    waveform.cleanliness_score = 100;  // Start at 100
    
    // Reduce score if harmonics are too high (over 50%)
    if (waveform.harmonic_content_percent > 50.0f) {
        waveform.cleanliness_score -= (uint8_t)((waveform.harmonic_content_percent - 50.0f) / 2.0f);
    }
    
    // Consistency score
    if (_waveforms.size() > 1) {
        float period_diff = fabsf((float)(waveform.timestamp_ms - _lastWaveform.timestamp_ms));
        if (period_diff > 0) {
            waveform.consistency_score = (uint8_t)(100.0f - fabsf(period_diff - _stats.mean_period_ms) / _stats.mean_period_ms * 100.0f);
        } else {
            waveform.consistency_score = 100;
        }
    } else {
        waveform.consistency_score = 100;
    }
    
    // Overall quality
    waveform.overall_quality = (waveform.cleanliness_score + waveform.consistency_score) / 2;
    waveform.overall_quality = constrain(waveform.overall_quality, (uint8_t)0, (uint8_t)100);
}

void AdvancedImpactAnalyzer::computeStatistics() {
    if (_waveforms.size() < 2) return;
    
    // Initialize statistics vectors
    _stats.point_time_means.clear();
    _stats.point_time_std_devs.clear();
    _stats.point_amplitude_means.clear();
    _stats.point_amplitude_stds.clear();
    
    // Calculate period statistics
    std::vector<float> periods;
    for (size_t i = 1; i < _waveforms.size(); i++) {
        float period_ms = (float)(_waveforms[i].timestamp_ms - _waveforms[i - 1].timestamp_ms);
        periods.push_back(period_ms);
    }
    
    if (!periods.empty()) {
        float sum = std::accumulate(periods.begin(), periods.end(), 0.0f);
        _stats.mean_period_ms = sum / periods.size();
        _stats.cps_calculated = (_stats.mean_period_ms > 0) ? (1000.0f / _stats.mean_period_ms) : 0.0f;
        _stats.cps_calculated = constrain(_stats.cps_calculated, CPS_MIN, CPS_MAX);
        
        // Calculate jitter
        float variance = 0.0f;
        for (float p : periods) {
            variance += (p - _stats.mean_period_ms) * (p - _stats.mean_period_ms);
        }
        _stats.period_jitter_ms = sqrt(variance / periods.size());
    }
    
    // Point-by-point statistics
    uint8_t max_point_count = 0;
    for (const auto& wf : _waveforms) {
        max_point_count = max(max_point_count, wf.point_count);
    }
    
    for (uint8_t p = 0; p < max_point_count && p < 20; p++) {
        std::vector<float> point_times, point_amps;
        
        for (const auto& wf : _waveforms) {
            if (p < wf.points.size()) {
                point_times.push_back(wf.points[p].time_ms);
                point_amps.push_back(wf.points[p].amplitude_linear);
            }
        }
        
        if (!point_times.empty()) {
            float mean_time = std::accumulate(point_times.begin(), point_times.end(), 0.0f) / point_times.size();
            float mean_amp = std::accumulate(point_amps.begin(), point_amps.end(), 0.0f) / point_amps.size();
            
            _stats.point_time_means.push_back(mean_time);
            _stats.point_amplitude_means.push_back(mean_amp);
            
            // Calculate std devs
            float var_time = 0.0f, var_amp = 0.0f;
            for (size_t i = 0; i < point_times.size(); i++) {
                var_time += (point_times[i] - mean_time) * (point_times[i] - mean_time);
                var_amp += (point_amps[i] - mean_amp) * (point_amps[i] - mean_amp);
            }
            _stats.point_time_std_devs.push_back(sqrt(var_time / point_times.size()));
            _stats.point_amplitude_stds.push_back(sqrt(var_amp / point_amps.size()));
        }
    }
    
    // Harmonic statistics
    float sum_h2_1 = 0.0f, sum_h3_1 = 0.0f;
    for (const auto& wf : _waveforms) {
        sum_h2_1 += wf.harmonic_ratio_2_1;
        sum_h3_1 += wf.harmonic_ratio_3_1;
    }
    _stats.mean_harmonic_2_1 = sum_h2_1 / _waveforms.size();
    _stats.mean_harmonic_3_1 = sum_h3_1 / _waveforms.size();
    _stats.harmonic_consistency = 100.0f - abs(_stats.mean_harmonic_2_1 - 0.5f) * 100.0f;  // Target 0.5
}

void AdvancedImpactAnalyzer::updateOverallQuality() {
    // Calculate consistency
    if (!_stats.point_time_std_devs.empty()) {
        float total_jitter = std::accumulate(_stats.point_time_std_devs.begin(),
                                             _stats.point_time_std_devs.end(), 0.0f);
        float max_acceptable_jitter = JITTER_MAX_ACCEPTABLE * _stats.point_time_std_devs.size();
        _stats.consistency_percent = 100.0f - (total_jitter / max_acceptable_jitter) * 100.0f;
        _stats.consistency_percent = constrain(_stats.consistency_percent, 0.0f, 100.0f);
    }
    
    // Quality score: 60% consistency, 40% harmonic content
    float harmonic_quality = (1.0f - abs(_stats.mean_harmonic_2_1 - 0.5f)) * 100.0f;
    _stats.quality_score = (uint8_t)((_stats.consistency_percent * 0.6f + harmonic_quality * 0.4f));
    _stats.quality_score = constrain(_stats.quality_score, (uint8_t)0, (uint8_t)100);
}
