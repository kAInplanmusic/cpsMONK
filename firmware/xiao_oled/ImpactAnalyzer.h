#ifndef CPS_IMPACT_ANALYZER_H
#define CPS_IMPACT_ANALYZER_H

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace cps {
static const uint32_t SAMPLE_RATE = 32000;
// Measurement specification: strokes to collect in one run.
static const size_t TARGET = 1000;
// Storage capacity. Must stay above the largest count a run can reach when the
// window overshoots TARGET, otherwise the ring indexing would run past the end.
static const size_t MAX_IMPACTS = 1400;
static const size_t WAVE_POINTS = 128;

// Waveform window around the strongest peak. 6 ms spans one full mechanical
// cycle at 170 CPS (5.9 ms measured on hardware), so the impact point, the
// reversing point and the cycle start all fall inside the compared shape.
// HALF_WINDOW counts native samples at SAMPLE_RATE: 96 samples = 3 ms = 6 ms total.
enum { HALF_WINDOW = 96 };
// Ring must outlast the impulse detection window plus the waveform window.
enum { RING_SIZE = 512 };
// Gate/warm-up timestamp ring: 2 s at up to 220 CPS is 440 strokes, and the
// warm-up only needs the most recent window, so 256 entries are ample.
static const size_t PRE_RING = 256;
// Native samples across the waveform window: 2 * HALF_WINDOW + 1.
static const size_t WAVE_SAMPLES = 2 * HALF_WINDOW + 1;
static const float WINDOW_DURATION_MS = 2.0f * HALF_WINDOW * 1000.0f / SAMPLE_RATE;

// Warm-up: short stable-stroke phase used only to estimate the provisional
// CPS that sizes the measurement window. Kept short because every warm-up
// second is measurement time not spent on the actual 1000 strokes.
static const float WARMUP_SECONDS = 2.0f;
static const size_t WARMUP_MIN_IMPACTS = 40;
// Measurement window: TARGET strokes at the provisional rate, plus 5 % so a
// small estimation error still lands above TARGET rather than short of it.
static const float WINDOW_MARGIN = 1.05f;
static const float WINDOW_MIN_SECONDS = 5.0f;
static const float WINDOW_MAX_SECONDS = 20.0f;
// Hard gate: below this the run aborts instead of producing an unclassifiable
// number. The user's slowest machines sit around 120 CPS, so 50 is a
// plausibility bound, not a working range limit.
static const float CPS_GATE = 50.0f;
// Auto-start gate: strokes and interval stability required before warming up.
static const size_t AUTO_START_MIN_IMPACTS = 4;
static const float AUTO_START_MAX_CV_PERCENT = 8.0f;
// Abandon a run that never gets going: machine switched off (or too quiet to
// trigger) while waiting for strokes or during warm-up leaves the analyzer in
// a state nobody can interpret, so it must give up with a readable reason.
static const float IDLE_ABORT_SECONDS = 30.0f;
// Peak-hold decay per sample, applied multiplicatively. At 32 kHz a factor of
// 0.99997 is a ~1.0 s time constant: fast enough to follow the operator moving
// the microphone, slow enough to bridge the gaps between strokes at 50-220 CPS.
static const float LEVEL_DECAY = 0.99997f;
// Below this amplitude the input is numerical noise on a floating/dead data
// line rather than room sound. INMP441 self-noise is far above it.
static const float LEVEL_SILENT_FLOOR = 1e-5f;

enum class State { Idle, Calibrating, WaitingForMotor, Warmup, Recording, Complete, Failed };
// Level-indicator band assessment, reported while idle so the operator can set
// the distance before a run exists.
enum class Level { Silent, TooQuiet, Good, Warning, TooLoud };

struct Config {
    float minCps = CPS_GATE;
    float maxCps = 220.0f;
    float thresholdMultiplier = 6.0f;
    float thresholdFloor = 0.001f;
    // Minimum separation = this fraction of SAMPLE_RATE/maxCps.
    float refractoryFraction = 0.75f;
    // Level-indicator bands, as RMS of the normalized raw input.
    // Calibrated against the measured monitor response: a stroke series that
    // drives the detector sits around 0.03-0.06, clipping territory is above
    // ~0.25. Values are relative and machine dependent by nature; they are
    // configuration, not constants, because microphone distance and machine
    // loudness both move them.
    float levelLow = 0.015f;
    float levelHigh = 0.12f;
    float levelLoud = 0.25f;
    // Automatic start once a stable stroke stream is present. Off means the
    // operator triggers every run by hand.
    bool autoStart = true;
};
struct Result {
    float cps = 0, shapeSimilarity = 0, periodCvPercent = 0;
    float amplitudeCvPercent = 0, clippedPercent = 0, noiseRms = 0, threshold = 0;
    // Provisional rate from the warm-up phase and the window it produced.
    float provisionalCps = 0, windowSeconds = 0;
    // Level indicator: held stroke amplitude (normalized input) and its dB value.
    // Deliberately NOT an RMS: see monitor().
    float levelAmplitude = 0, levelDb = 0;
    uint32_t rejected = 0;
    bool timingWarning = false, clippingWarning = false;
    // True when the window ended before TARGET strokes were reached.
    bool windowTruncated = false;
};

// Single-owner streaming analyzer; synchronize externally with any UI readers.
// No heap allocation, Arduino dependency, or per-sample storage allocation.
//
// Run sequence:
//   start()        -> Calibrating (exactly 1 s machine-OFF room noise)
//   gate satisfied -> Warmup (WARMUP_SECONDS of stable strokes)
//   warm-up done   -> Recording (window sized from provisional CPS)
//   window end     -> Complete
// The warm-up phase is deliberately NOT part of the measured window: motor
// spin-up is irregular and would bias both CPS and shape statistics.
//
// Samples are normalized ADC audio; missing input samples cannot be detected.
// impactSample() is zero-based relative to start(), INCLUDING calibration.
// WAVE_SAMPLES native samples over 6 ms, centered on the strongest absolute HP
// peak, are linearly resampled to WAVE_POINTS, demeaned and normalized to unit
// absolute peak. The peak is at coordinate 63.5. Peak-centering is the
// alignment method; no additional lag search, time warping or polarity
// inversion is performed.
// Similarity is 100 * mean signed cosine correlation against ALL OTHER
// waveforms, clipped to [0,100] for display. shapeSimilarity clips only the
// global mean. An aggregate unit-vector sum computes exact all-pairs means
// in O(N*WAVE_POINTS), not a reference-template approximation.
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
        refractory_ = 0; gateImpacts_ = 0; warmupStart_ = 0; windowSamples_ = 0;
        windowSeconds_ = 0; gateOpen_ = false;
        preHead_ = warmupStrokes_ = 0;
        levelHold_ = 0; levelGoodSeen_ = false;
        result_ = Result(); error_[0] = '\0';
        std::memset(ring_, 0, sizeof(ring_));
        std::memset(preTimes_, 0, sizeof(preTimes_));
    }
    void start() {
        reset(); active_ = config;
        if (!validConfig()) { fail("Invalid analyzer configuration"); return; }
        refractory_ = static_cast<uint32_t>(SAMPLE_RATE / active_.maxCps *
                                            active_.refractoryFraction);
        windowSamples_ = static_cast<uint64_t>(SAMPLE_RATE * WINDOW_MAX_SECONDS);
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
    size_t target() const { return TARGET; }
    float progress() const {
        return count_ >= TARGET ? 1.0f : static_cast<float>(count_) / TARGET;
    }
    const Result& result() const { return result_; }
    const float* waveform(size_t i) const { return i < count_ ? waves_[i] : nullptr; }
    uint64_t impactSample(size_t i) const { return i < count_ ? times_[i] : 0; }
    float similarity(size_t i) const { return i < count_ ? similarities_[i] : 0; }
    float amplitude(size_t i) const { return i < count_ ? amplitudes_[i] : 0; }
    // Number of strokes recognized since start(), including warm-up. Used for
    // the OLED/web "waiting for motor" and warm-up progress display.
    uint32_t seen() const { return gateImpacts_; }

    // Level indicator for the idle phase: keeps operator feedback live without
    // a running measurement. Never used as a measurement value.
    //
    // Tracks the STROKE AMPLITUDE, not the average power. Measured on synthetic
    // stroke trains at 150 CPS: a 160 ms RMS stays near 0.005 even when a single
    // stroke peaks at 0.4, because the impact occupies only ~4 % of each cycle.
    // An energy average therefore reports "quiet" for every realistic distance,
    // while the peak-hold below tracks exactly what changes when the microphone
    // is moved. The hold decays with a ~500 ms time constant so the reading
    // follows the operator and does not freeze on a single loud event.
    void monitor(float input) {
        const float magnitude = std::fabs(input);
        // Multiplicative decay, NOT a fixed subtraction. A constant decrement
        // eats small signals entirely between strokes (at 150 CPS there are 213
        // idle samples per cycle) while barely touching loud ones, so a quiet
        // stroke train would read as silence and a loud one as normal. The
        // exponential form gives every amplitude the same time constant.
        levelHold_ *= LEVEL_DECAY;
        if (magnitude > levelHold_) levelHold_ = magnitude;
        result_.levelAmplitude = levelHold_;
        // Amplitude relative to full scale, reported in dB for the UI.
        result_.levelDb = levelHold_ > 1e-9f ? 20.0f * std::log10(levelHold_) : -180.0f;
    }
    Level levelAssessment() const {
        // A dead data line shows only numerical noise; INMP441 self-noise sits
        // orders of magnitude above that, so a floor separates "no signal" from
        // "signal, but quiet" without knowing the absolute calibration.
        if (levelHold_ < LEVEL_SILENT_FLOOR) return Level::Silent;
        if (levelHold_ < active_.levelLow) return Level::TooQuiet;
        if (levelHold_ <= active_.levelHigh) return Level::Good;
        if (levelHold_ <= active_.levelLoud) return Level::Warning;
        return Level::TooLoud;
    }
    // True once the level indicator sat in the good band. The UI locks the
    // start button on this, so a run cannot begin from a badly placed mic.
    bool levelWasGood() const { return levelGoodSeen_; }
    void levelTouch() { if (levelAssessment() == Level::Good) levelGoodSeen_ = true; }
    bool gateSatisfied() const { return gateOpen_; }

    void process(float input) {
        if (state_ != State::Calibrating && state_ != State::Warmup &&
            state_ != State::WaitingForMotor && state_ != State::Recording) return;
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
                result_.noiseRms = calibrationCount_ ?
                    static_cast<float>(std::sqrt(calibrationEnergy_ / calibrationCount_)) : 0.0f;
                result_.threshold = maximum(active_.thresholdFloor,
                    result_.noiseRms * active_.thresholdMultiplier);
                state_ = State::WaitingForMotor; envelope_ = 0; armed_ = true;
                lastAccepted_ = 0; gateStart_ = now;
            }
            return;
        }
        if (state_ == State::Recording) ++recordingSamples_;
        if (std::fabs(input) >= 0.999f) ++clipped_;
        if (recordingSamples_) {
            result_.clippedPercent = 100.0f * static_cast<float>(clipped_) /
                                     static_cast<float>(recordingSamples_);
            result_.clippingWarning = clipped_ != 0;
        }
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
                // Gate and warm-up strokes only need their timing for the rate
                // and stability estimate; storing their waveforms would burn the
                // waveform store on data that is never evaluated.
                if (state_ == State::Recording) {
                    saveImpact();
                    if (count_ == TARGET) { pending_ = false; finish(); return; }
                    if (count_ >= MAX_IMPACTS) { pending_ = false; finish(); return; }
                } else {
                    ++gateImpacts_;
                    lastAccepted_ = peakTime_;
                    preTimes_[preHead_++ % PRE_RING] = peakTime_;
                }
                pending_ = false;
            }
        } else if (armed_ && envelope_ >= result_.threshold) {
            armed_ = false;
            if (gateImpacts_ && now - lastAccepted_ < refractory_) {
                ++result_.rejected; result_.timingWarning = true;
            } else {
                pending_ = true; onset_ = peakTime_ = now; peak_ = magnitude;
            }
        }
        advance(now);
    }

