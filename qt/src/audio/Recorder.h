/*
 * xournal-qt: recording from the microphone into an Ogg Vorbis file (qt/docs/features/audio.md, "Recording").
 *
 * The time of a recording is its count of samples: positionMs() is how much sound the file holds now, so a stroke
 * stamped with it (upstream's `ts`, in milliseconds from the start of the recording) is heard at the right moment,
 * even after a pause (paused time is not in the file) or when the device delivers late.
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

#include "AudioDevice.h"
#include "OggVorbis.h"

namespace xqt::audio {

class Recorder: public QObject {
    Q_OBJECT
public:
    enum class State { Idle, Recording, Paused };

    /// Below this level for the first SILENCE_MS of a recording: silent() (a muted or wrong microphone).
    static constexpr float SILENCE_LEVEL = 0.003f;  // (about -50 dBFS)
    static constexpr int SILENCE_MS = 5000;

    explicit Recorder(QObject* parent = nullptr);
    ~Recorder() override;

    /// Starts recording into `file` (made, with its folder; replaced if there). False: see error().
    bool start(const std::filesystem::path& file);
    void pause();
    void resume();
    /// Ends the recording: the file is complete. Returns its length in milliseconds (0 when none ran).
    int64_t stop();

    State state() const { return st; }
    bool isRecording() const { return st != State::Idle; }
    const std::filesystem::path& file() const { return path; }
    /// The length of the recording so far: the timestamp of what is written now.
    int64_t positionMs() const;
    int sampleRate() const;
    /// The loudest sample of the last 50 ms (0 … 1).
    float level() const { return lastLevel; }
    /// Nothing louder than SILENCE_LEVEL in the first SILENCE_MS of the recording (until something is).
    bool silent() const { return quiet; }
    const QString& error() const { return err; }

Q_SIGNALS:
    void stateChanged();
    /// At most every 100 ms of sound.
    void positionChanged();
    void levelChanged();
    void silentChanged();
    /// The device or the disk failed: the recording was ended (the file holds what came before).
    void failed(const QString& error);
    /// A recording ended (stop() or a failure): its file and length.
    void finished(const QString& file, qint64 durationMs);

private:
    void take(const float* samples, size_t count);
    void fail(const QString& error);

    std::unique_ptr<AudioInput> input;
    VorbisWriter writer;
    std::filesystem::path path;
    State st = State::Idle;
    QString err;
    float peak = 0;
    float lastLevel = 0;
    int64_t levelFrom = 0;     ///< sample where the current level window started
    int64_t positionSent = 0;  ///< sample of the last positionChanged
    bool heard = false;        ///< something louder than SILENCE_LEVEL came
    bool quiet = false;
    bool failing = false;  ///< a failure is being handled (the samples after it are dropped)
};

}  // namespace xqt::audio
