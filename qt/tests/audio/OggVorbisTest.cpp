/*
 * xournal-qt: the Ogg Vorbis files of the recordings (qt/src/audio/OggVorbis.h).
 */
#include <cmath>
#include <filesystem>
#include <fstream>
#include <numbers>
#include <vector>

#include <QTemporaryDir>
#include <gtest/gtest.h>

#include "audio/OggVorbis.h"

#include "config-test.h"

using namespace xqt::audio;
namespace fs = std::filesystem;

namespace {
constexpr int RATE = 48000;

std::vector<float> sine(double hz, double seconds, float amplitude, int rate = RATE) {
    std::vector<float> v(static_cast<size_t>(seconds * rate));
    for (size_t i = 0; i < v.size(); ++i) {
        v[i] = amplitude * static_cast<float>(std::sin(2 * std::numbers::pi * hz * static_cast<double>(i) / rate));
    }
    return v;
}

std::vector<float> readAll(VorbisReader& r) {
    std::vector<float> all;
    std::vector<float> buffer(1000);
    while (const size_t n = r.read(buffer.data(), buffer.size())) {
        all.insert(all.end(), buffer.begin(), buffer.begin() + static_cast<long>(n));
    }
    return all;
}

double rms(const std::vector<float>& v, size_t from, size_t to) {
    double sum = 0;
    for (size_t i = from; i < to; ++i) {
        sum += double(v[i]) * v[i];
    }
    return std::sqrt(sum / static_cast<double>(to - from));
}

fs::path pathOf(const QTemporaryDir& dir, const char* name) { return fs::path(dir.filePath(name).toStdString()); }

void copyPrefix(const fs::path& from, const fs::path& to, std::streamsize bytes) {
    std::ifstream in(from, std::ios::binary);
    std::vector<char> data(static_cast<size_t>(bytes));
    in.read(data.data(), bytes);
    std::ofstream out(to, std::ios::binary);
    out.write(data.data(), in.gcount());
}
}  // namespace

TEST(OggVorbis, aSineComesBackAsItWentIn) {
    QTemporaryDir dir;
    const fs::path file = pathOf(dir, "sine.ogg");
    const auto in = sine(440, 2.0, 0.5f);
    VorbisWriter w;
    ASSERT_TRUE(w.open(file, RATE)) << w.error();
    ASSERT_TRUE(w.write(in.data(), in.size()));
    EXPECT_EQ(w.samplesWritten(), static_cast<int64_t>(in.size()));
    ASSERT_TRUE(w.close()) << w.error();

    VorbisReader r;
    ASSERT_TRUE(r.open(file)) << r.error();
    EXPECT_EQ(r.sampleRate(), RATE);
    EXPECT_EQ(r.channels(), 1);
    EXPECT_EQ(r.length(), static_cast<int64_t>(in.size()));  // (the last page's granule: the exact length)
    EXPECT_EQ(r.durationMs(), 2000);
    const auto out = readAll(r);
    ASSERT_EQ(out.size(), in.size());
    // Lossy, but close: the error is far below the signal (no delay, no other pitch)
    double signal = 0, noise = 0;
    for (size_t i = 0; i < in.size(); ++i) {
        signal += double(in[i]) * in[i];
        noise += (double(out[i]) - in[i]) * (double(out[i]) - in[i]);
    }
    EXPECT_GT(10 * std::log10(signal / noise), 20.0);
    EXPECT_NEAR(rms(out, 0, out.size()), 0.5 / std::sqrt(2.0), 0.02);
    // Mono at about 64 kbit/s or less (a pure tone needs less than speech)
    const double kbits = static_cast<double>(fs::file_size(file)) * 8 / 2.0 / 1000;
    EXPECT_LT(kbits, 96.0);
    EXPECT_GT(kbits, 8.0);
    EXPECT_EQ(durationMsOf(file), 2000);
}

TEST(OggVorbis, sixteenBitSamplesAndSmallChunks) {
    QTemporaryDir dir;
    const fs::path file = pathOf(dir, "chunks.ogg");
    const auto f = sine(1000, 1.0, 0.25f, 44100);
    std::vector<int16_t> in(f.size());
    for (size_t i = 0; i < f.size(); ++i) {
        in[i] = static_cast<int16_t>(f[i] * 32767);
    }
    VorbisWriter w;
    ASSERT_TRUE(w.open(file, 44100));
    for (size_t i = 0, n = 1; i < in.size(); i += n, n = n % 997 + 7) {  // (as an audio device gives them)
        ASSERT_TRUE(w.write(in.data() + i, std::min(n, in.size() - i)));
    }
    ASSERT_TRUE(w.close());
    VorbisReader r;
    ASSERT_TRUE(r.open(file));
    EXPECT_EQ(r.sampleRate(), 44100);
    EXPECT_EQ(r.length(), 44100);
    std::vector<int16_t> out(50000);
    ASSERT_EQ(r.read(out.data(), out.size()), 44100u);
    double sum = 0;
    for (size_t i = 0; i < 44100; ++i) {
        sum += double(out[i]) * out[i];
    }
    EXPECT_NEAR(std::sqrt(sum / 44100) / 32767, 0.25 / std::sqrt(2.0), 0.01);
}

