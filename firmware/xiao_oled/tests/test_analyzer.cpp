// SYNTHETIC fixtures only. These tests are not microphone/hardware evidence.
#include "../ImpactAnalyzer.h"
#include <algorithm>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <memory>
#include <vector>

using cps::ImpactAnalyzer;
using cps::State;
namespace {
void check(bool condition, const char* message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
bool near(double a, double b, double tolerance) { return std::fabs(a - b) <= tolerance; }
void calibrate(ImpactAnalyzer& a, float dc = 0) {
    a.start(); check(a.state() == State::Calibrating, "start calibration");
    for (uint32_t i = 0; i < cps::SAMPLE_RATE - 1; ++i) a.process(dc);
    check(a.state() == State::Calibrating, "calibration lasts a full second");
    a.process(dc); check(a.state() == State::Recording, "recording after one second");
}
// Bipolar decaying synthetic acoustic impulse. Alternate shape changes frequency.
float pulse(int t, bool alternate = false) {
    return t >= 0 && t < 60 ? static_cast<float>(std::exp(-t / 9.0) *
        std::cos(t * (alternate ? 1.1 : 0.42))) : 0;
}
struct Fixture {
    std::vector<uint64_t> times;
    std::vector<float> amplitudes;
    bool changed = false, clipping = false;
    float dc = 0;
};
Fixture regular(double hz, size_t count = cps::TARGET) {
    Fixture f;
    for (size_t i = 0; i < count; ++i) {
        f.times.push_back(200 + static_cast<uint64_t>(std::llround(i * cps::SAMPLE_RATE / hz)));
        f.amplitudes.push_back(0.35f);
    }
    return f;
}
void feed(ImpactAnalyzer& a, const Fixture& f, size_t blockSize = 1) {
    const uint64_t end = f.times.back() + 300;
    size_t event = 0;
    for (uint64_t block = 0; block < end; block += blockSize) {
        for (uint64_t n = block; n < std::min(end, block + blockSize); ++n) {
            while (event + 1 < f.times.size() && n >= f.times[event + 1]) ++event;
            const int offset = n >= f.times[event] ? static_cast<int>(n - f.times[event]) : -1;
            float x = f.dc + f.amplitudes[event] * pulse(offset, f.changed && event >= 250);
            if (f.clipping) x = std::max(-1.0f, std::min(1.0f, x));
            a.process(x);
        }
    }
}
void complete(const ImpactAnalyzer& a) {
    check(a.state() == State::Complete, "500 impulses complete");
    check(a.count() == 500 && a.progress() == 1, "exact target and progress");
    check(a.waveform(500) == nullptr && a.waveform(size_t(-1)) == nullptr, "safe waveform access");
    check(a.impactSample(500) == 0 && a.amplitude(500) == 0 && a.similarity(500) == 0, "safe scalar access");
    for (size_t i = 0; i < 500; ++i) {
        double mean = 0, peak = 0;
        for (size_t j = 0; j < cps::WAVE_POINTS; ++j) {
            const double x = a.waveform(i)[j];
            check(std::isfinite(x), "finite waveform"); mean += x;
            peak = std::max(peak, std::fabs(x));
        }
        check(near(mean / cps::WAVE_POINTS, 0, 1e-6), "demeaned waveform");
        check(near(peak, 1, 1e-6), "peak normalized waveform");
    }
}
void regularTests() {
    for (double hz : {20.0, 85.0, 200.0}) {
        std::unique_ptr<ImpactAnalyzer> a(new ImpactAnalyzer);
        calibrate(*a); const Fixture f = regular(hz); feed(*a, f); complete(*a);
        check(near(a->result().cps, hz, 0.002), "regular CPS");
        const double exact = double(cps::SAMPLE_RATE) * 499 /
            (a->impactSample(499) - a->impactSample(0));
        check(near(a->result().cps, exact, 0.00001), "499 intervals, not 500");
        check(a->result().shapeSimilarity > 99.99f, "identical shape correlation");
        check(a->result().periodCvPercent < 0.2f, "regular period CV");
        check(!a->result().timingWarning && !a->result().clippingWarning, "regular warning free");
        const cps::Result saved = a->result();
        const uint64_t last = a->impactSample(499);
        for (int i = 0; i < 40000; ++i) a->process(i % 2 ? 1 : -1);
        a->process(std::numeric_limits<float>::quiet_NaN());
        check(a->state() == State::Complete && a->count() == 500 && a->impactSample(499) == last &&
            a->result().cps == saved.cps && a->result().clippedPercent == saved.clippedPercent &&
            a->result().shapeSimilarity == saved.shapeSimilarity, "results frozen after completion");
        std::cout << "PASS regular " << hz << " Hz: cps=" << saved.cps
                  << " shape=" << saved.shapeSimilarity << " periodCV=" << saved.periodCvPercent << "%\n";
    }
}
void timeoutTests() {
    std::unique_ptr<ImpactAnalyzer> a(new ImpactAnalyzer);
    calibrate(*a, 0.25f);
    for (uint32_t i = 0; i < cps::SAMPLE_RATE * 60 - 1; ++i) a->process(0.25f);
    check(a->state() == State::Recording && a->count() == 0, "silence cannot complete");
    a->process(0.25f);
    check(a->state() == State::Failed && a->error()[0], "silence timeout exactly 60 seconds");
    a->start();
    uint32_t rng = 12345;
    for (uint32_t i = 0; i < cps::SAMPLE_RATE * 61; ++i) {
        rng = rng * 1664525u + 1013904223u;
        const float noise = (static_cast<float>(rng >> 8) / 16777216.0f - 0.5f) * 0.008f;
        a->process(noise);
    }
    check(a->state() == State::Failed && a->count() == 0, "stationary calibrated noise cannot complete");
    check(a->result().noiseRms > 0.001f && a->result().threshold > 0.006f, "measured noise threshold");
    std::cout << "PASS silence/DC and deterministic noise: 60 s timeout, zero impacts\n";
}
void shapeAndAmplitudeTests() {
    std::unique_ptr<ImpactAnalyzer> a(new ImpactAnalyzer);
    Fixture f = regular(85); f.changed = true;
    calibrate(*a); feed(*a, f); complete(*a);
    check(a->result().shapeSimilarity < 85, "shape change reduces similarity");
    // Independently verify the aggregate implementation against literal O(N^2*P) pairs.
    double total = 0;
    for (size_t i = 0; i < 500; ++i) {
        double row = 0;
        for (size_t k = 0; k < 500; ++k) {
            if (i == k) continue;
            double dot = 0, aa = 0, bb = 0;
            for (size_t j = 0; j < 128; ++j) {
                double x = a->waveform(i)[j], y = a->waveform(k)[j];
                dot += x * y; aa += x * x; bb += y * y;
            }
            row += dot / std::sqrt(aa * bb);
        }
        row /= 499;
        check(near(a->similarity(i), std::max(0.0, row) * 100, 0.0001), "all-other pairwise similarity");
        total += row;
    }
    check(near(a->result().shapeSimilarity, total / 5, 0.0001), "global exact pairwise mean");
    std::cout << "PASS changed shape: similarity=" << a->result().shapeSimilarity << "% (brute-force verified)\n";
    f = regular(85);
    for (size_t i = 0; i < 500; ++i) f.amplitudes[i] = i % 2 ? 0.6f : 0.2f;
    calibrate(*a); feed(*a, f); complete(*a);
    check(near(a->result().amplitudeCvPercent, 50, 0.01), "audio amplitude CV");
    check(a->result().shapeSimilarity > 99.99f, "shape amplitude invariant");
    std::cout << "PASS amplitude variation: audio amplitudeCV=" << a->result().amplitudeCvPercent << "%\n";
}
void timingAndClippingTests() {
    std::unique_ptr<ImpactAnalyzer> a(new ImpactAnalyzer);
    Fixture f = regular(85);
    for (size_t i = 1; i < 500; ++i) f.times[i] = f.times[i - 1] + (i % 2 ? 310 : 442);
    calibrate(*a); feed(*a, f); complete(*a);
    check(a->result().timingWarning && a->result().periodCvPercent > 10, "jitter warning");
    std::cout << "PASS jitter: periodCV=" << a->result().periodCvPercent << "% warning=1\n";
    f = regular(85);
    for (size_t i = 250; i < 500; ++i) f.times[i] += 376;
    calibrate(*a); feed(*a, f); complete(*a);
    check(a->result().timingWarning && a->result().cps < 85, "missing-pulse interval warning");
    std::cout << "PASS missing pulse: cps=" << a->result().cps << " warning=1\n";
    f = regular(85);
    f.times[250] = f.times[249] + 190;
    calibrate(*a); feed(*a, f); complete(*a);
    check(a->result().timingWarning, "double-pulse interval warning");
    // Resolvable pulses above a 0.01 floor: the HP decay must release the
    // hysteresis before a second onset can exercise refractory rejection.
    a->config.thresholdFloor = 0.01f;
    calibrate(*a);
    for (int i = 0; i < 700; ++i) a->process(0.35f * (pulse(i - 200) + pulse(i - 300)));
    check(a->count() == 1 && a->result().rejected == 1 && a->result().timingWarning,
          "refractory rejects double pulse and flags it");
    a->config.refractoryFraction = 0.5f; calibrate(*a);
    for (int i = 0; i < 700; ++i) a->process(0.35f * (pulse(i - 200) + pulse(i - 300)));
    check(a->count() == 2, "refractory is configurable");
    a->config.refractoryFraction = 0.75f;
    std::cout << "PASS double pulses: interval warning and configurable refractory rejection\n";
    f = regular(85); f.clipping = true;
    std::fill(f.amplitudes.begin(), f.amplitudes.end(), 3.0f);
    calibrate(*a); feed(*a, f); complete(*a);
    check(a->result().clippingWarning && a->result().clippedPercent > 0, "clipping tracked");
    std::cout << "PASS clipping: clippedSamples=" << a->result().clippedPercent << "% warning=1\n";
}
void lifecycleAndBlocks() {
    std::unique_ptr<ImpactAnalyzer> a(new ImpactAnalyzer), b(new ImpactAnalyzer);
    check(a->state() == State::Idle && a->count() == 0 && !a->waveform(0), "default idle safe");
    a->process(1); check(a->state() == State::Idle, "idle ignores samples");
    a->fail(nullptr); check(a->state() == State::Failed && a->error()[0], "external fail");
    calibrate(*a); a->process(std::numeric_limits<float>::infinity());
    check(a->state() == State::Failed, "nonfinite samples fail");
    a->config.maxCps = 0; a->start(); check(a->state() == State::Failed, "invalid config rejected");
    a->config = cps::Config();
    Fixture f = regular(200); f.dc = 0.15f;
    calibrate(*a, f.dc); calibrate(*b, f.dc);
    // Edits during acquisition only apply to the next start().
    a->config.thresholdFloor = 100;
    feed(*a, f, 1); feed(*b, f, 257); complete(*a); complete(*b);
    check(near(a->result().cps, 200, 0.001), "DC blocker and config snapshot");
    check(a->result().cps == b->result().cps && a->result().shapeSimilarity == b->result().shapeSimilarity,
          "block independence results");
    for (size_t i = 0; i < 500; ++i) {
        check(a->impactSample(i) == b->impactSample(i) && a->amplitude(i) == b->amplitude(i), "block independent impacts");
        for (size_t j = 0; j < 128; ++j) check(a->waveform(i)[j] == b->waveform(i)[j], "block independent waveform");
    }
    a->reset(); check(a->state() == State::Idle && a->count() == 0 && a->error()[0] == 0 &&
        a->result().cps == 0 && !a->waveform(0), "reset clears visible results");
    a->config = cps::Config(); calibrate(*a); feed(*a, regular(20)); complete(*a);
    check(near(a->result().cps, 20, 0.001), "restart after completion");
    std::cout << "PASS lifecycle/restart/config/invalid access/nonfinite input/block independence\n";
}
}
int main() {
    std::cout << std::fixed << std::setprecision(4);
    std::cout << "SYNTHETIC NATIVE C++11 TESTS (no hardware claims)\n";
    regularTests(); timeoutTests(); shapeAndAmplitudeTests(); timingAndClippingTests(); lifecycleAndBlocks();
    std::cout << "ALL TESTS PASSED; sizeof(ImpactAnalyzer)=" << sizeof(ImpactAnalyzer) << " bytes\n";
}
