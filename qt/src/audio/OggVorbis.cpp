#include "OggVorbis.h"

#include <algorithm>
#include <cstdio>
#include <random>
#include <vector>

#include <ogg/ogg.h>
#include <vorbis/codec.h>
#include <vorbis/vorbisenc.h>
#define OV_EXCLUDE_STATIC_CALLBACKS  // (not used: our own, for wide paths)
#include <vorbis/vorbisfile.h>

namespace xqt::audio {

namespace {
std::FILE* openFile(const std::filesystem::path& file, bool write) {
#ifdef _WIN32
    return _wfopen(file.c_str(), write ? L"wb" : L"rb");
#else
    return std::fopen(file.c_str(), write ? "wb" : "rb");
#endif
}

/// How many samples go to the encoder at once (its buffer grows to this).
constexpr size_t CHUNK = 4096;
}  // namespace

// --- writing ----------------------------------------------------------------------------------------------------------

struct VorbisWriter::State {
    std::FILE* f = nullptr;
    ogg_stream_state os{};
    vorbis_info vi{};
    vorbis_comment vc{};
    vorbis_dsp_state vd{};
    vorbis_block vb{};
    int rate = 0;
    int64_t samples = 0;
    int64_t pagedAt = 0;  ///< samples given when a page was last written
    uint64_t bytes = 0;
    bool failed = false;
    std::string error;

    bool writePage(const ogg_page& og) {
        if (std::fwrite(og.header, 1, static_cast<size_t>(og.header_len), f) != static_cast<size_t>(og.header_len) ||
            std::fwrite(og.body, 1, static_cast<size_t>(og.body_len), f) != static_cast<size_t>(og.body_len)) {
            failed = true;
            error = "Could not write the recording (disk full?)";
            return false;
        }
        bytes += static_cast<uint64_t>(og.header_len + og.body_len);
        return true;
    }

    /// Encodes what the encoder has buffered and writes the pages it gives, at least one a second (and flushed): if
    /// the app stops, the file on disk plays up to about a second before.
    void drain() {
        ogg_packet op;
        ogg_page og;
        bool wrote = false;
        while (vorbis_analysis_blockout(&vd, &vb) == 1) {
            vorbis_analysis(&vb, nullptr);
            vorbis_bitrate_addblock(&vb);
            while (vorbis_bitrate_flushpacket(&vd, &op)) {
                ogg_stream_packetin(&os, &op);
                while (ogg_stream_pageout(&os, &og) != 0) {
                    wrote = writePage(og) || wrote;
                }
            }
        }
        if (!wrote && samples - pagedAt >= rate) {
            while (ogg_stream_flush(&os, &og) != 0) {
                wrote = writePage(og) || wrote;
            }
        }
        if (wrote) {
            pagedAt = samples;
            std::fflush(f);
        }
    }

