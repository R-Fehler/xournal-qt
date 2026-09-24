#include "TextFile.h"

#include <chrono>
#include <fstream>

#include <QSaveFile>
#include <QString>

namespace xqt {

namespace {
std::string readAll(const fs::path& file, bool& ok) {
    std::ifstream in(file, std::ios::binary);
    ok = static_cast<bool>(in);
    if (!ok) {
        return {};
    }
    std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    ok = !in.bad();
    return bytes;
}

constexpr std::string_view BOM = "\xEF\xBB\xBF";
}  // namespace

TextFile::Stamp TextFile::stampOf(const fs::path& file) {
    Stamp s;
    std::error_code ec;
    s.size = fs::file_size(file, ec);
    if (ec) {
        return {};
    }
    const auto t = fs::last_write_time(file, ec);
    if (!ec) {
        s.time = std::chrono::duration_cast<std::chrono::nanoseconds>(t.time_since_epoch()).count();
    }
    return s;
}

bool TextFile::load(const fs::path& path, Kind kind, std::string& error) {
    file = path;
    textKind = kind;
    diskStamp = stampOf(path);
    bool ok = false;
    std::string bytes = readAll(path, ok);
    if (!ok) {
        error = "cannot be read";
        return false;
    }
    setBytes(std::move(bytes));
    return true;
}

void TextFile::setBytes(std::string bytes) {
    raw = std::move(bytes);
    analyse();
}

void TextFile::analyse() {
    saved = normalized(raw);
    bom = raw.compare(0, BOM.size(), BOM) == 0;
    size_t crlfs = 0, lfs = 0;
    for (size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] == '\n') {
            (i > 0 && raw[i - 1] == '\r' ? crlfs : lfs)++;
        }
    }
    mainCrlf = crlfs > lfs;
    utf8 = validUtf8(saved);
    tooBig = raw.size() > MAX_EDIT_BYTES;
}

std::string TextFile::normalized(const std::string& bytes) {
    std::string out;
    out.reserve(bytes.size());
    size_t i = bytes.compare(0, BOM.size(), BOM) == 0 ? BOM.size() : 0;
    for (; i < bytes.size(); ++i) {
        if (bytes[i] == '\r' && i + 1 < bytes.size() && bytes[i + 1] == '\n') {
            continue;  // "\r\n": the "\n" follows
        }
        out += bytes[i];
    }
    return out;
}

bool TextFile::validUtf8(const std::string& s) {
    const auto* p = reinterpret_cast<const unsigned char*>(s.data());
    const size_t n = s.size();
    for (size_t i = 0; i < n;) {
        const unsigned char c = p[i];
        size_t len = 0;
        if (c < 0x80) {
            ++i;
            continue;
        } else if ((c & 0xE0) == 0xC0 && c >= 0xC2) {
            len = 2;
        } else if ((c & 0xF0) == 0xE0) {
            len = 3;
        } else if ((c & 0xF8) == 0xF0 && c <= 0xF4) {
            len = 4;
        } else {
            return false;
        }
        if (i + len > n) {
            return false;
        }
        for (size_t k = 1; k < len; ++k) {
            if ((p[i + k] & 0xC0) != 0x80) {
                return false;
            }
        }
        if (len == 3 && ((c == 0xE0 && p[i + 1] < 0xA0) || (c == 0xED && p[i + 1] >= 0xA0))) {
            return false;  // overlong, or a surrogate
        }
        if (len == 4 && ((c == 0xF0 && p[i + 1] < 0x90) || (c == 0xF4 && p[i + 1] >= 0x90))) {
            return false;
        }
        i += len;
    }
    return true;
}

std::string TextFile::encode(const std::string& text) const {
    const std::string& old = saved;
    // The part of the text that changed: between the common start and the common end
    size_t prefix = 0;
    while (prefix < old.size() && prefix < text.size() && old[prefix] == text[prefix]) {
        ++prefix;
    }
    size_t suffix = 0;
    while (suffix < old.size() - prefix && suffix < text.size() - prefix &&
           old[old.size() - 1 - suffix] == text[text.size() - 1 - suffix]) {
        ++suffix;
    }
    if (prefix == old.size() && prefix == text.size()) {
        return raw;  // unchanged
    }
    // Where those places are in the file (a byte order mark before the text; "\r\n" for a "\n")
    const size_t oldEnd = old.size() - suffix;
    size_t rawPrefix = 0, rawSuffixStart = 0;
    {
        size_t j = bom ? BOM.size() : 0;
        for (size_t i = 0; i <= old.size(); ++i) {
            if (i == prefix) {
                rawPrefix = j;
            }
            if (i == oldEnd) {
                rawSuffixStart = j;
                break;
            }
            if (i < old.size()) {
                j += old[i] == '\n' && j < raw.size() && raw[j] == '\r' ? 2 : 1;
            }
        }
    }
    std::string out;
    out.reserve(raw.size() + text.size() - std::min(text.size(), old.size()) + 64);
    out.append(raw, 0, rawPrefix);
    for (size_t i = prefix; i < text.size() - suffix; ++i) {
        if (text[i] == '\n' && mainCrlf) {
            out += '\r';
        }
        out += text[i];
    }
    out.append(raw, rawSuffixStart, std::string::npos);
    return out;
}

bool TextFile::writeAtomically(const fs::path& target, const std::string& bytes, std::string& error) {
    fs::path to = target;
    std::error_code ec;
    if (fs::is_symlink(to, ec)) {
        to = fs::canonical(to, ec);  // (the file it points to is written, the link stays)
        if (ec) {
            to = target;
        }
    }
    QSaveFile out(QString::fromStdString(to.string()));
    out.setDirectWriteFallback(true);  // (a folder that cannot be written in, with a file that can)
    if (!out.open(QIODevice::WriteOnly)) {
        error = out.errorString().toStdString();
        return false;
    }
    if (out.write(bytes.data(), static_cast<qint64>(bytes.size())) != static_cast<qint64>(bytes.size())) {
        error = out.errorString().toStdString();
        out.cancelWriting();
        return false;
    }
    if (!out.commit()) {
        error = out.errorString().toStdString();
        return false;
    }
    return true;
}

void TextFile::written(const fs::path& target, const std::string& text, std::string bytes) {
    if (!target.empty()) {
        file = target;
    }
    raw = std::move(bytes);
    saved = text;
    // (the file's own format stays: its byte order mark and its line ends)
    utf8 = true;
    tooBig = false;
    diskStamp = stampOf(file);
}

bool TextFile::save(const std::string& text, std::string& error, const fs::path& target) {
    const fs::path to = target.empty() ? file : target;
    std::string bytes = encode(text);
    if (!writeAtomically(to, bytes, error)) {
        return false;
    }
    written(to, text, std::move(bytes));
    return true;
}

bool TextFile::changedOnDisk(std::string* now) {
    const Stamp current = stampOf(file);
    if (current == diskStamp) {
        return false;
    }
    bool ok = false;
    std::string bytes = readAll(file, ok);
    if (!ok) {
        return false;  // (gone, or being replaced: asked again at the next check)
    }
    if (bytes == raw) {
        diskStamp = current;  // (touched, or written by us)
        return false;
    }
    if (now) {
        *now = std::move(bytes);
    }
    return true;
}

}  // namespace xqt
