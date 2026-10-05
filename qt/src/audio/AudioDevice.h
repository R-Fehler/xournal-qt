/*
 * xournal-qt: the microphone and the speaker behind small interfaces (qt/docs/audio.md, "Devices").
 *
 * The recorder and the player (Recorder.h, Player.h) only see these. Three backends:
 * - Qt Multimedia (QAudioSource / QAudioSink, QtAudioDevice.cpp): built when Qt6::Multimedia is found and XQT_AUDIO is
 *   on. Only samples go through Qt; the files are Ogg Vorbis by the vendored codec, so no FFmpeg plugin is needed.
 * - fakes (FakeAudio.h): a tone instead of the microphone, a counter instead of the speaker. The tests use them; the
 *   app uses them with XQT_FAKE_AUDIO=1 (UI tests, and builds without Qt Multimedia, to try the UI).
 * - none: recording is not offered (a build without Qt Multimedia).
 *
 * Both run on the thread that starts them (the UI thread): the samples are few (mono, 48 kHz), encoding and decoding
 * them costs well under a millisecond per 10 ms of sound.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace xqt::audio {

/// The microphone: gives mono samples in -1 … 1.
class AudioInput {
public:
    using Consumer = std::function<void(const float* samples, size_t count)>;
    virtual ~AudioInput() = default;
    /// Starts; `consume` gets the samples, on the thread that called start(), until stop(). False: see error().
    virtual bool start(Consumer consume) = 0;
    virtual void stop() = 0;
    /// Pause the device (no samples come); resume() goes on.
    virtual void suspend() = 0;
    virtual void resume() = 0;
    /// The rate of the samples (valid after start).
    virtual int sampleRate() const = 0;
    virtual std::string error() const = 0;
    /// Called when the device fails while running (unplugged, taken by another app): no more samples come.
    std::function<void(const std::string& error)> onError;
};

/// The speaker: takes mono samples in -1 … 1 at a given rate.
class AudioOutput {
public:
    /// Fills `out` with up to `count` samples; fewer: the sound ends after them.
    using Producer = std::function<size_t(float* out, size_t count)>;
    virtual ~AudioOutput() = default;
    /// Starts asking `produce` for samples at `rate`, on the thread that called start(). False: see error().
    virtual bool start(int rate, Producer produce) = 0;
    virtual void stop() = 0;
    virtual void suspend() = 0;
    virtual void resume() = 0;
    /// Samples that were played since start() (not those still in the device's buffer).
    virtual int64_t playedSamples() const = 0;
    /// The producer gave fewer samples than asked and the device has played them all.
    virtual bool finished() const = 0;
    virtual std::string error() const = 0;
    /// Called when the device fails while running.
    std::function<void(const std::string& error)> onError;
};

enum class Backend { None, Fake, Qt };

/// The backend in use: XQT_FAKE_AUDIO=1 gives the fakes; else Qt Multimedia when built with it; else none.
Backend backend();
/// Tests: use the fakes (true) or the default again (false).
void useFakeDevices(bool on);
/// Tests: as a build without any backend (true: recording is not offered) or the default again (false).
void useNoDevices(bool on);
/// Whether recording and playing are offered at all.
inline bool available() { return backend() != Backend::None; }
/// Whether this build has the Qt Multimedia backend.
bool builtWithQtMultimedia();

/// A new microphone or speaker of the current backend (nullptr: none).
std::unique_ptr<AudioInput> makeInput();
std::unique_ptr<AudioOutput> makeOutput();

}  // namespace xqt::audio
