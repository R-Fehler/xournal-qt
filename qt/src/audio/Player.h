/*
 * xournal-qt: playing a recording (qt/docs/audio.md, "Playing").
 *
 * Decodes the Ogg Vorbis file while the speaker asks for samples. The position is where the reader started plus what
 * the device has played, so it follows what is heard (not what is buffered). Seeking restarts the device, so nothing
 * buffered from before the seek is heard after it.
 *
 * Lives on the UI thread, as its device (AudioDevice.h).
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <filesystem>
#include <memory>

#include <QObject>
#include <QString>
#include <QTimer>

#include "AudioDevice.h"
#include "OggVorbis.h"

namespace xqt::audio {

class Player: public QObject {
    Q_OBJECT
public:
    enum class State { Stopped, Playing, Paused };

    explicit Player(QObject* parent = nullptr);
    ~Player() override;

    /// Plays `file` from `fromMs` (clamped to the recording). False: see error().
    bool play(const std::filesystem::path& file, int64_t fromMs = 0);
    void pause();
    void resume();
    void stop();
    /// Goes to `ms` in the recording (also while paused).
    void seek(int64_t ms);

    State state() const { return st; }
    const std::filesystem::path& file() const { return path; }
    int64_t positionMs() const;
    int64_t durationMs() const { return duration; }
    const QString& error() const { return err; }

Q_SIGNALS:
    void stateChanged();
    /// About every 50 ms while playing, and on a seek.
    void positionChanged();
    /// The recording played to its end (the player is stopped then).
    void finished();
    void failed(const QString& error);

private:
    bool startOutput();
    void tick();

    std::unique_ptr<AudioOutput> output;
    VorbisReader reader;
    std::filesystem::path path;
    State st = State::Stopped;
    QString err;
    int64_t base = 0;  ///< the sample the output started at
    int64_t duration = 0;
    QTimer timer;
};

}  // namespace xqt::audio
