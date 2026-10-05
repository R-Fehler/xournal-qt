#include "Player.h"

#include <algorithm>

namespace xqt::audio {

Player::Player(QObject* parent): QObject(parent) {
    timer.setInterval(50);
    connect(&timer, &QTimer::timeout, this, &Player::tick);
}

Player::~Player() {
    if (output) {
        output->stop();
    }
}

bool Player::play(const std::filesystem::path& file, int64_t fromMs) {
    stop();
    err.clear();
    if (!reader.open(file)) {
        err = tr("The recording cannot be played: %1").arg(QString::fromStdString(reader.error()));
        return false;
    }
    path = file;
    duration = std::max<int64_t>(0, reader.durationMs());
    reader.seekMs(std::clamp<int64_t>(fromMs, 0, duration));
    output = makeOutput();
    if (!output) {
        err = tr("Playing is not available in this version of the app.");
        reader.close();
        return false;
    }
    if (!startOutput()) {
        reader.close();
        output.reset();
        return false;
    }
    st = State::Playing;
    timer.start();
    Q_EMIT stateChanged();
    Q_EMIT positionChanged();
    return true;
}

bool Player::startOutput() {
    output->stop();
    base = reader.position();
    if (!output->start(reader.sampleRate(), [this](float* out, size_t n) { return reader.read(out, n); })) {
        err = QString::fromStdString(output->error());
        return false;
    }
    output->onError = [this](const std::string& e) {
        err = QString::fromStdString(e);
        QMetaObject::invokeMethod(
                this,
                [this] {
                    stop();
                    Q_EMIT failed(err);
                },
                Qt::QueuedConnection);
    };
    return true;
}

void Player::pause() {
    if (st != State::Playing) {
        return;
    }
    output->suspend();
    st = State::Paused;
    timer.stop();
    Q_EMIT stateChanged();
    Q_EMIT positionChanged();
}

void Player::resume() {
    if (st != State::Paused) {
        return;
    }
    output->resume();
    st = State::Playing;
    timer.start();
    Q_EMIT stateChanged();
}

void Player::stop() {
    timer.stop();
    if (output) {
        output->stop();
        output.reset();
    }
    reader.close();
    if (st != State::Stopped) {
        st = State::Stopped;
        Q_EMIT stateChanged();
    }
}

void Player::seek(int64_t ms) {
    if (st == State::Stopped) {
        return;
    }
    reader.seekMs(std::clamp<int64_t>(ms, 0, duration));
    if (!startOutput()) {
        stop();
        Q_EMIT failed(err);
        return;
    }
    if (st == State::Paused) {
        output->suspend();
    }
    Q_EMIT positionChanged();
}

int64_t Player::positionMs() const {
    if (st == State::Stopped || !output || reader.sampleRate() <= 0) {
        return 0;
    }
    return std::min(duration, (base + output->playedSamples()) * 1000 / reader.sampleRate());
}

void Player::tick() {
    if (st != State::Playing) {
        return;
    }
    if (output->finished()) {
        stop();
        Q_EMIT positionChanged();
        Q_EMIT finished();
        return;
    }
    Q_EMIT positionChanged();
}

}  // namespace xqt::audio