    void clear() {
        ogg_stream_clear(&os);
        vorbis_block_clear(&vb);
        vorbis_dsp_clear(&vd);
        vorbis_comment_clear(&vc);
        vorbis_info_clear(&vi);
    }
};

VorbisWriter::VorbisWriter() = default;

VorbisWriter::~VorbisWriter() { close(); }

bool VorbisWriter::open(const std::filesystem::path& file, int sampleRate, float quality, const std::string& comment) {
    close();
    auto st = std::make_unique<State>();
    vorbis_info_init(&st->vi);
    if (sampleRate <= 0 || vorbis_encode_init_vbr(&st->vi, 1, sampleRate, quality) != 0) {
        vorbis_info_clear(&st->vi);
        s = std::make_unique<State>();
        s->error = "The encoder does not take this sample rate: " + std::to_string(sampleRate);
        return false;
    }
    st->f = openFile(file, true);
    if (!st->f) {
        vorbis_info_clear(&st->vi);
        s = std::make_unique<State>();
        s->error = "Could not create the recording " + file.string();
        return false;
    }
    st->rate = sampleRate;
    vorbis_comment_init(&st->vc);
    vorbis_comment_add_tag(&st->vc, "ENCODER", "xournal-qt");
    if (!comment.empty()) {
        vorbis_comment_add_tag(&st->vc, "TITLE", comment.c_str());
    }
    vorbis_analysis_init(&st->vd, &st->vi);
    vorbis_block_init(&st->vd, &st->vb);
    std::random_device rd;
    ogg_stream_init(&st->os, static_cast<int>(rd() & 0x7fffffff));
    ogg_packet header, comm, code;
    vorbis_analysis_headerout(&st->vd, &st->vc, &header, &comm, &code);
    ogg_stream_packetin(&st->os, &header);
    ogg_stream_packetin(&st->os, &comm);
    ogg_stream_packetin(&st->os, &code);
    ogg_page og;
    while (ogg_stream_flush(&st->os, &og) != 0) {  // (the audio starts on a page of its own)
        if (!st->writePage(og)) {
            break;
        }
    }
    std::fflush(st->f);
    s = std::move(st);
    return !s->failed;
}

bool VorbisWriter::isOpen() const { return s && s->f; }

bool VorbisWriter::write(const float* samples, size_t count) {
    if (!isOpen() || s->failed) {
        return false;
    }
    while (count > 0) {
        const size_t n = std::min(count, CHUNK);
        float** buffer = vorbis_analysis_buffer(&s->vd, static_cast<int>(n));
        std::copy(samples, samples + n, buffer[0]);
        vorbis_analysis_wrote(&s->vd, static_cast<int>(n));
        s->samples += static_cast<int64_t>(n);
        s->drain();
        samples += n;
        count -= n;
    }
    return !s->failed;
}

bool VorbisWriter::write(const int16_t* samples, size_t count) {
    if (!isOpen() || s->failed) {
        return false;
    }
    while (count > 0) {
        const size_t n = std::min(count, CHUNK);
        float** buffer = vorbis_analysis_buffer(&s->vd, static_cast<int>(n));
        for (size_t i = 0; i < n; ++i) {
            buffer[0][i] = static_cast<float>(samples[i]) / 32768.f;
        }
        vorbis_analysis_wrote(&s->vd, static_cast<int>(n));
        s->samples += static_cast<int64_t>(n);
        s->drain();
        samples += n;
        count -= n;
    }
    return !s->failed;
}

bool VorbisWriter::close() {
    if (!isOpen()) {
        return s ? !s->failed : true;
    }
    vorbis_analysis_wrote(&s->vd, 0);  // (the end of the stream)
    s->drain();
    ogg_page og;
    while (ogg_stream_flush(&s->os, &og) != 0) {
        if (!s->writePage(og)) {
            break;
        }
    }
    s->clear();
    if (std::fclose(s->f) != 0 && !s->failed) {
        s->failed = true;
        s->error = "Could not close the recording";
    }
    s->f = nullptr;
    return !s->failed;
}

int VorbisWriter::sampleRate() const { return s ? s->rate : 0; }

int64_t VorbisWriter::samplesWritten() const { return s ? s->samples : 0; }

uint64_t VorbisWriter::bytesWritten() const { return s ? s->bytes : 0; }

const std::string& VorbisWriter::error() const {
    static const std::string none;
    return s ? s->error : none;
}

// --- reading ----------------------------------------------------------------------------------------------------------

namespace {
size_t readCb(void* ptr, size_t size, size_t nmemb, void* source) {
    return std::fread(ptr, size, nmemb, static_cast<std::FILE*>(source));
}
int seekCb(void* source, ogg_int64_t offset, int whence) {
#ifdef _WIN32
    return _fseeki64(static_cast<std::FILE*>(source), offset, whence);
#else
    return fseeko(static_cast<std::FILE*>(source), static_cast<off_t>(offset), whence);
#endif
}
int closeCb(void* source) { return std::fclose(static_cast<std::FILE*>(source)); }
long tellCb(void* source) {
#ifdef _WIN32
    return static_cast<long>(_ftelli64(static_cast<std::FILE*>(source)));
#else
    return static_cast<long>(ftello(static_cast<std::FILE*>(source)));
#endif
}
}  // namespace

struct VorbisReader::State {
    OggVorbis_File vf{};
    bool open = false;
    int rate = 0;
    int channels = 0;
    int64_t length = -1;
    std::string error;
};

VorbisReader::VorbisReader(): s(std::make_unique<State>()) {}

VorbisReader::~VorbisReader() { close(); }

bool VorbisReader::open(const std::filesystem::path& file) {
    close();
    s->error.clear();
    std::FILE* f = openFile(file, false);
    if (!f) {
        s->error = "Could not open the recording " + file.string();
        return false;
    }
    const ov_callbacks callbacks{readCb, seekCb, closeCb, tellCb};
    if (const int r = ov_open_callbacks(f, &s->vf, nullptr, 0, callbacks); r != 0) {
        std::fclose(f);  // (not closed by vorbisfile when opening failed)
        s->error = r == OV_ENOTVORBIS ? "Not an Ogg Vorbis file" : "The recording cannot be read";
        return false;
    }
    s->open = true;
    const vorbis_info* vi = ov_info(&s->vf, -1);
    s->rate = vi ? static_cast<int>(vi->rate) : 0;
    s->channels = vi ? vi->channels : 0;
    const ogg_int64_t total = ov_seekable(&s->vf) ? ov_pcm_total(&s->vf, -1) : OV_EINVAL;
    s->length = total >= 0 ? static_cast<int64_t>(total) : -1;
    if (s->rate <= 0 || s->channels <= 0) {
        close();
        s->error = "The recording has no audio";
        return false;
    }
    return true;
}

bool VorbisReader::isOpen() const { return s->open; }

void VorbisReader::close() {
    if (s->open) {
        ov_clear(&s->vf);  // (closes the file)
        s->open = false;
    }
    s->rate = s->channels = 0;
    s->length = -1;
}

int VorbisReader::sampleRate() const { return s->rate; }

int VorbisReader::channels() const { return s->channels; }

int64_t VorbisReader::length() const { return s->length; }

int64_t VorbisReader::durationMs() const { return s->length < 0 || s->rate <= 0 ? -1 : s->length * 1000 / s->rate; }

bool VorbisReader::seek(int64_t sample) {
    if (!s->open) {
        return false;
    }
    sample = std::max<int64_t>(0, s->length >= 0 ? std::min(sample, s->length) : sample);
    return ov_pcm_seek(&s->vf, static_cast<ogg_int64_t>(sample)) == 0;
}

bool VorbisReader::seekMs(int64_t ms) { return seek(ms * s->rate / 1000); }

int64_t VorbisReader::position() const {
    return s->open ? static_cast<int64_t>(ov_pcm_tell(&s->vf)) : 0;
}

int64_t VorbisReader::positionMs() const { return s->rate > 0 ? position() * 1000 / s->rate : 0; }

size_t VorbisReader::read(float* out, size_t count) {
    if (!s->open) {
        return 0;
    }
    size_t done = 0;
    while (done < count) {
        float** pcm = nullptr;
        int link = 0;
        const long n = ov_read_float(&s->vf, &pcm, static_cast<int>(std::min<size_t>(count - done, CHUNK)), &link);
        if (n == OV_HOLE) {
            continue;  // (a gap in the data: the samples after it follow)
        }
        if (n <= 0) {
            if (n < 0) {
                s->error = "The recording is damaged";
            }
            break;
        }
        const vorbis_info* vi = ov_info(&s->vf, link);
        const int ch = vi ? vi->channels : 1;
        for (long i = 0; i < n; ++i) {
            float sum = 0;
            for (int c = 0; c < ch; ++c) {
                sum += pcm[c][i];
            }
            out[done + static_cast<size_t>(i)] = sum / static_cast<float>(ch);
        }
        done += static_cast<size_t>(n);
    }
    return done;
}

size_t VorbisReader::read(int16_t* out, size_t count) {
    std::vector<float> buffer(std::min(count, CHUNK));
    size_t done = 0;
    while (done < count) {
        const size_t n = read(buffer.data(), std::min(count - done, buffer.size()));
        if (n == 0) {
            break;
        }
        for (size_t i = 0; i < n; ++i) {
            const float v = std::clamp(buffer[i], -1.f, 1.f);
            out[done + i] = static_cast<int16_t>(v * 32767.f);
        }
        done += n;
    }
    return done;
}

const std::string& VorbisReader::error() const { return s->error; }

int64_t durationMsOf(const std::filesystem::path& file) {
    VorbisReader r;
    return r.open(file) ? r.durationMs() : -1;
}

}  // namespace xqt::audio
