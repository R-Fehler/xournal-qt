#include "Recorder.h"

#include <algorithm>
#include <cmath>

namespace xqt::audio {

Recorder::Recorder(QObject* parent): QObject(parent) {}

Recorder::~Recorder() {
    if (isRecording()) {
        input->stop();
        writer.close();
    }
}

bool Recorder::start(const std::filesystem::path& file) {
    if (isRecording()) {
        stop();
    }
    err.clear();
    input = makeInput();
    if (!input) {
        err = tr("Recording is not available in this version of the app.");
        return false;
    }
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    // The device first: its rate is the file's
    if (!input->start([this](const float* s, size_t n) { take(s, n); })) {
        err = QString::fromStdString(input->error());
        input.reset();
        return false;
    }
    if (!writer.open(file, input->sampleRate())) {
        err = QString::fromStdString(writer.error());
        input->stop();
        input.reset();
        return false;
    }
    input->onError = [this](const std::string& e) { fail(QString::fromStdString(e)); };
    path = file;
    peak = lastLevel = 0;
    levelFrom = positionSent = 0;
    heard = quiet = false;
    st = State::Recording;
    Q_EMIT stateChanged();
    Q_EMIT positionChanged();
    return true;
}

void Recorder::pause() {
    if (st != State::Recording) {
        return;
    }
    input->suspend();
    st = State::Paused;
    lastLevel = 0;
    Q_EMIT levelChanged();
    Q_EMIT stateChanged();
}

void Recorder::resume() {
    if (st != State::Paused) {
        return;
    }
    input->resume();
    st = State::Recording;
    Q_EMIT stateChanged();
}

int64_t Recorder::stop() {
    if (!isRecording()) {
        return 0;
    }
    input->stop();
    input.reset();
    const int64_t ms = positionMs();
    const bool ok = writer.close();
    st = State::Idle;
    lastLevel = 0;
    if (!ok) {
        err = QString::fromStdString(writer.error());
    }
    Q_EMIT levelChanged();
    Q_EMIT stateChanged();
    Q_EMIT finished(QString::fromStdU16String(path.u16string()), ms);
    return ms;
}

int64_t Recorder::positionMs() const {
    const int rate = writer.sampleRate();
    return rate > 0 ? writer.samplesWritten() * 1000 / rate : 0;
}

int Recorder::sampleRate() const { return writer.sampleRate(); }

void Recorder::take(const float* samples, size_t count) {
    if (st != State::Recording || count == 0 || failing) {
        return;  // (paused: what a device still gives is not part of the recording)
    }
    const int64_t before = writer.samplesWritten();
    if (!writer.write(samples, count)) {
        fail(QString::fromStdString(writer.error()));
        return;
    }
    const int rate = writer.sampleRate();
    for (size_t i = 0; i < count; ++i) {
        peak = std::max(peak, std::abs(samples[i]));
    }
    const int64_t now = writer.samplesWritten();
    if (!heard && peak > SILENCE_LEVEL) {
        heard = true;
        if (quiet) {
            quiet = false;
            Q_EMIT silentChanged();
        }
    }
    if (!heard && !quiet && now >= static_cast<int64_t>(SILENCE_MS) * rate / 1000) {
        quiet = true;
        Q_EMIT silentChanged();
    }
    if (now - levelFrom >= rate / 20) {  // (50 ms)
        lastLevel = std::min(peak, 1.f);
        peak = 0;
        levelFrom = now;
        Q_EMIT levelChanged();
    }
    if (now - positionSent >= rate / 10 || before == 0) {
        positionSent = now;
        Q_EMIT positionChanged();
    }
}

void Recorder::fail(const QString& error) {
    if (!isRecording() || failing) {
        return;
    }
    failing = true;  // (later: this may run inside the device's signal, and stop() deletes the device)
    QMetaObject::invokeMethod(
            this,
            [this, error] {
                failing = false;
                if (isRecording()) {
                    stop();
                    err = error;
                    Q_EMIT failed(error);
                }
            },
            Qt::QueuedConnection);
}

}  // namespace xqt::audio
