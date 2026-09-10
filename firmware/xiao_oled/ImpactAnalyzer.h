#ifndef CPS_IMPACT_ANALYZER_H
#define CPS_IMPACT_ANALYZER_H

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace cps {
static const uint32_t SAMPLE_RATE = 32000;
static const size_t TARGET = 500;
static const size_t WAVE_POINTS = 128;
enum class State { Idle, Calibrating, Recording, Complete, Failed };
struct Config {
    float minCps = 20.0f;
    float maxCps = 200.0f;
    float thresholdMultiplier = 6.0f;
    float thresholdFloor = 0.001f;
    // Minimum separation = this fraction of SAMPLE_RATE/maxCps.
    float refractoryFraction = 0.75f;
};
struct Result {
    float cps = 0, shapeSimilarity = 0, periodCvPercent = 0;
    float amplitudeCvPercent = 0, clippedPercent = 0, noiseRms = 0, threshold = 0;
    uint32_t rejected = 0;
    bool timingWarning = false, clippingWarning = false;
};

// Single-owner streaming analyzer; synchronize externally with any UI readers.
// No heap allocation, Arduino dependency, or per-sample storage allocation.
// start(): exactly 1 s machine-OFF calibration, then <=60 s recording.
// Samples are normalized ADC audio; missing input samples cannot be detected.
// impactSample() is zero-based relative to start(), INCLUDING calibration.
// 97 raw samples over 3 ms, centered on the strongest absolute HP peak,
// are linearly resampled to 128, demeaned and normalized to unit absolute peak.
// The peak is at coordinate 63.5. Peak-centering is the alignment method;
// no additional lag search, time warping or polarity inversion is performed.
// Similarity is 100 * mean signed cosine correlation against ALL OTHER 499
// waveforms, clipped to [0,100] for display. shapeSimilarity clips only the
// global mean. An aggregate unit-vector sum computes exact all-pairs means
// in O(TARGET*WAVE_POINTS), not a reference-template approximation.
// Amplitudes and their CV describe filtered AUDIO peaks, not mechanical force.
// Timing warnings indicate irregular/out-of-range acoustic intervals, not a
// proof of missed/double mechanical strokes. Periodic interference may look
// like impacts: this algorithm cannot authenticate the acoustic source.
class ImpactAnalyzer {
public:
    Config config;
    ImpactAnalyzer() { reset(); }
    void reset() {
        state_ = State::Idle; count_ = 0; clock_ = 0; recordingSamples_ = 0;
        clipped_ = 0; calibrationEnergy_ = 0; calibrationCount_ = 0;
        previousInput_ = previousHp_ = envelope_ = 0;
        armed_ = true; lowSamples_ = 0; pending_ = false;
        onset_ = peakTime_ = lastAccepted_ = 0; peak_ = 0;
        refractory_ = 0; result_ = Result(); error_[0] = '\0';
        std::memset(ring_, 0, sizeof(ring_));
    }
    void start() {
        reset(); active_ = config;
        if (!validConfig()) { fail("Invalid analyzer configuration"); return; }
        refractory_ = static_cast<uint32_t>(SAMPLE_RATE / active_.maxCps *
                                            active_.refractoryFraction);
        state_ = State::Calibrating;
    }
    void fail(const char* reason) {
        state_ = State::Failed;
        const char* text = reason ? reason : "Acquisition failed";
        std::strncpy(error_, text, sizeof(error_) - 1);
        error_[sizeof(error_) - 1] = '\0';
    }
    const char* error() const { return error_; }
    State state() const { return state_; }
    size_t count() const { return count_; }
    float progress() const { return static_cast<float>(count_) / TARGET; }
    const Result& result() const { return result_; }
    const float* waveform(size_t i) const { return i < count_ ? waves_[i] : nullptr; }
    uint64_t impactSample(size_t i) const { return i < count_ ? times_[i] : 0; }
    float similarity(size_t i) const { return i < count_ ? similarities_[i] : 0; }
    float amplitude(size_t i) const { return i < count_ ? amplitudes_[i] : 0; }

    void process(float input) {
        if (state_ != State::Calibrating && state_ != State::Recording) return;
        if (!std::isfinite(input)) { fail("Non-finite audio sample"); return; }
        // One-pole DC blocker, approximately 103 Hz corner at 32 kHz.
        const float hp = input - previousInput_ + 0.98f * previousHp_;
        previousInput_ = input; previousHp_ = hp;
        ring_[clock_ % RING_SIZE] = hp;
        const uint64_t now = clock_++;
        if (state_ == State::Calibrating) {
            // Ignore first 10 ms to avoid calibrating the DC-blocker startup.
            if (now >= 320) { calibrationEnergy_ += double(hp) * hp; ++calibrationCount_; }
            if (clock_ == SAMPLE_RATE) {
                result_.noiseRms = static_cast<float>(std::sqrt(calibrationEnergy_ / calibrationCount_));
                result_.threshold = maximum(active_.thresholdFloor,
                    result_.noiseRms * active_.thresholdMultiplier);
                state_ = State::Recording; envelope_ = 0; armed_ = true;
            }
            return;
        }
        ++recordingSamples_;
        if (std::fabs(input) >= 0.999f) ++clipped_;
        result_.clippedPercent = 100.0f * static_cast<float>(clipped_) / recordingSamples_;
        result_.clippingWarning = clipped_ != 0;
        const float magnitude = std::fabs(hp);
        envelope_ = maximum(magnitude, envelope_ * 0.90f);
        if (envelope_ < result_.threshold * 0.4f) {
            if (++lowSamples_ >= 4) { armed_ = true; lowSamples_ = 4; }
        } else lowSamples_ = 0;

        if (pending_) {
            // Search the first millisecond only: don't merge neighboring 200 Hz impacts.
            if (now - onset_ <= 32 && magnitude > peak_) {
                peak_ = magnitude; peakTime_ = now;
            }
            if (now - onset_ >= 32 && now >= peakTime_ + HALF_WINDOW) {
                saveImpact(); pending_ = false;
                if (count_ == TARGET) { finish(); return; }
            }
        } else if (armed_ && envelope_ >= result_.threshold) {
            armed_ = false;
            if (count_ && now - lastAccepted_ < refractory_) {
                ++result_.rejected; result_.timingWarning = true;
            } else {
                pending_ = true; onset_ = peakTime_ = now; peak_ = magnitude;
            }
        }
        if (recordingSamples_ >= uint64_t(SAMPLE_RATE) * 60)
            fail("Recording timeout: fewer than 500 impacts in 60 s");
    }

private:
    enum { RING_SIZE = 256, HALF_WINDOW = 48 };
    State state_;
    Config active_;
    Result result_;
    size_t count_;
    uint64_t clock_, recordingSamples_, clipped_, onset_, peakTime_, lastAccepted_;
    uint32_t calibrationCount_, refractory_, lowSamples_;
    double calibrationEnergy_;
    float previousInput_, previousHp_, envelope_, peak_;
    bool armed_, pending_;
    char error_[96];
    float ring_[RING_SIZE];
    float waves_[TARGET][WAVE_POINTS];
    uint64_t times_[TARGET];
    float amplitudes_[TARGET], similarities_[TARGET];

    static float maximum(float a, float b) { return a > b ? a : b; }
    static float percentCorrelation(double correlation) {
        return static_cast<float>(100 * (correlation < 0 ? 0 : correlation > 1 ? 1 : correlation));
    }
    bool validConfig() const {
        return std::isfinite(active_.minCps) && std::isfinite(active_.maxCps) &&
            std::isfinite(active_.thresholdFloor) && std::isfinite(active_.thresholdMultiplier) &&
            std::isfinite(active_.refractoryFraction) && active_.minCps > 0 &&
            active_.maxCps >= active_.minCps && active_.maxCps <= 200 &&
            active_.thresholdFloor > 0 && active_.thresholdMultiplier >= 3 &&
            active_.refractoryFraction >= 0.4f && active_.refractoryFraction <= 1;
    }
    void saveImpact() {
        times_[count_] = peakTime_; amplitudes_[count_] = peak_;
        similarities_[count_] = 0; lastAccepted_ = peakTime_;
        double mean = 0;
        for (size_t j = 0; j < WAVE_POINTS; ++j) {
            const double pos = double(j) * (2 * HALF_WINDOW) / (WAVE_POINTS - 1);
            const uint32_t base = static_cast<uint32_t>(pos);
            const float fraction = static_cast<float>(pos - base);
            const uint64_t first = peakTime_ - HALF_WINDOW + base;
            const float a = ring_[first % RING_SIZE];
            // Do not read the unwritten sample just beyond the window endpoint.
            const float b = base == 2 * HALF_WINDOW ? a : ring_[(first + 1) % RING_SIZE];
            waves_[count_][j] = a + fraction * (b - a); mean += waves_[count_][j];
        }
        mean /= WAVE_POINTS;
        float scale = 0;
        for (size_t j = 0; j < WAVE_POINTS; ++j) {
            waves_[count_][j] -= static_cast<float>(mean);
            scale = maximum(scale, std::fabs(waves_[count_][j]));
        }
        if (scale > 0)
            for (size_t j = 0; j < WAVE_POINTS; ++j) waves_[count_][j] /= scale;
        ++count_;
    }
    static float cv(double sum, double squareSum, size_t n) {
        const double mean = sum / n;
        const double variance = squareSum / n - mean * mean;
        return mean > 0 ? static_cast<float>(100 * std::sqrt(variance > 0 ? variance : 0) / mean) : 0;
    }
    void finish() {
        double amplitudeSum = 0, amplitudeSquares = 0;
        double periodSum = 0, periodSquares = 0;
        double vectorSum[WAVE_POINTS] = {};
        double inverseNorms[TARGET];
        for (size_t i = 0; i < TARGET; ++i) {
            amplitudeSum += amplitudes_[i];
            amplitudeSquares += double(amplitudes_[i]) * amplitudes_[i];
            if (i) {
                const double period = static_cast<double>(times_[i] - times_[i - 1]);
                periodSum += period; periodSquares += period * period;
                if (period < SAMPLE_RATE / active_.maxCps * 0.95 ||
                    period > SAMPLE_RATE / active_.minCps * 1.05) result_.timingWarning = true;
            }
            double norm = 0;
            for (size_t j = 0; j < WAVE_POINTS; ++j) norm += double(waves_[i][j]) * waves_[i][j];
            inverseNorms[i] = norm > 0 ? 1 / std::sqrt(norm) : 0;
            for (size_t j = 0; j < WAVE_POINTS; ++j) vectorSum[j] += waves_[i][j] * inverseNorms[i];
        }
        double correlationSum = 0;
        for (size_t i = 0; i < TARGET; ++i) {
            double dot = 0;
            for (size_t j = 0; j < WAVE_POINTS; ++j) dot += waves_[i][j] * inverseNorms[i] * vectorSum[j];
            const double correlation = (dot - (inverseNorms[i] > 0 ? 1 : 0)) / (TARGET - 1);
            similarities_[i] = percentCorrelation(correlation); correlationSum += correlation;
            if (i && std::fabs(double(times_[i] - times_[i - 1]) / (periodSum / (TARGET - 1)) - 1) > 0.15)
                result_.timingWarning = true;
        }
        result_.shapeSimilarity = percentCorrelation(correlationSum / TARGET);
        result_.cps = static_cast<float>(double(SAMPLE_RATE) * (TARGET - 1) /
                                         static_cast<double>(times_[TARGET - 1] - times_[0]));
        result_.periodCvPercent = cv(periodSum, periodSquares, TARGET - 1);
        result_.amplitudeCvPercent = cv(amplitudeSum, amplitudeSquares, TARGET);
        if (result_.periodCvPercent > 5) result_.timingWarning = true;
        state_ = State::Complete;
    }
};
} // namespace cps
#endif
