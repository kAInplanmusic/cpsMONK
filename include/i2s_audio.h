#ifndef I2S_AUDIO_H
#define I2S_AUDIO_H

#include <Arduino.h>
#include <driver/i2s.h>
#include "config.h"

/**
 * @class I2SAudio
 * @brief Handles I2S audio input from microphone with DMA buffering
 * 
 * Features:
 * - Configurable sample rate and bit width
 * - DMA-based continuous sampling
 * - Ring buffer for audio data
 * - Automatic gain control (AGC)
 */
class I2SAudio {
public:
    I2SAudio();
    ~I2SAudio();
    
    /**
     * Initialize I2S peripheral with microphone
     * @return true if successful
     */
    bool init();
    
    /**
     * Start I2S sampling
     * @return true if successful
     */
    bool start();
    
    /**
     * Stop I2S sampling
     * @return true if successful
     */
    bool stop();
    
    /**
     * Read available samples from ring buffer
     * @param buffer Destination buffer
     * @param samples Number of samples to read
     * @return Number of samples actually read
     */
    size_t readSamples(int16_t* buffer, size_t samples);
    
    /**
     * Get current audio level in dB
     * @return Audio level (dB relative to reference)
     */
    float getAudioLevelDB();
    
    /**
     * Get number of samples available in buffer
     * @return Number of available samples
     */
    size_t getAvailableSamples();
    
    /**
     * Calibrate microphone reference level
     * Uses current audio level as reference (0dB)
     */
    void calibrateReference();
    
    /**
     * Set microphone gain
     * @param gainDB Gain in dB (-20 to +40)
     */
    void setGain(float gainDB);
    
    /**
     * Get microphone gain
     * @return Current gain in dB
     */
    float getGain() const { return _gainDB; }
    
    /**
     * Enable/disable automatic gain control
     * @param enable true to enable AGC
     */
    void setAGC(bool enable);
    
    /**
     * Get peak-to-peak amplitude of current audio
     * @return Amplitude (0-32767 for 16-bit)
     */
    uint16_t getPeakAmplitude();

private:
    static const size_t _AUDIO_RINGBUFFER_SIZE = RINGBUFFER_SIZE;  // aus config.h (Member nicht wie Makro benennen)
    static const size_t DMA_BUFF_LEN = I2S_DMA_BUFF_LEN;
    static const size_t DMA_BUFF_COUNT = I2S_DMA_BUFF_COUNT;
    
    // Ring buffer for audio samples
    int16_t* _ringBuffer;
    volatile size_t _writePos;
    volatile size_t _readPos;
    
    // Calibration
    float _referenceLevel;
    float _gainDB;
    bool _agcEnabled;
    
    // Statistics
    uint16_t _peakAmplitude;
    unsigned long _lastPeakUpdate;
    
    /**
     * Calculate RMS (Root Mean Square) level
     * @param buffer Audio samples
     * @param samples Number of samples
     * @return RMS value
     */
    float calculateRMS(const int16_t* buffer, size_t samples);
    
    /**
     * Convert linear amplitude to dB
     * @param amplitude Linear amplitude value
     * @return Value in dB
     */
    float amplitudeToDBFS(float amplitude);
};

#endif // I2S_AUDIO_H