TEST(OggVorbis, seekingLandsOnTheSample) {
    QTemporaryDir dir;
    const fs::path file = pathOf(dir, "click.ogg");
    // Silence with a tone burst from 1000 to 1010 ms
    std::vector<float> in(2 * RATE, 0.f);
    const auto burst = sine(1000, 0.010, 0.8f);
    std::copy(burst.begin(), burst.end(), in.begin() + RATE);
    VorbisWriter w;
    ASSERT_TRUE(w.open(file, RATE));
    ASSERT_TRUE(w.write(in.data(), in.size()));
    ASSERT_TRUE(w.close());

    VorbisReader r;
    ASSERT_TRUE(r.open(file));
    for (int target: {980, 500, 990}) {  // (forwards and back)
        ASSERT_TRUE(r.seekMs(target));
        EXPECT_EQ(r.position(), target * RATE / 1000);
        EXPECT_EQ(r.positionMs(), target);
        std::vector<float> out(RATE / 2);
        ASSERT_EQ(r.read(out.data(), out.size()), out.size());
        size_t onset = 0;
        while (onset < out.size() && std::abs(out[onset]) < 0.2f) {
            ++onset;
        }
        const double ms = static_cast<double>(onset) * 1000 / RATE;
        EXPECT_NEAR(ms, 1000 - target, 1.0) << "after seeking to " << target << " ms";
    }
    // Past the end: at the end
    ASSERT_TRUE(r.seekMs(5000));
    float x;
    EXPECT_EQ(r.read(&x, 1), 0u);
}

TEST(OggVorbis, aRecordingCutShortStillPlays) {
    QTemporaryDir dir;
    const fs::path file = pathOf(dir, "long.ogg");
    const auto in = sine(300, 10.0, 0.4f);
    VorbisWriter w;
    ASSERT_TRUE(w.open(file, RATE));
    for (size_t i = 0; i < in.size(); i += 480) {  // (10 ms at a time, as from a device)
        ASSERT_TRUE(w.write(in.data() + i, 480));
    }
    // The app stops here (no close()): what is on disk plays, up to about a second before
    const fs::path crashed = pathOf(dir, "crashed.ogg");
    fs::copy_file(file, crashed);
    // and a file cut in the middle of a page
    const fs::path cut = pathOf(dir, "cut.ogg");
    copyPrefix(file, cut, static_cast<std::streamsize>(fs::file_size(file) * 2 / 3) + 17);
    ASSERT_TRUE(w.close());

    for (const auto& [f, least]: {std::pair{crashed, 8800}, std::pair{cut, 3000}}) {
        VorbisReader r;
        ASSERT_TRUE(r.open(f)) << f << ": " << r.error();
        EXPECT_GE(r.durationMs(), least) << f;
        EXPECT_LE(r.durationMs(), 10000) << f;
        const auto out = readAll(r);
        EXPECT_EQ(static_cast<int64_t>(out.size()), r.length()) << f;
        ASSERT_GT(out.size(), size_t(RATE));
        EXPECT_NEAR(rms(out, RATE / 10, out.size() - RATE / 10), 0.4 / std::sqrt(2.0), 0.02) << f;
    }
}

TEST(OggVorbis, whatIsNoRecordingIsRefused) {
    QTemporaryDir dir;
    const fs::path text = pathOf(dir, "text.ogg");
    std::ofstream(text) << "not a recording";
    VorbisReader r;
    EXPECT_FALSE(r.open(text));
    EXPECT_FALSE(r.error().empty());
    EXPECT_EQ(durationMsOf(text), -1);
    EXPECT_EQ(durationMsOf(pathOf(dir, "missing.ogg")), -1);
    // Only the start of the headers
    const fs::path file = pathOf(dir, "full.ogg");
    {
        VorbisWriter w;
        ASSERT_TRUE(w.open(file, RATE));
        const auto in = sine(440, 0.5, 0.3f);
        w.write(in.data(), in.size());
    }
    const fs::path headers = pathOf(dir, "headers.ogg");
    copyPrefix(file, headers, 40);
    EXPECT_FALSE(r.open(headers));
    // A rate the encoder does not take
    VorbisWriter w;
    EXPECT_FALSE(w.open(pathOf(dir, "bad.ogg"), 0));
    EXPECT_FALSE(w.error().empty());
}

TEST(OggVorbis, xournalppsRecordingPlays) {
    const fs::path file(GET_TESTFILE(u8"packaged_xopp/audioAttachment/test.ogg"));
    VorbisReader r;
    ASSERT_TRUE(r.open(file)) << r.error();
    EXPECT_EQ(r.channels(), 1);
    EXPECT_EQ(r.sampleRate(), 48000);
    EXPECT_GT(r.durationMs(), 0);
    const auto out = readAll(r);
    EXPECT_EQ(static_cast<int64_t>(out.size()), r.length());
    EXPECT_TRUE(r.seekMs(r.durationMs() / 2));
    EXPECT_NEAR(static_cast<double>(r.positionMs()), static_cast<double>(r.durationMs() / 2), 1.0);
}

TEST(OggVorbis, aNonAsciiPath) {
    QTemporaryDir dir;
    const fs::path file = fs::path(dir.path().toStdString()) / fs::path(u8"Aufnahme ü 録音.ogg");
    const auto in = sine(440, 0.3, 0.3f);
    VorbisWriter w;
    ASSERT_TRUE(w.open(file, RATE));
    ASSERT_TRUE(w.write(in.data(), in.size()));
    ASSERT_TRUE(w.close());
    EXPECT_EQ(durationMsOf(file), 300);
}
