#include "FakeAudio.h"

#include <cmath>
#include <numbers>

#include <QTimer>

namespace xqt::audio::fake {

namespace {
Config& cfg() {
    static Config c;
    return c;
}

class FakeInput;
class FakeOutput;
FakeInput* runningInput = nullptr;
FakeOutput* runningOutput = nullptr;

constexpr int TICK_MS = 10;

class FakeInput final: public AudioInput {
public:
    ~FakeInput() override { stop(); }

    bool start(Consumer c) override {
        stop();
        if (cfg().failInput) {
            err = "No microphone (fake)";
            return false;
        }
        consume = std::move(c);
        rate = cfg().inputRate;
        phase = 0;
        suspended = false;
        runningInput = this;
        if (cfg().timers) {
            timer = std::make_unique<QTimer>();
            timer->setInterval(TICK_MS);
            QObject::connect(timer.get(), &QTimer::timeout, [this] { give(TICK_MS); });
            timer->start();
        }
        return true;
    }
    void stop() override {
        timer.reset();
        consume = {};
        if (runningInput == this) {
            runningInput = nullptr;
        }
    }
    void suspend() override { suspended = true; }
    void resume() override { suspended = false; }
    int sampleRate() const override { return rate; }
    std::string error() const override { return err; }

    bool give(int ms) {
        if (!consume || suspended) {
            return false;
        }
        std::vector<float> v(static_cast<size_t>(rate) * static_cast<size_t>(ms) / 1000);
        const double step = 2 * std::numbers::pi * cfg().toneHz / rate;
        for (auto& x: v) {
            x = cfg().amplitude * static_cast<float>(std::sin(phase));
            phase += step;
        }
        phase = std::fmod(phase, 2 * std::numbers::pi);
        auto c = consume;  // (the consumer may stop the device)
        c(v.data(), v.size());
        return true;
    }

private:
    Consumer consume;
    std::unique_ptr<QTimer> timer;
    int rate = 48000;
    double phase = 0;
    bool suspended = false;
    std::string err;
};

class FakeOutput final: public AudioOutput {
public:
    ~FakeOutput() override { stop(); }

    bool start(int r, Producer p) override {
        stop();
        if (cfg().failOutput) {
            err = "No speaker (fake)";
            return false;
        }
        rate = r;
        produce = std::move(p);
        played = 0;
        ended = false;
        suspended = false;
        runningOutput = this;
        if (cfg().timers) {
            timer = std::make_unique<QTimer>();
            timer->setInterval(TICK_MS);
            QObject::connect(timer.get(), &QTimer::timeout, [this] { take(TICK_MS); });
            timer->start();
        }
        return true;
    }
    void stop() override {
        timer.reset();
        produce = {};
        if (runningOutput == this) {
            runningOutput = nullptr;
        }
    }
    void suspend() override { suspended = true; }
    void resume() override { suspended = false; }
    int64_t playedSamples() const override { return played; }
    bool finished() const override { return ended; }
    std::string error() const override { return err; }

    std::vector<float> take(int ms) {
        if (!produce || suspended || ended) {
            return {};
        }
        std::vector<float> v(static_cast<size_t>(rate) * static_cast<size_t>(ms) / 1000);
        const size_t n = produce(v.data(), v.size());
        v.resize(n);
        played += static_cast<int64_t>(n);
        if (n < static_cast<size_t>(rate) * static_cast<size_t>(ms) / 1000) {
            ended = true;
        }
        return v;
    }

private:
    Producer produce;
    std::unique_ptr<QTimer> timer;
    int rate = 48000;
    int64_t played = 0;
    bool ended = false;
    bool suspended = false;
    std::string err;
};
}  // namespace

Config& config() { return cfg(); }

void reset() { cfg() = Config(); }

bool record(int ms) { return runningInput && runningInput->give(ms); }

std::vector<float> play(int ms) { return runningOutput ? runningOutput->take(ms) : std::vector<float>(); }

bool inputRunning() { return runningInput != nullptr; }

void failInputNow() {
    if (runningInput && runningInput->onError) {
        runningInput->onError("The microphone was unplugged (fake)");
    }
}

void failOutputNow() {
    if (runningOutput && runningOutput->onError) {
        runningOutput->onError("The speaker was unplugged (fake)");
    }
}

bool outputRunning() { return runningOutput != nullptr; }

std::unique_ptr<AudioInput> makeInput() { return std::make_unique<FakeInput>(); }

std::unique_ptr<AudioOutput> makeOutput() { return std::make_unique<FakeOutput>(); }

}  // namespace xqt::audio::fake
