/*
 * xournal-qt: the microphone and the speaker through Qt Multimedia (AudioDevice.h). Built only with Qt6::Multimedia
 * (XQT_AUDIO, qt/cmake/XqtAudio.cmake).
 *
 * The microphone is asked for mono float samples at its own rate (else mono 16-bit, else what it prefers, mixed down
 * here). The speaker gets the file's rate in mono when it takes it, else its preferred format, with the samples
 * resampled linearly and copied to every channel.
 *
 * @license GNU GPLv2 or later
 */
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSink>
#include <QAudioSource>
#include <QIODevice>
#include <QMediaDevices>

#include "AudioDevice.h"

namespace xqt::audio {

namespace {

/// The first of these formats the device takes (mono float, mono 16-bit at `rate`), else its preferred one.
QAudioFormat formatFor(const QAudioDevice& dev, int rate) {
    QAudioFormat f = dev.preferredFormat();
    if (rate > 0) {
        f.setSampleRate(rate);
    }
    f.setChannelCount(1);
    for (auto sf: {QAudioFormat::Float, QAudioFormat::Int16}) {
        f.setSampleFormat(sf);
        if (dev.isFormatSupported(f)) {
            return f;
        }
    }
    return dev.preferredFormat();
}

/// One sample in the device's format.
void putSample(char* to, const QAudioFormat& f, float v) {
    v = std::clamp(v, -1.f, 1.f);
    switch (f.sampleFormat()) {
        case QAudioFormat::Float:
            std::memcpy(to, &v, sizeof v);
            break;
        case QAudioFormat::Int16: {
            const auto s = static_cast<int16_t>(std::lround(v * 32767.f));
            std::memcpy(to, &s, sizeof s);
            break;
        }
        case QAudioFormat::Int32: {
            const auto s = static_cast<int32_t>(std::llround(static_cast<double>(v) * 2147483647.0));
            std::memcpy(to, &s, sizeof s);
            break;
        }
        case QAudioFormat::UInt8: {
            const auto s = static_cast<uint8_t>(std::lround((v + 1.f) * 127.5f));
            std::memcpy(to, &s, sizeof s);
            break;
        }
        default:
            std::memset(to, 0, static_cast<size_t>(f.bytesPerSample()));
    }
}

class QtInput final: public AudioInput {
public:
    ~QtInput() override { stop(); }

