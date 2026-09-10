#include "sd_logger.h"
#include <esp_log.h>
#include <time.h>

static const char* TAG = "SDLogger";

SDLogger::SDLogger()
    : _initialized(false) {
}

SDLogger::~SDLogger() {
}

bool SDLogger::init() {
    if (!_sd.begin(SD_CS_PIN, SD_SCK_MHZ(25))) {
        ESP_LOGE(TAG, "Failed to initialize SD card");
        _initialized = false;
        return false;
    }
    
    _initialized = true;
    ESP_LOGI(TAG, "SD card initialized successfully");
    ESP_LOGI(TAG, "Free space: %.1f MB", getFreeSpace() / 1024.0 / 1024.0);
    return true;
}

String SDLogger::logMeasurement(const AdvancedImpactAnalyzer& analyzer, LogFormat format) {
    if (!_initialized) {
        ESP_LOGW(TAG, "SD card not initialized");
        return "";
    }
    
    String filename = generateFilename(format);
    FsFile file = _sd.open(filename, FILE_WRITE);
    
    if (!file) {
        ESP_LOGE(TAG, "Failed to open file: %s", filename.c_str());
        return "";
    }
    
    switch (format) {
        case FORMAT_CSV:
            writeCSVHeader(file);
            {
                const auto& waveforms = analyzer.getWaveforms();
                for (size_t i = 0; i < waveforms.size(); i++) {
                    logWaveform(waveforms[i], i + 1, file);
                }
            }
            file.println();
            file.println("=== STATISTICS SUMMARY ===");
            logStatistics(analyzer.getStats(), file);
            break;
            
        case FORMAT_JSON:
            writeJSONHeader(file);
            file.println("  \"measurements\": [");
            {
                const auto& waveforms = analyzer.getWaveforms();
                for (size_t i = 0; i < waveforms.size(); i++) {
                    file.print("    {\n");
                    logWaveform(waveforms[i], i + 1, file);
                    file.print("    }");
                    if (i < waveforms.size() - 1) file.println(",");
                    else file.println();
                }
            }
            file.println("  ]");
            file.println("}");
            break;
            
        default:
            break;
    }
    
    file.close();
    ESP_LOGI(TAG, "Logged measurement to: %s", filename.c_str());
    return filename;
}

void SDLogger::logWaveform(const AdvancedImpactAnalyzer::ImpactWaveform& waveform,
                           uint32_t measurement_number, FsFile& file) {
    file.printf("Measurement %d, Time %lu ms, Points %d\n",
               measurement_number, waveform.timestamp_ms, waveform.point_count);
    file.println("#, Time_ms, Amplitude, dB, Slope_deg, Energy, Freq_Hz");
    
    for (size_t i = 0; i < waveform.points.size(); i++) {
        writePointAsCSV(waveform.points[i], file);
    }
    
    file.printf("Summary, %.2f, %.2f, %.2f, %d, %.2f, %.4f\n",
               waveform.total_energy,
               waveform.peak_amplitude,
               waveform.peak_amplitude_db,
               waveform.primary_peak_idx,
               waveform.harmonic_ratio_2_1,
               waveform.decay_rate);
    file.println();
}

void SDLogger::logStatistics(const AdvancedImpactAnalyzer::AdvancedStats& stats, FsFile& file) {
    file.printf("CPS, %.2f\n", stats.cps_calculated);
    file.printf("Quality Score, %d%%\n", stats.quality_score);
    file.printf("Consistency, %.1f%%\n", stats.consistency_percent);
    file.printf("Samples Collected, %d\n", stats.samples_collected);
    file.printf("Mean Period, %.2f ms\n", stats.mean_period_ms);
    file.printf("Period Jitter, %.3f ms\n", stats.period_jitter_ms);
    file.println();
    file.printf("Mean Harmonic 2/1, %.4f\n", stats.mean_harmonic_2_1);
    file.printf("Mean Harmonic 3/1, %.4f\n", stats.mean_harmonic_3_1);
    file.printf("Mean Decay Rate, %.4f\n", stats.mean_decay_rate);
    file.printf("Mean Q-Factor, %.2f\n", stats.mean_quality_factor);
}

std::vector<String> SDLogger::listLogs() {
    std::vector<String> files;
    
    if (!_initialized) return files;
    
    FsFile root = _sd.open("/");
    if (!root) return files;
    
    FsFile entry = root.openNextFile();
    while (entry) {
        if (!entry.isDirectory()) {
            char namebuf[64];
            entry.getName(namebuf, sizeof(namebuf));
            String name = String(namebuf);
            if (name.endsWith(".csv") || name.endsWith(".json")) {
                files.push_back(name);
            }
        }
        entry = root.openNextFile();
    }
    
    return files;
}

void SDLogger::cleanupOldLogs(uint8_t keep_count) {
    std::vector<String> files = listLogs();
    
    if (files.size() > keep_count) {
        // Simple deletion of oldest files (by name/timestamp)
        for (size_t i = keep_count; i < files.size(); i++) {
            _sd.remove(files[i].c_str());
            ESP_LOGI(TAG, "Deleted old log: %s", files[i].c_str());
        }
    }
}

uint64_t SDLogger::getFreeSpace() const {
    if (!_initialized) return 0;
    
    uint64_t free_clusters = _sd.freeClusterCount();
    return free_clusters * _sd.bytesPerCluster();
}

String SDLogger::generateFilename(LogFormat format) {
    time_t now = time(nullptr);
    struct tm* timeinfo = localtime(&now);
    
    char buffer[32];
    const char* ext = (format == FORMAT_CSV) ? ".csv" : (format == FORMAT_JSON) ? ".json" : ".bin";
    
    snprintf(buffer, sizeof(buffer), "/measure_%04d%02d%02d_%02d%02d%02d%s",
             timeinfo->tm_year + 1900,
             timeinfo->tm_mon + 1,
             timeinfo->tm_mday,
             timeinfo->tm_hour,
             timeinfo->tm_min,
             timeinfo->tm_sec,
             ext);
    
    return String(buffer);
}

void SDLogger::writeCSVHeader(FsFile& file) {
    file.println("ESP32-S3 Coil Machine Analyzer - Measurement Log");
    file.printf("Timestamp, %lu\n", millis());
    file.println();
}

void SDLogger::writeJSONHeader(FsFile& file) {
    file.println("{");
    file.printf("  \"timestamp\": %lu,\n", millis());
    file.println("  \"device\": \"ESP32-S3 Coil Analyzer\",");
    file.println("  \"version\": \"3.0\",");
}

void SDLogger::writePointAsCSV(const AdvancedImpactAnalyzer::MeasurementPoint& point, FsFile& file) {
    file.printf("%d, %.2f, %.4f, %.2f, %.1f, %.4f, %.1f\n",
               point.point_number,
               point.time_ms,
               point.amplitude_linear,
               point.amplitude_db,
               point.slope_angle_deg,
               point.segment_energy,
               point.estimated_freq_hz);
}

void SDLogger::writePointAsJSON(const AdvancedImpactAnalyzer::MeasurementPoint& point, FsFile& file, bool last) {
    file.printf("      {\"number\": %d, \"time_ms\": %.2f, \"amplitude\": %.4f, \"db\": %.2f}%s\n",
               point.point_number,
               point.time_ms,
               point.amplitude_linear,
               point.amplitude_db,
               last ? "" : ",");
}