private:
    State state_;
    Config active_;
    Result result_;
    size_t count_;
    uint64_t clock_, recordingSamples_, clipped_, onset_, peakTime_, lastAccepted_;
    uint64_t warmupStart_, windowSamples_, gateStart_;
    uint32_t calibrationCount_, refractory_, lowSamples_, gateImpacts_;
    uint32_t preHead_ = 0, warmupStrokes_ = 0;
    double calibrationEnergy_;
    float previousInput_, previousHp_, envelope_, peak_;
    float levelHold_ = 0;
    float windowSeconds_;
    bool armed_, pending_, gateOpen_ = false, levelGoodSeen_ = false;
    char error_[96];
    float ring_[RING_SIZE];
    // Gate/warm-up stroke timestamps only; enough to estimate rate and CV.
    uint64_t preTimes_[PRE_RING];
    float waves_[MAX_IMPACTS][WAVE_POINTS];
    uint64_t times_[MAX_IMPACTS];
    float amplitudes_[MAX_IMPACTS], similarities_[MAX_IMPACTS];

    static float maximum(float a, float b) { return a > b ? a : b; }
    static float percentCorrelation(double correlation) {
        return static_cast<float>(100 * (correlation < 0 ? 0 : correlation > 1 ? 1 : correlation));
    }
    bool validConfig() const {
        return std::isfinite(active_.minCps) && std::isfinite(active_.maxCps) &&
            std::isfinite(active_.thresholdFloor) && std::isfinite(active_.thresholdMultiplier) &&
            std::isfinite(active_.refractoryFraction) && active_.minCps > 0 &&
            active_.maxCps >= active_.minCps && active_.maxCps <= 220 &&
            active_.thresholdFloor > 0 && active_.thresholdMultiplier >= 3 &&
            active_.refractoryFraction >= 0.4f && active_.refractoryFraction <= 1 &&
            std::isfinite(active_.levelLow) && std::isfinite(active_.levelHigh) &&
            std::isfinite(active_.levelLoud) && active_.levelLow > 0 &&
            active_.levelHigh > active_.levelLow && active_.levelLoud >= active_.levelHigh &&
            active_.levelLoud < 1;
    }

    // Measured CPS over the last n gate/warm-up strokes, or 0 while fewer than
    // two exist. Reads the small pre-measurement ring, not the waveform store.
    float rateOfLast(size_t n) const {
        if (n < 2) return 0;
        const size_t have = gateImpacts_ < PRE_RING ? gateImpacts_ : PRE_RING;
        if (n > have) n = have;
        if (n < 2) return 0;
        const uint64_t newest = preTimes_[(preHead_ + PRE_RING - 1) % PRE_RING];
        const uint64_t oldest = preTimes_[(preHead_ + PRE_RING - n) % PRE_RING];
        const double span = static_cast<double>(newest - oldest);
        return span > 0 ? static_cast<float>(double(SAMPLE_RATE) * (n - 1) / span) : 0;
    }
    // Interval CV over the last n gate/warm-up strokes, in percent.
    float cvOfLast(size_t n) const {
        const size_t have = gateImpacts_ < PRE_RING ? gateImpacts_ : PRE_RING;
        if (n > have) n = have;
        if (n < 3) return 100;
        double sum = 0, squares = 0;
        uint64_t previous = 0;
        for (size_t k = 0; k < n; ++k) {
            const uint64_t t = preTimes_[(preHead_ + PRE_RING - n + k) % PRE_RING];
            if (k) {
                const double period = static_cast<double>(t - previous);
                sum += period; squares += period * period;
            }
            previous = t;
        }
        const double mean = sum / (n - 1);
        const double variance = squares / (n - 1) - mean * mean;
        return mean > 0 ? static_cast<float>(100 * std::sqrt(variance > 0 ? variance : 0) / mean) : 100;
    }

    // State advance after every processed sample: runs the auto-start gate,
    // the warm-up estimate and the measurement window.
    void advance(uint64_t now) {
        if (state_ == State::WaitingForMotor) {
            // Nothing arriving at all: the machine is off or the mic is deaf.
            if (now - lastAccepted_ > uint64_t(IDLE_ABORT_SECONDS * SAMPLE_RATE) &&
                count_ < AUTO_START_MIN_IMPACTS) {
                fail("Kein Impuls erkannt: Motor aus oder Pegel zu niedrig");
                return;
            }
            if (gateImpacts_ < AUTO_START_MIN_IMPACTS) return;
            // Stable stroke stream? Rate gate is hard: slower machines abort
            // rather than produce a number that cannot be classified.
            const float rate = rateOfLast(PRE_RING);
            if (rate > 0 && rate < CPS_GATE) {
                fail("Impulsrate unter 50 CPS: Messung abgebrochen");
                return;
            }
            if (cvOfLast(PRE_RING) >= AUTO_START_MAX_CV_PERCENT) {
                // Strokes keep arriving but never steadily enough to size a
                // window. Bound the wait instead of filling memory with data
                // that will never be evaluated.
                if (now - gateStart_ > uint64_t(IDLE_ABORT_SECONDS * SAMPLE_RATE))
                    fail("Kein stabiler Impulsstrom: Abstand/Winkel oder Schwellwert pruefen");
                return;
            }
            // Auto-start may be switched off; the gate then stays closed and the
            // run waits for an explicit start. Kept as a configuration because a
            // manual trigger is sometimes wanted for reproducible series.
            if (!active_.autoStart) return;
            gateOpen_ = true;
            state_ = State::Warmup;
            warmupStart_ = now;
            warmupStrokes_ = gateImpacts_;
            result_.provisionalCps = rate;
            return;
        }
        if (state_ == State::Warmup) {
            // Warm-up stalls if strokes stop: same reasoning as above.
            if (now - lastAccepted_ > uint64_t(IDLE_ABORT_SECONDS * SAMPLE_RATE)) {
                fail("Impulse waehrend der Vorlaufphase ausgeblieben");
                return;
            }
            if (now - warmupStart_ < uint64_t(SAMPLE_RATE) * WARMUP_SECONDS &&
                gateImpacts_ - warmupStrokes_ < WARMUP_MIN_IMPACTS) return;
            const float rate = rateOfLast(PRE_RING);
            if (rate < CPS_GATE) {
                fail("Impulsrate unter 50 CPS: Messung abgebrochen");
                return;
            }
            float seconds = static_cast<float>(TARGET) / rate * WINDOW_MARGIN;
            if (seconds < WINDOW_MIN_SECONDS) seconds = WINDOW_MIN_SECONDS;
            if (seconds > WINDOW_MAX_SECONDS) seconds = WINDOW_MAX_SECONDS;
            result_.provisionalCps = rate;
            windowSeconds_ = seconds;
            result_.windowSeconds = seconds;
            windowSamples_ = static_cast<uint64_t>(seconds * SAMPLE_RATE);
            count_ = 0;              // warm-up strokes are not measurement data
            recordingSamples_ = 0; clipped_ = 0;
            result_.clippedPercent = 0; result_.clippingWarning = false;
            state_ = State::Recording;
            return;
        }
        if (state_ == State::Recording && recordingSamples_ >= windowSamples_) {
            if (!count_) { fail("Keine Impulse im Messfenster"); return; }
            result_.windowTruncated = count_ < TARGET;
            finish();
        }
    }
    void saveImpact() {
        times_[count_] = peakTime_; amplitudes_[count_] = peak_;
        similarities_[count_] = 0; lastAccepted_ = peakTime_;
        ++gateImpacts_;
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
        const size_t n = count_;
        if (n < 2) { fail("Zu wenige Impulse fuer die Auswertung"); return; }
        double amplitudeSum = 0, amplitudeSquares = 0;
        double periodSum = 0, periodSquares = 0;
        double vectorSum[WAVE_POINTS] = {};
        // 1000 inverses fit the stack; keep it below ~8 KB on a 4 KB-task safe path.
        double inverseNorms[MAX_IMPACTS];
        for (size_t i = 0; i < n; ++i) {
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
        const double meanPeriod = periodSum / (n - 1);
        for (size_t i = 0; i < n; ++i) {
            double dot = 0;
            for (size_t j = 0; j < WAVE_POINTS; ++j) dot += waves_[i][j] * inverseNorms[i] * vectorSum[j];
            const double correlation = (dot - (inverseNorms[i] > 0 ? 1 : 0)) / (n - 1);
            similarities_[i] = percentCorrelation(correlation); correlationSum += correlation;
            if (i && meanPeriod > 0 &&
                std::fabs(double(times_[i] - times_[i - 1]) / meanPeriod - 1) > 0.15)
                result_.timingWarning = true;
        }
        result_.shapeSimilarity = percentCorrelation(correlationSum / n);
        result_.cps = static_cast<float>(double(SAMPLE_RATE) * (n - 1) /
                                         static_cast<double>(times_[n - 1] - times_[0]));
        result_.periodCvPercent = cv(periodSum, periodSquares, n - 1);
        result_.amplitudeCvPercent = cv(amplitudeSum, amplitudeSquares, n);
        if (result_.periodCvPercent > 5) result_.timingWarning = true;
        if (result_.windowSeconds <= 0) result_.windowSeconds = WINDOW_MAX_SECONDS;
        if (result_.clippedPercent == 0 && clipped_) { // warm-up clipping must not vanish
            const uint64_t denom = recordingSamples_ ? recordingSamples_ : 1;
            result_.clippedPercent = 100.0f * static_cast<float>(clipped_) / static_cast<float>(denom);
        }
        state_ = State::Complete;
    }
};
} // namespace cps
#endif