    bool start(Consumer c) override {
        stop();
        const QAudioDevice dev = QMediaDevices::defaultAudioInput();
        if (dev.isNull()) {
            err = "No microphone was found.";
            return false;
        }
        fmt = formatFor(dev, 0);
        source = std::make_unique<QAudioSource>(dev, fmt);
        source->setBufferSize(fmt.bytesForDuration(100'000));  // (100 ms)
        io = source->start();
        if (!io || source->error() != QAudio::NoError) {
            err = "The microphone cannot be opened.";
            source.reset();
            io = nullptr;
            return false;
        }
        consume = std::move(c);
        QObject::connect(io, &QIODevice::readyRead, io, [this] { pull(); });
        QObject::connect(source.get(), &QAudioSource::stateChanged, source.get(), [this](QAudio::State s) {
            if (s == QAudio::StoppedState && source && source->error() != QAudio::NoError) {
                err = "The microphone stopped.";
                if (onError) {
                    onError(err);
                }
            }
        });
        return true;
    }

    void stop() override {
        if (source) {
            source->stop();
            source.reset();
        }
        io = nullptr;
        consume = {};
        rest.clear();
    }
    void suspend() override {
        if (source) {
            source->suspend();
        }
    }
    void resume() override {
        if (source) {
            source->resume();
        }
    }
    int sampleRate() const override { return fmt.sampleRate(); }
    std::string error() const override { return err; }

private:
    void pull() {
        if (!io || !consume) {
            return;
        }
        rest += io->readAll();
        const int bpf = fmt.bytesPerFrame();
        const int bps = fmt.bytesPerSample();
        const int ch = fmt.channelCount();
        if (bpf <= 0 || ch <= 0) {
            return;
        }
        const qsizetype frames = rest.size() / bpf;
        samples.resize(static_cast<size_t>(frames));
        const char* data = rest.constData();
        for (qsizetype i = 0; i < frames; ++i) {
            float sum = 0;
            for (int c = 0; c < ch; ++c) {
                sum += fmt.normalizedSampleValue(data + i * bpf + c * bps);
            }
            samples[static_cast<size_t>(i)] = sum / static_cast<float>(ch);
        }
        rest.remove(0, frames * bpf);
        if (frames > 0) {
            auto c = consume;
            c(samples.data(), samples.size());
        }
    }

    std::unique_ptr<QAudioSource> source;
    QIODevice* io = nullptr;
    QAudioFormat fmt;
    Consumer consume;
    QByteArray rest;
    std::vector<float> samples;
    std::string err;
};

/// What the speaker reads: the producer's samples in the device's format (resampled when its rate differs).
class PullDevice final: public QIODevice {
public:
    PullDevice(AudioOutput::Producer p, int rate, QAudioFormat f): produce(std::move(p)), srcRate(rate), fmt(f) {
        step = static_cast<double>(srcRate) / fmt.sampleRate();
    }
    bool isSequential() const override { return true; }
    qint64 bytesAvailable() const override { return (ended ? 0 : 1 << 16) + QIODevice::bytesAvailable(); }
    bool ended = false;

protected:
    qint64 readData(char* data, qint64 maxlen) override {
        const int bpf = fmt.bytesPerFrame();
        const int bps = fmt.bytesPerSample();
        const int ch = fmt.channelCount();
        if (ended || bpf <= 0) {
            return 0;
        }
        const auto frames = static_cast<size_t>(maxlen / bpf);
        out.resize(frames);
        size_t n = 0;
        if (srcRate == fmt.sampleRate()) {
            n = produce(out.data(), frames);
        } else {
            // Linear: src holds the samples from floor(pos) on
            const auto need = static_cast<size_t>(std::ceil(pos + step * static_cast<double>(frames))) + 2;
            if (src.size() < need && !srcEnded) {
                const size_t have = src.size();
                src.resize(need);
                const size_t got = produce(src.data() + have, need - have);
                src.resize(have + got);
                srcEnded = got < need - have;
            }
            while (n < frames) {
                const auto i = static_cast<size_t>(pos);
                if (i + 1 >= src.size()) {
                    break;
                }
                const auto t = static_cast<float>(pos - static_cast<double>(i));
                out[n++] = src[i] * (1 - t) + src[i + 1] * t;
                pos += step;
            }
            const auto used = std::min(static_cast<size_t>(pos), src.size());
            src.erase(src.begin(), src.begin() + static_cast<long>(used));
            pos -= static_cast<double>(used);
        }
        for (size_t i = 0; i < n; ++i) {
            for (int c = 0; c < ch; ++c) {
                putSample(data + static_cast<qint64>(i) * bpf + c * bps, fmt, out[i]);
            }
        }
        if (n < frames) {
            ended = true;
        }
        return static_cast<qint64>(n) * bpf;
    }
    qint64 writeData(const char*, qint64) override { return -1; }

private:
    AudioOutput::Producer produce;
    int srcRate;
    QAudioFormat fmt;
    double step = 1;
    double pos = 0;
    bool srcEnded = false;
    std::vector<float> src, out;
};

class QtOutput final: public AudioOutput {
public:
    ~QtOutput() override { stop(); }

    bool start(int rate, Producer p) override {
        stop();
        const QAudioDevice dev = QMediaDevices::defaultAudioOutput();
        if (dev.isNull()) {
            err = "No speaker was found.";
            return false;
        }
        const QAudioFormat fmt = formatFor(dev, rate);
        srcRate = rate;
        pull = std::make_unique<PullDevice>(std::move(p), rate, fmt);
        pull->open(QIODevice::ReadOnly);
        sink = std::make_unique<QAudioSink>(dev, fmt);
        sink->setBufferSize(fmt.bytesForDuration(150'000));  // (150 ms: the position stays close to what is heard)
        sink->start(pull.get());
        if (sink->error() != QAudio::NoError) {
            err = "The speaker cannot be opened.";
            stop();
            return false;
        }
        QObject::connect(sink.get(), &QAudioSink::stateChanged, sink.get(), [this](QAudio::State s) {
            if (s == QAudio::StoppedState && sink && sink->error() != QAudio::NoError &&
                sink->error() != QAudio::UnderrunError) {
                err = "The speaker stopped.";
                if (onError) {
                    onError(err);
                }
            }
        });
        return true;
    }
    void stop() override {
        if (sink) {
            sink->stop();
            sink.reset();
        }
        pull.reset();
    }
    void suspend() override {
        if (sink) {
            sink->suspend();
        }
    }
    void resume() override {
        if (sink) {
            sink->resume();
        }
    }
    int64_t playedSamples() const override {
        return sink ? static_cast<int64_t>(sink->processedUSecs()) * srcRate / 1'000'000 : 0;
    }
    bool finished() const override { return pull && pull->ended && sink && sink->state() == QAudio::IdleState; }
    std::string error() const override { return err; }

private:
    std::unique_ptr<PullDevice> pull;
    std::unique_ptr<QAudioSink> sink;
    int srcRate = 48000;
    std::string err;
};

}  // namespace

std::unique_ptr<AudioInput> makeQtInput() { return std::make_unique<QtInput>(); }

std::unique_ptr<AudioOutput> makeQtOutput() { return std::make_unique<QtOutput>(); }

}  // namespace xqt::audio
