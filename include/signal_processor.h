#ifndef SIGNAL_PROCESSOR_H
#define SIGNAL_PROCESSOR_H

#include <Arduino.h>
#include <vector>
#include "config.h"

/**
 * @class SignalProcessor
 * @brief Handles signal processing: FFT, peak detection, frequency analysis
 * 
 * Features:
 * - Real-time FFT computation using CMSIS-DSP
 * - Peak detection in time and frequency domains
 * - Bandpass filtering
 * - Harmonic analysis
 * - RMS and spectral statistics
 */
class SignalProcessor {
public:
    // Structure for impact event data
    struct ImpactEvent {
        unsigned long timestamp;      // Time of impact (microseconds)
        float peakAmplitude;          // Peak amplitude (linear)
        float peakAmplitudeDB;        // Peak amplitude (dB)
        float fundamentalFreq;        // Fundamental frequency (Hz)
        float totalEnergy;            // Total energy in signal
        float noiseFloor;             // Noise floor level (dB)
        std::vector<float> harmonics; // Harmonic frequencies and magnitudes
    };
    
    // Structure for spectrum data (for display)
    struct SpectrumData {
        std::vector<float> magnitude;     // FFT magnitude in dB
        std::vector<float> frequency;     // Frequency axis (Hz)
        float peakMagnitude;              // Peak magnitude (dB)
        float peakFrequency;              // Frequency at peak (Hz)
        float spectralCentroid;           // Weighted center of spectrum
    };
    
    SignalProcessor();
    ~SignalProcessor();
    
    /**
     * Initialize signal processor
     * Allocates FFT buffers and prepares DSP accelerators
     * @return true if successful
     */
    bool init();
    
    /**
     * Process audio samples and update internal state
     * @param samples Audio samples (int16_t)
     * @param count Number of samples
     */
    void processSamples(const int16_t* samples, size_t count);
    
    /**
     * Perform FFT on current buffer
     * @return true if FFT was computed
     */
    bool computeFFT();
    
    /**
     * Detect peaks in current signal
     * @param minDistance Minimum distance between peaks (samples)
     * @param threshold Threshold (dB) relative to maximum
     * @return true if new peaks were detected
     */
    bool detectPeaks(size_t minDistance = PEAK_MIN_DISTANCE, 
                     float threshold = PEAK_THRESHOLD_DB);
    
    /**
     * Get detected impact events
     * @return Vector of impact events
     */
    const std::vector<ImpactEvent>& getImpactEvents() const { 
        return _impactEvents; 
    }
    
    /**
     * Clear impact event history
     */
    void clearImpactEvents() { 
        _impactEvents.clear(); 
    }
    
    /**
     * Get spectrum data for display
     * @return Current spectrum data
     */
    const SpectrumData& getSpectrumData() const { 
        return _spectrumData; 
    }
    
    /**
     * Get current time-domain waveform (for oscilloscope display)
     * @param buffer Output buffer
     * @param samples Number of samples to return
     * @return Number of samples returned
     */
    size_t getWaveformData(int16_t* buffer, size_t samples) const;
    
    /**
     * Calculate RMS energy
     * @return RMS value
     */
    float getRMSEnergy() const { return _rmsEnergy; }
    
    /**
     * Calculate peak-to-peak amplitude
     * @return Peak-to-peak value
     */
    float getPeakToPeak() const { return _peakToPeak; }
    
    /**
     * Get noise floor level (dB)
     * @return Noise floor
     */
    float getNoiseFloor() const { return _noiseFloor; }
    
    /**
     * Calculate number of impacts per second (CPS)
     * @param windowSec Time window in seconds to analyze
     * @return CPS value
     */
    float calculateCPS(float windowSec = 2.0f);
    
    /**
     * Get statistics of impact intervals
     * @return Array [mean, stddev, min, max] in milliseconds
     */
    std::vector<float> getImpactIntervalStats() const;
    
    /**
     * Update bandpass filter coefficients
     * @param freqLow Low frequency cutoff (Hz)
     * @param freqHigh High frequency cutoff (Hz)
     * @param Q Quality factor
     */
    void updateBandpassFilter(float freqLow, float freqHigh, float Q = 1.0f);
    
    /**
     * Apply bandpass filter to samples
     * @param samples Input samples
     * @param output Output samples
     * @param count Number of samples
     */
    void applyBandpassFilter(const int16_t* samples, int16_t* output, size_t count);
    
    /**
     * Analyze harmonic content
     * @return Vector of harmonic magnitudes (dB)
     */
    std::vector<float> analyzeHarmonics();
    
    /**
     * Get quality score (0-100)
     * Based on consistency and spectral analysis
     * @return Quality score
     */
    uint8_t getQualityScore();

private:
    // FFT buffers
    float* _fftInput;
    float* _fftOutput;
    float* _window;
    
    // Time-domain buffer (ring buffer for waveform display)
    int16_t* _waveformBuffer;
    size_t _waveformWritePos;
    
    // Frequency-domain data
    SpectrumData _spectrumData;
    
    // Impact events
    std::vector<ImpactEvent> _impactEvents;
    unsigned long _lastImpactTime;
    
    // Signal statistics
    float _rmsEnergy;
    float _peakToPeak;
    float _noiseFloor;
    
    // Filter state (IIR bandpass)
    struct FilterState {
        float x1, x2;      // Input history
        float y1, y2;      // Output history
        float b0, b1, b2;  // Numerator coefficients
        float a1, a2;      // Denominator coefficients
    } _filterState;
    
    // Quality metrics
    std::vector<float> _impactIntervals;  // milliseconds
    
    /**
     * Apply Hann window to buffer
     * @param buffer Input buffer
     * @param count Buffer size
     */
    void applyHannWindow(float* buffer, size_t count);
    
    /**
     * Find peaks in magnitude spectrum
     * @param magnitudes Magnitude values
     * @param threshold Threshold for peak detection
     * @return Vector of peak indices
     */
    std::vector<size_t> findSpectrumPeaks(const std::vector<float>& magnitudes, 
                                           float threshold);
    
    /**
     * Convert FFT bin index to frequency
     * @param binIndex Bin index
     * @return Frequency in Hz
     */
    float binIndexToFrequency(size_t binIndex) const;
    
    /**
     * Update quality score based on current state
     */
public:
    void updateQualityScore();
    
private:
    uint8_t _qualityScore;
};

#endif // SIGNAL_PROCESSOR_H
