/*
 * xournal-qt: Ogg Vorbis files of the audio recordings (qt/docs/audio.md), with the vendored libvorbis.
 *
 * Recordings are mono Ogg Vorbis at the input device's rate, as Xournal++ writes them (through libsndfile there), so
 * either app plays the other's files. The writer puts every page into the file as soon as the encoder gives it: a
 * recording cut short (the app killed, the battery empty) is a shorter file that still plays. The reader mixes any
 * number of channels down to mono, and seeks to the sample.
 *
 * Plain C++ (no Qt): the recorder feeds the writer from its own thread, the player reads on its own.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace xqt::audio {

/// The encoder's quality (libvorbis' VBR quality, -0.1 … 1). 0.1 gives about 60 kbit/s for dense sound in mono at 44.1
/// or 48 kHz, less for speech with its pauses (qt/docs/audio.md, "Format"): under 30 MB an hour.
constexpr float DEFAULT_QUALITY = 0.1f;

/// Writes a mono Ogg Vorbis file.
class VorbisWriter {
public:
    VorbisWriter();
    ~VorbisWriter();  ///< closes the file (as close())
    VorbisWriter(const VorbisWriter&) = delete;
    VorbisWriter& operator=(const VorbisWriter&) = delete;

    /// Creates `file` (replacing one that is there). `sampleRate`: of the samples given to write(). `comment`: a
    /// free text kept in the file's comment header (TITLE); empty: none.
    bool open(const std::filesystem::path& file, int sampleRate, float quality = DEFAULT_QUALITY,
              const std::string& comment = {});
    bool isOpen() const;
    /// Samples in -1 … 1 (outside: clipped by the encoder).
    bool write(const float* samples, size_t count);
    /// 16-bit samples.
    bool write(const int16_t* samples, size_t count);
    /// Ends the stream (the last page) and closes the file. Returns false when a write failed on the way.
    bool close();

    int sampleRate() const;
    /// Samples written so far: the recording's length is samplesWritten() / sampleRate().
    int64_t samplesWritten() const;
    /// The file's size so far.
    uint64_t bytesWritten() const;
    /// Why open, write or close failed.
    const std::string& error() const;

private:
    struct State;
    std::unique_ptr<State> s;
};

/// Reads an Ogg Vorbis file as mono samples.
class VorbisReader {
public:
    VorbisReader();
    ~VorbisReader();
    VorbisReader(const VorbisReader&) = delete;
    VorbisReader& operator=(const VorbisReader&) = delete;

    bool open(const std::filesystem::path& file);
    bool isOpen() const;
    void close();

    int sampleRate() const;
    int channels() const;  ///< of the file (read() gives one)
    /// Samples (per channel) in the file; for a file cut short, up to its last complete page. -1: unknown.
    int64_t length() const;
    /// length() in milliseconds (-1: unknown).
    int64_t durationMs() const;

    /// Goes to this sample (clamped to the file). Sample-accurate.
    bool seek(int64_t sample);
    bool seekMs(int64_t ms);
    /// The sample read() gives next.
    int64_t position() const;
    int64_t positionMs() const;

    /// Up to `count` mono samples (channels averaged). Returns how many; 0 at the end or on an error (see error()).
    size_t read(float* out, size_t count);
    size_t read(int16_t* out, size_t count);

    const std::string& error() const;

private:
    struct State;
    std::unique_ptr<State> s;
};

/// The length of a recording in milliseconds (-1: not a readable Ogg Vorbis file).
int64_t durationMsOf(const std::filesystem::path& file);

}  // namespace xqt::audio
