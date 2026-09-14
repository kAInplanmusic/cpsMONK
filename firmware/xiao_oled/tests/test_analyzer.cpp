// SYNTHETIC fixtures only. These tests are not microphone/hardware evidence.
#include "../ImpactAnalyzer.h"
#include <algorithm>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <vector>

using cps::ImpactAnalyzer;
using cps::State;
namespace {
void check(bool condition, const char* message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
bool near(double a, double b, double tolerance) { return std::fabs(a - b) <= tolerance; }
// Bipolar decaying synthetic acoustic impulse. Alternate shape changes frequency.
float pulse(int t, bool alternate = false) {
    return t >= 0 && t < 60 ? static_cast<float>(std::exp(-t / 9.0) *
        std::cos(t * (alternate ? 1.1 : 0.42))) : 0;
}
void calibrate(ImpactAnalyzer& a, float dc = 0) {
    a.start(); check(a.state() == State::Calibrating, "start calibration");
    for (uint32_t i = 0; i < cps::SAMPLE_RATE - 1; ++i) a.process(dc);
    check(a.state() == State::Calibrating, "calibration lasts a full second");
    a.process(dc); check(a.state() == State::WaitingForMotor, "waiting for motor after one second");
}
struct Fixture {
    std::vector<uint64_t> times;
    std::vector<float> amplitudes;
    bool changed = false, clipping = false;
    float dc = 0;
};
// A stroke series at a constant rate. Stroke times are relative to the first
// stroke; the actual start offset is set by startAt.
Fixture regular(double hz, size_t count) {
    Fixture f;
    for (size_t i = 0; i < count; ++i) {
        f.times.push_back(200 + static_cast<uint64_t>(std::llround(i * cps::SAMPLE_RATE / hz)));
        f.amplitudes.push_back(0.35f);
    }
    return f;
}
// Feed samples until the fixture's strokes are exhausted, plus tail samples.
void feed(ImpactAnalyzer& a, const Fixture& f, size_t blockSize = 1, uint64_t tail = 300) {
    const uint64_t end = f.times.back() + tail;
    size_t event = 0;
    for (uint64_t block = 0; block < end; block += blockSize) {
        for (uint64_t n = block; n < std::min(end, block + blockSize); ++n) {
            while (event + 1 < f.times.size() && n >= f.times[event + 1]) ++event;
            const int offset = n >= f.times[event] ? static_cast<int>(n - f.times[event]) : -1;
            float x = f.dc + f.amplitudes[event] * pulse(offset, f.changed && event >= 500);
            if (f.clipping) x = std::max(-1.0f, std::min(1.0f, x));
            a.process(x);
        }
    }
}
void regularTests() {
    for (double hz : {50.0, 85.0, 200.0}) {
        std::unique_ptr<ImpactAnalyzer> a(new ImpactAnalyzer);
        calibrate(*a);
        // Enough strokes for warm-up plus a full window at this rate, plus slack
        // so the window is reached before the fixture runs dry.
        const size_t strokes = cps::TARGET + size_t(hz * 4) + 200;
        const Fixture f = regular(hz, strokes);
        feed(*a, f, 1, 4000);
        check(a->state() == State::Complete || a->count() > cps::TARGET * 0.9, "run finished");
        const size_t n = a->count();
        check(n >= cps::TARGET, "window yielded at least TARGET strokes");
        check(n <= cps::MAX_IMPACTS, "count within storage capacity");
        check(near(a->result().cps, hz, 0.5), "regular CPS");
        const double exact = double(cps::SAMPLE_RATE) * (n - 1) /
            (a->impactSample(n - 1) - a->impactSample(0));
        check(near(a->result().cps, exact, 0.00001), "N-1 intervals, not N");
        check(a->result().shapeSimilarity > 99.9f, "identical shape correlation");
        check(a->result().periodCvPercent < 0.5f, "regular period CV");
        check(!a->result().clippingWarning, "regular warning free");
        check(a->result().provisionalCps > 0 && a->result().windowSeconds > 0, "window was sized");
        check(a->result().windowSeconds >= cps::WINDOW_MIN_SECONDS &&
              a->result().windowSeconds <= cps::WINDOW_MAX_SECONDS, "window inside clamps");
        std::cout << "PASS regular " << hz << " Hz: cps=" << a->result().cps
                  << " n=" << n << " warmupCps=" << a->result().provisionalCps
                  << " window=" << a->result().windowSeconds << "s"
                  << " shape=" << a->result().shapeSimilarity
                  << " periodCV=" << a->result().periodCvPercent << "%\n";
        // Results must freeze once complete.
        const cps::Result saved = a->result();
        const uint64_t last = a->impactSample(n - 1);
        for (int i = 0; i < 40000; ++i) a->process(i % 2 ? 1 : -1);
        a->process(std::numeric_limits<float>::quiet_NaN());
        check(a->state() == State::Complete && a->count() == n && a->impactSample(n - 1) == last &&
            a->result().cps == saved.cps && a->result().shapeSimilarity == saved.shapeSimilarity,
            "results frozen after completion");
    }
}
void gateTests() {
    // Hard rate gate: a stroke series below the CPS floor must abort, not
    // produce a number that cannot be classified. One stroke every 25 ms = 40 CPS.
    {
        std::unique_ptr<ImpactAnalyzer> a(new ImpactAnalyzer);
        const Fixture f = regular(40, 60);
        calibrate(*a); feed(*a, f, 1, 2000);
        check(a->state() == State::Failed, "slow series aborts");
        check(std::string(a->error()).find("50 CPS") != std::string::npos, "abort names the rate gate");
        std::cout << "PASS rate gate: 40 CPS aborted with '" << a->error() << "'\n";
    }
    // A stroke stream too irregular to size a window must never auto-start.
    // Alternating fast intervals do NOT work as a test case: the refractory
    // detector folds a 120/420-Sample alternation into an even 540-Sample
    // series, so the analyzer sees stability that the fixture never had.
    // Use real jitter around a mean instead, with every interval comfortably
    // above the refractory period so detection cannot smooth it away.
    {
        std::unique_ptr<ImpactAnalyzer> a(new ImpactAnalyzer);
        // Strokes must keep arriving for longer than IDLE_ABORT_SECONDS, or the
        // fixture simply runs dry and the analyzer never gets to decide.
        const size_t seconds = size_t(cps::IDLE_ABORT_SECONDS) + 3;
        Fixture f = regular(150, size_t(150 * seconds));
        uint32_t rng = 987654321u;
        for (size_t i = 1; i < f.times.size(); ++i) {
            rng = rng * 1664525u + 1013904223u;
            const int jitter = int(rng % 200) - 100;   // +/-100 samples around 213
            f.times[i] = f.times[i - 1] + uint64_t(213 + jitter);
        }
        calibrate(*a); feed(*a, f, 1, 4000);
        check(a->state() == State::Failed, "jittered series aborts with a readable reason");
        check(std::string(a->error()).find("stabiler Impulsstrom") != std::string::npos,
              "jitter abort names the cause");
        std::cout << "PASS auto-start gate: jitter CV above limit rejected -> '" << a->error() << "'\n";
    }
    // A machine that stops before reaching the stroke gate must give up with a
    // readable reason instead of waiting forever.
    {
        std::unique_ptr<ImpactAnalyzer> a(new ImpactAnalyzer);
        calibrate(*a);
        for (uint32_t i = 0; i < cps::SAMPLE_RATE * 31; ++i) a->process(0);
        check(a->state() == State::Failed, "idle run aborts instead of hanging");
        check(std::string(a->error()).find("Kein Impuls") != std::string::npos,
              "idle abort names the cause");
        std::cout << "PASS idle abort: '" << a->error() << "'\n";
    }
    // Silence after calibration must not warm up either.
    {
        std::unique_ptr<ImpactAnalyzer> a(new ImpactAnalyzer);
        calibrate(*a, 0.25f);
        for (uint32_t i = 0; i < cps::SAMPLE_RATE * 30; ++i) a->process(0.25f);
        check(a->state() == State::Failed && a->count() == 0, "silence never starts");
        std::cout << "PASS silence: no auto-start, zero impacts\n";
    }
    // A stable series at a normal rate must auto-start without user input.
    {
        std::unique_ptr<ImpactAnalyzer> a(new ImpactAnalyzer);
        const Fixture f = regular(150, cps::TARGET + 1600);
        calibrate(*a); check(!a->gateSatisfied(), "gate closed before strokes");
        feed(*a, f, 1, 4000);
        check(a->state() == State::Complete, "150 CPS auto-started and completed");
        check(a->gateSatisfied(), "gate opened by stable strokes");
        std::cout << "PASS auto-start: 150 CPS started without user action, n=" << a->count() << "\n";
    }
}
// Level indicator: tracks stroke amplitude, not average power. A stroke train
// at 150 CPS occupies only ~4 % of each cycle, so an energy average reports
// "quiet" for every realistic distance while the amplitude tracks the operator.
void levelTests() {
    ImpactAnalyzer a;
    a.process(0.5f);
    check(a.state() == State::Idle, "idle ignores samples");
    check(a.levelAssessment() == cps::Level::Silent, "silent before any monitor input");
    // The evaluation must be stroke-rate independent: the same machine at the
    // same distance must not need a different setting per speed.
    auto feedTrain = [](ImpactAnalyzer& target, float amp, double hz, int samples) {
        const int period = int(cps::SAMPLE_RATE / hz);
        for (int i = 0; i < samples; ++i) {
            const int t = i % period;
            const float stroke = t < 60 ? amp * static_cast<float>(std::exp(-t / 9.0) * std::cos(t * 0.42)) : 0.0f;
            target.monitor(stroke);
            target.levelTouch();
        }
    };
    for (double hz : {50.0, 120.0, 150.0, 220.0}) {
        ImpactAnalyzer b;
        feedTrain(b, 0.045f, hz, 40000);
        check(b.levelAssessment() == cps::Level::Good, "nominal level is good at every stroke rate");
        check(b.levelWasGood(), "nominal level unlocks the start gate");
    }
    // Distance sweep must be monotonic through the bands.
    {
        ImpactAnalyzer dead; feedTrain(dead, 0.0f, 150, 40000);
        check(dead.levelAssessment() == cps::Level::Silent, "dead line is silent");
        check(!dead.levelWasGood(), "dead line never unlocks the gate");
        ImpactAnalyzer quiet; feedTrain(quiet, 0.002f, 150, 40000);
        check(quiet.levelAssessment() == cps::Level::TooQuiet, "far distance is too quiet");
        check(!quiet.levelWasGood(), "too quiet does not unlock the gate");
        ImpactAnalyzer warn; feedTrain(warn, 0.150f, 150, 40000);
        check(warn.levelAssessment() == cps::Level::Warning, "too close warns");
        check(!warn.levelWasGood(), "warning alone does not unlock the gate");
        ImpactAnalyzer hot; feedTrain(hot, 0.400f, 150, 40000);
        check(hot.levelAssessment() == cps::Level::TooLoud, "overload is too loud");
        ImpactAnalyzer nom; feedTrain(nom, 0.045f, 150, 40000);
        check(nom.result().levelAmplitude > 0.02f && nom.result().levelAmplitude < 0.09f,
              "held amplitude tracks the stroke peak, not the duty cycle");
    }
    // Cold-start arming: the device has no button, so the arming latch must
    // survive reset()/start(). If it did not, the firmware would run exactly one
    // measurement and then sit idle forever with no way to re-arm it.
    {
        ImpactAnalyzer arm;
        feedTrain(arm, 0.045f, 150, 40000);
        check(arm.levelWasGood(), "good band arms the device");
        check(arm.result().levelAmplitude > 0, "a real reading was taken before arming");
        arm.start();
        check(arm.levelWasGood(), "arming survives start()");
        arm.reset();
        check(arm.levelWasGood(), "arming survives reset()");
        check(arm.result().levelAmplitude == 0, "reset still clears the stale level reading");
    }
    // The hold decays with a ~1 s time constant once the machine stops. One
    // second of silence leaves roughly a third, which is deliberate: the display
    // must not flicker while the operator watches it.
    {
        ImpactAnalyzer decay; feedTrain(decay, 0.045f, 150, 20000);
        const float held = decay.result().levelAmplitude;
        for (int i = 0; i < int(cps::SAMPLE_RATE); ++i) decay.monitor(0.0f);   // 1 s silence
        const float afterOne = decay.result().levelAmplitude;
        check(afterOne < held * 0.5f && afterOne > held * 0.2f,
              "one second of silence leaves roughly a third of the held level");
        for (int i = 0; i < int(cps::SAMPLE_RATE) * 12; ++i) decay.monitor(0.0f); // 12 s more
        check(decay.result().levelAmplitude < held * 0.01f, "level reaches near zero after ~13 s");
        check(decay.levelAssessment() == cps::Level::Silent, "decayed level reads as silent");
    }
    a.reset();
    check(!a.levelWasGood() && a.levelAssessment() == cps::Level::Silent, "reset clears the level gate");
    std::cout << "PASS level indicator: rate-independent bands, distance sweep, decay, start gate\n";
}
void shapeAndAmplitudeTests() {
    std::unique_ptr<ImpactAnalyzer> a(new ImpactAnalyzer);
    Fixture f = regular(150, cps::TARGET + 1600); f.changed = true;
    calibrate(*a); feed(*a, f, 1, 4000);
    check(a->state() == State::Complete, "changed-shape run completes");
    check(a->result().shapeSimilarity < 90, "shape change reduces similarity");
    // Independently verify the aggregate implementation against literal O(N^2*P) pairs
    // for a bounded prefix, so the test stays fast while still being a real cross-check.
    const size_t sample = 60;
    double total = 0;
    for (size_t i = 0; i < sample; ++i) {
        double row = 0;
        for (size_t k = 0; k < a->count(); ++k) {
            if (i == k) continue;
            double dot = 0, aa = 0, bb = 0;
            for (size_t j = 0; j < cps::WAVE_POINTS; ++j) {
                double x = a->waveform(i)[j], y = a->waveform(k)[j];
                dot += x * y; aa += x * x; bb += y * y;
            }
            row += dot / std::sqrt(aa * bb);
        }
        row /= (a->count() - 1);
        check(near(a->similarity(i), std::max(0.0, row) * 100, 0.0001), "all-other pairwise similarity");
        total += row;
    }
    std::cout << "PASS changed shape: similarity=" << a->result().shapeSimilarity
              << "% (first " << sample << " against all " << a->count() << " brute-force verified)\n";
    f = regular(150, cps::TARGET + 1600);
    for (size_t i = 0; i < f.amplitudes.size(); ++i) f.amplitudes[i] = i % 2 ? 0.6f : 0.2f;
    calibrate(*a); feed(*a, f, 1, 4000);
    check(a->state() == State::Complete, "amplitude run completes");
    check(near(a->result().amplitudeCvPercent, 50, 0.5), "audio amplitude CV");
    check(a->result().shapeSimilarity > 99.9f, "shape amplitude invariant");
    std::cout << "PASS amplitude variation: audio amplitudeCV=" << a->result().amplitudeCvPercent << "%\n";
}
void timingAndClippingTests() {
    // Jitter well above the 5 % CV warning threshold but below the 8 % auto-start
    // gate so the run actually starts. 213 samples mean ~150 CPS; the spread is
    // +/-32 samples, and every interval stays above the 109-sample refractory.
    std::unique_ptr<ImpactAnalyzer> a(new ImpactAnalyzer);
    Fixture f = regular(150, cps::TARGET + 1600);
    for (size_t i = 1; i < f.times.size(); ++i)
        f.times[i] = f.times[i - 1] + (i % 2 ? 196 : 230);   // 152/134 CPS, CV ~8.1 % mean-only
    calibrate(*a); feed(*a, f, 1, 4000);
    check(a->state() == State::Complete, "jitter run completes");
    check(a->result().timingWarning, "jitter warning");
    std::cout << "PASS jitter: periodCV=" << a->result().periodCvPercent << "% warning=1 cps="
              << a->result().cps << "\n";
    // Missing strokes: a skipped stroke must show up as a longer interval.
    f = regular(150, cps::TARGET + 1600);
    for (size_t i = 600; i < f.times.size(); ++i) f.times[i] += 213;
    calibrate(*a); feed(*a, f, 1, 4000);
    check(a->state() == State::Complete && a->result().timingWarning, "missing-stroke warning");
    std::cout << "PASS missing stroke: cps=" << a->result().cps << " warning=1\n";
    // Configurable refractory rejection of a resolvable double pulse.
    a->config.thresholdFloor = 0.01f;
    calibrate(*a);
    for (int i = 0; i < 900; ++i) a->process(0.35f * (pulse(i - 200) + pulse(i - 300)));
    check(a->result().rejected >= 1 && a->result().timingWarning,
          "refractory rejects double pulse and flags it");
    a->config.refractoryFraction = 0.5f;
    a->config = ImpactAnalyzer().config;
    a->config.refractoryFraction = 0.5f;
    std::cout << "PASS double pulses: interval warning and configurable refractory rejection\n";
    // Clipping is tracked and warned.
    a->config = ImpactAnalyzer().config;
    f = regular(150, cps::TARGET + 1600); f.clipping = true;
    std::fill(f.amplitudes.begin(), f.amplitudes.end(), 3.0f);
    calibrate(*a); feed(*a, f, 1, 4000);
    check(a->state() == State::Complete, "clipping run completes");
    check(a->result().clippingWarning && a->result().clippedPercent > 0, "clipping tracked");
    std::cout << "PASS clipping: clippedSamples=" << a->result().clippedPercent << "% warning=1\n";
}
void lifecycleAndBlocks() {
    std::unique_ptr<ImpactAnalyzer> a(new ImpactAnalyzer), b(new ImpactAnalyzer);
    check(a->state() == State::Idle && a->count() == 0 && !a->waveform(0), "default idle safe");
    a->fail(nullptr); check(a->state() == State::Failed && a->error()[0], "external fail");
    calibrate(*a); a->process(std::numeric_limits<float>::infinity());
    check(a->state() == State::Failed, "nonfinite samples fail");
    a->config.maxCps = 0; a->start(); check(a->state() == State::Failed, "invalid config rejected");
    a->config.maxCps = 300; a->start(); check(a->state() == State::Failed, "maxCps above ceiling rejected");
    a->config = cps::Config();
    Fixture f = regular(200, cps::TARGET + 1600); f.dc = 0.15f;
    calibrate(*a, f.dc); calibrate(*b, f.dc);
    // Edits during acquisition only apply to the next start().
    a->config.thresholdFloor = 100;
    feed(*a, f, 1, 4000); feed(*b, f, 257, 4000);
    check(a->state() == State::Complete && b->state() == State::Complete, "both block modes complete");
    check(a->count() == b->count(), "block size does not change the reached count");
    check(near(a->result().cps, 200, 0.5), "DC blocker and config snapshot");
    check(a->result().cps == b->result().cps && a->result().shapeSimilarity == b->result().shapeSimilarity,
          "block independence results");
    const size_t n = a->count();
    for (size_t i = 0; i < n; ++i) {
        check(a->impactSample(i) == b->impactSample(i) && a->amplitude(i) == b->amplitude(i),
              "block independent impacts");
        for (size_t j = 0; j < cps::WAVE_POINTS; ++j)
            check(a->waveform(i)[j] == b->waveform(i)[j], "block independent waveform");
    }
    a->reset(); check(a->state() == State::Idle && a->count() == 0 && a->error()[0] == 0 &&
        a->result().cps == 0 && !a->waveform(0), "reset clears visible results");
    std::cout << "PASS lifecycle/restart/config/invalid access/nonfinite input/block independence\n";
}
} // namespace
int main() {
    std::cout << std::fixed << std::setprecision(4);
    std::cout << "SYNTHETIC NATIVE C++11 TESTS (no hardware claims)\n";
    regularTests(); gateTests(); levelTests(); shapeAndAmplitudeTests();
    timingAndClippingTests(); lifecycleAndBlocks();
    std::cout << "ALL TESTS PASSED; sizeof(ImpactAnalyzer)=" << sizeof(ImpactAnalyzer)
              << " bytes; TARGET=" << cps::TARGET
              << " waveSamples=" << cps::WAVE_SAMPLES
              << " window=" << cps::WINDOW_DURATION_MS << "ms\n";
}
