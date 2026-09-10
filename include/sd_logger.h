#ifndef SD_LOGGER_H
#define SD_LOGGER_H

#include <Arduino.h>

#include <SdFat.h>
#include "config.h"
#include "advanced_impact_analyzer.h"

/**
 * @class SDLogger
 * @brief SD card logging for measurement data export
 * 
 * Features:
 * - CSV export of all measurement points
 * - Waveform data storage
 * - Statistics summary
 * - Multiple file format support
 */
class SDLogger {
public:
    enum LogFormat {
        FORMAT_CSV,        // CSV text format
        FORMAT_JSON,       // JSON format
        FORMAT_BINARY      // Binary compressed format
    };
    
    SDLogger();
    ~SDLogger();
    
    /**
     * Initialize SD card
     * @return true if successful
     */
    bool init();
    
    /**
     * Check if SD card is available
     */
    bool isAvailable() const { return _initialized; }
    
    /**
     * Log measurement data to SD card
     * @param analyzer Advanced analyzer with measurement data
     * @param format Output format (CSV, JSON, Binary)
     * @return filename if successful, empty string on error
     */
    String logMeasurement(const AdvancedImpactAnalyzer& analyzer, LogFormat format = FORMAT_CSV);
    
    /**
     * Log single waveform
     * @param waveform Waveform to log
     * @param measurement_number Which measurement this is (1-100)
     * @param file Output file stream
     */
    void logWaveform(const AdvancedImpactAnalyzer::ImpactWaveform& waveform,
                     uint32_t measurement_number, FsFile& file);
    
    /**
     * Log statistics summary
     * @param stats Statistics to log
     * @param file Output file stream
     */
    void logStatistics(const AdvancedImpactAnalyzer::AdvancedStats& stats, FsFile& file);
    
    /**
     * Get list of log files
     * @return Vector of filenames
     */
    std::vector<String> listLogs();
    
    /**
     * Delete old log files (keep last N)
     * @param keep_count How many recent logs to keep
     */
    void cleanupOldLogs(uint8_t keep_count = 10);
    
    /**
     * Get free space on SD card
     * @return Free space in bytes
     */
    uint64_t getFreeSpace() const;
    
private:
    mutable SdFat _sd;  // mutable: freeClusterCount() ist nicht const, logisch aber read-only
    bool _initialized;
    static const uint8_t SD_CS_PIN = 10;  // Configure per your setup
    
    /**
     * Generate filename with timestamp
     * @return Filename string
     */
    String generateFilename(LogFormat format);
    
    /**
     * Write CSV header
     */
    void writeCSVHeader(FsFile& file);
    
    /**
     * Write JSON header
     */
    void writeJSONHeader(FsFile& file);
    
    /**
     * Write measurement point as CSV row
     */
    void writePointAsCSV(const AdvancedImpactAnalyzer::MeasurementPoint& point, FsFile& file);
    
    /**
     * Write measurement point as JSON
     */
    void writePointAsJSON(const AdvancedImpactAnalyzer::MeasurementPoint& point, FsFile& file, bool last = false);
};

#endif // SD_LOGGER_H
