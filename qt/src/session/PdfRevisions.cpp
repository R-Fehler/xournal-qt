#include "PdfRevisions.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <stdexcept>
#include <system_error>

#include <qpdf/InputSource.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <zlib.h>

#include "util/Util.h"
#include "FileIo.h"
#include "PdfEncryption.h"

namespace xqt::PdfRevisions {

namespace {

using OH = QPDFObjectHandle;

std::string bytesOf(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

bool isSpace(char c) { return c == ' ' || c == '\n' || c == '\r' || c == '\t' || c == '\f' || c == '\0'; }
bool isDelimiter(char c) {
    return isSpace(c) || c == '(' || c == ')' || c == '<' || c == '>' || c == '[' || c == ']' || c == '{' ||
           c == '}' || c == '/' || c == '%';
}

void skipSpace(const std::string& d, size_t& i) {
    while (i < d.size()) {
        if (isSpace(d[i])) {
            ++i;
        } else if (d[i] == '%') {  // (a comment, to the end of its line)
            while (i < d.size() && d[i] != '\n' && d[i] != '\r') {
                ++i;
            }
        } else {
            break;
        }
    }
}

bool readUInt(const std::string& d, size_t& i, uint64_t& v) {
    size_t n = 0;
    v = 0;
    while (i < d.size() && std::isdigit(static_cast<unsigned char>(d[i])) && n < 20) {
        v = v * 10 + static_cast<uint64_t>(d[i] - '0');
        ++i;
        ++n;
    }
    return n > 0;
}

bool keywordAt(const std::string& d, size_t i, const char* word) {
    const size_t n = std::strlen(word);
    return d.compare(i, n, word) == 0 && (i + n >= d.size() || isDelimiter(d[i + n]));
}

/// The end of the dictionary that starts at `i` ("<<"): after its ">>" (npos: not one).
size_t dictEnd(const std::string& d, size_t i) {
    int depth = 0;
    while (i < d.size()) {
        const char c = d[i];
        if (c == '<' && i + 1 < d.size() && d[i + 1] == '<') {
            ++depth;
            i += 2;
        } else if (c == '>' && i + 1 < d.size() && d[i + 1] == '>') {
            --depth;
            i += 2;
            if (depth == 0) {
                return i;
            }
        } else if (c == '<') {  // a hex string
            const size_t close = d.find('>', i);
            if (close == std::string::npos) {
                return std::string::npos;
            }
            i = close + 1;
        } else if (c == '(') {  // a literal string: nested parentheses, escapes
            int nest = 0;
            for (; i < d.size(); ++i) {
                if (d[i] == '\\') {
                    ++i;
                } else if (d[i] == '(') {
                    ++nest;
                } else if (d[i] == ')' && --nest == 0) {
                    ++i;
                    break;
                }
            }
        } else if (c == '%') {
            while (i < d.size() && d[i] != '\n' && d[i] != '\r') {
                ++i;
            }
        } else {
            ++i;
        }
        if (depth == 0 && i > 0 && c != '%' && !isSpace(c) && c != '<') {
            return std::string::npos;  // (not a dictionary)
        }
    }
    return std::string::npos;
}

/// A dictionary's text as an object (its references go nowhere; only direct values are read).
OH parseDict(const std::string& text) {
    static thread_local std::unique_ptr<QPDF> context;
    if (!context) {
        context = std::make_unique<QPDF>();
        context->emptyPDF();
        context->setSuppressWarnings(true);
    }
    return OH::parse(context.get(), text, "cross-reference section");
}

long long intOf(OH dict, const char* key, long long fallback = -1) {
    OH v = dict.isDictionary() ? dict.getKey(key) : OH::newNull();
    return v.isInteger() ? v.getIntValue() : fallback;
}

struct Entry {
    int num = 0;
    int gen = 0;
    int type = 0;  ///< 0 free, 1 at an offset, 2 in an object stream
};

struct Section {
    bool stream = false;
    std::vector<Entry> entries;
    long long prev = -1;
    long long xrefStm = -1;  ///< a hybrid-reference file's stream
    long long size = 0;
};

std::string inflate(const std::string& in, bool& ok) {
    ok = false;
    z_stream z{};
    if (inflateInit(&z) != Z_OK) {
        return {};
    }
    std::string out;
    char buf[16384];
    z.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(in.data()));
    z.avail_in = static_cast<uInt>(in.size());
    int rc = Z_OK;
    while (rc == Z_OK) {
        z.next_out = reinterpret_cast<Bytef*>(buf);
        z.avail_out = sizeof buf;
        rc = inflate(&z, Z_NO_FLUSH);
        out.append(buf, sizeof buf - z.avail_out);
        if (out.size() > (64u << 20)) {
            break;  // (no cross-reference stream is that big)
        }
    }
    inflateEnd(&z);
    ok = rc == Z_STREAM_END || (rc == Z_BUF_ERROR && z.avail_in == 0);
    return out;
}

/// PNG predictors (10..15): each row starts with its filter type.
bool unpredict(std::string& data, int columns) {
    if (columns <= 0) {
        return false;
    }
    const size_t row = static_cast<size_t>(columns) + 1;
    if (data.size() % row != 0) {
        return false;
    }
    std::string out;
    out.reserve(data.size() / row * static_cast<size_t>(columns));
    std::string prev(static_cast<size_t>(columns), '\0');
    for (size_t at = 0; at < data.size(); at += row) {
        const auto type = static_cast<unsigned char>(data[at]);
        std::string cur = data.substr(at + 1, static_cast<size_t>(columns));
        for (size_t k = 0; k < cur.size(); ++k) {
            const int a = k > 0 ? static_cast<unsigned char>(cur[k - 1]) : 0;  // (one byte per pixel)
            const int b = static_cast<unsigned char>(prev[k]);
            const int c = k > 0 ? static_cast<unsigned char>(prev[k - 1]) : 0;
            int x = static_cast<unsigned char>(cur[k]);
            switch (type) {
                case 0: break;
                case 1: x += a; break;
                case 2: x += b; break;
                case 3: x += (a + b) / 2; break;
                case 4: {
                    const int p = a + b - c;
                    const int pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
                    x += (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c);
                    break;
                }
                default: return false;
            }
            cur[k] = static_cast<char>(x & 0xff);
        }
        out += cur;
        prev = cur;
    }
    data = std::move(out);
    return true;
}

/// A classic cross-reference table at `at` ("xref"), and its trailer.
bool parseTable(const std::string& d, size_t at, Section& s) {
    if (!keywordAt(d, at, "xref")) {
        return false;
    }
    size_t i = at + 4;
    for (;;) {
        skipSpace(d, i);
        if (keywordAt(d, i, "trailer")) {
            i += 7;
            break;
        }
        uint64_t first = 0, count = 0;
        if (!readUInt(d, i, first)) {
            return false;
        }
        skipSpace(d, i);
        if (!readUInt(d, i, count) || count > 50'000'000) {
            return false;
        }
        for (uint64_t k = 0; k < count; ++k) {
            uint64_t offset = 0, gen = 0;
            skipSpace(d, i);
            if (!readUInt(d, i, offset)) {
                return false;
            }
            skipSpace(d, i);
            if (!readUInt(d, i, gen)) {
                return false;
            }
            skipSpace(d, i);
            if (i >= d.size() || (d[i] != 'n' && d[i] != 'f')) {
                return false;
            }
            if (d[i] == 'n') {
                s.entries.push_back({static_cast<int>(first + k), static_cast<int>(gen), 1});
            }
            ++i;
        }
    }
    skipSpace(d, i);
    const size_t end = dictEnd(d, i);
    if (end == std::string::npos) {
        return false;
    }
    try {
        OH trailer = parseDict(d.substr(i, end - i));
        s.prev = intOf(trailer, "/Prev");
        s.xrefStm = intOf(trailer, "/XRefStm");
        s.size = intOf(trailer, "/Size", 0);
    } catch (const std::exception&) {
        return false;
    }
    return true;
}

/// A cross-reference stream at `at` ("n g obj << /Type /XRef … >> stream").
bool parseStream(const std::string& d, size_t at, Section& s) {
    size_t i = at;
    uint64_t num = 0, gen = 0;
    if (!readUInt(d, i, num)) {
        return false;
    }
    skipSpace(d, i);
    if (!readUInt(d, i, gen)) {
        return false;
    }
    skipSpace(d, i);
    if (!keywordAt(d, i, "obj")) {
        return false;
    }
    i += 3;
    skipSpace(d, i);
    const size_t end = dictEnd(d, i);
    if (end == std::string::npos) {
        return false;
    }
    OH dict;
    try {
        dict = parseDict(d.substr(i, end - i));
    } catch (const std::exception&) {
        return false;
    }
    if (!dict.isDictionary() || !dict.getKey("/Type").isNameAndEquals("/XRef")) {
        return false;
    }
    i = end;
    skipSpace(d, i);
    if (!keywordAt(d, i, "stream")) {
        return false;
    }
    i += 6;
    if (i < d.size() && d[i] == '\r') {
        ++i;
    }
    if (i < d.size() && d[i] == '\n') {
        ++i;
    }
    const long long length = intOf(dict, "/Length");
    if (length < 0 || i + static_cast<uint64_t>(length) > d.size()) {
        return false;
    }
    std::string data = d.substr(i, static_cast<size_t>(length));
    OH filter = dict.getKey("/Filter");
    if (filter.isArray() && filter.getArrayNItems() == 1) {
        filter = filter.getArrayItem(0);
    }
    OH parms = dict.getKey("/DecodeParms");
    if (parms.isArray() && parms.getArrayNItems() == 1) {
        parms = parms.getArrayItem(0);
    }
    if (filter.isNameAndEquals("/FlateDecode")) {
        bool ok = false;
        data = inflate(data, ok);
        if (!ok) {
            return false;
        }
    } else if (!filter.isNull()) {
        return false;
    }
    if (const long long predictor = intOf(parms, "/Predictor", 1); predictor >= 10) {
        if (!unpredict(data, static_cast<int>(intOf(parms, "/Columns", 1)))) {
            return false;
        }
    } else if (predictor != 1) {
        return false;
    }
    OH w = dict.getKey("/W");
    if (!w.isArray() || w.getArrayNItems() != 3) {
        return false;
    }
    int widths[3];
    for (int k = 0; k < 3; ++k) {
        OH v = w.getArrayItem(k);
        if (!v.isInteger() || v.getIntValue() < 0 || v.getIntValue() > 8) {
            return false;
        }
        widths[k] = static_cast<int>(v.getIntValue());
    }
    const size_t rowSize = static_cast<size_t>(widths[0] + widths[1] + widths[2]);
    s.size = intOf(dict, "/Size", 0);
    std::vector<std::pair<long long, long long>> index;
    if (OH idx = dict.getKey("/Index"); idx.isArray()) {
        for (int k = 0; k + 1 < idx.getArrayNItems(); k += 2) {
            index.emplace_back(idx.getArrayItem(k).getIntValueAsInt(), idx.getArrayItem(k + 1).getIntValueAsInt());
        }
    } else {
        index.emplace_back(0, s.size);
    }
    auto field = [&](size_t at, int width, uint64_t fallback) {
        if (width == 0) {
            return fallback;
        }
        uint64_t v = 0;
        for (int k = 0; k < width; ++k) {
            v = (v << 8) | static_cast<unsigned char>(data[at + static_cast<size_t>(k)]);
        }
        return v;
    };
    size_t row = 0;
    for (const auto& [first, count]: index) {
        for (long long k = 0; k < count; ++k, ++row) {
            const size_t base = row * rowSize;
            if (rowSize == 0 || base + rowSize > data.size()) {
                return false;
            }
            const auto type = field(base, widths[0], 1);
            const auto third = field(base + static_cast<size_t>(widths[0] + widths[1]), widths[2], 0);
            if (type == 1) {
                s.entries.push_back({static_cast<int>(first + k), static_cast<int>(third), 1});
            } else if (type == 2) {
                s.entries.push_back({static_cast<int>(first + k), 0, 2});
            }
        }
    }
    s.stream = true;
    s.prev = intOf(dict, "/Prev");
    return true;
}

bool parseSection(const std::string& d, uint64_t at, Section& s) {
    if (at >= d.size()) {
        return false;
    }
    const auto i = static_cast<size_t>(at);
    return keywordAt(d, i, "xref") ? parseTable(d, i, s) : parseStream(d, i, s);
}

/// A bounded view of a file for qpdf: its first `limit` bytes.
class PrefixSource: public InputSource {
public:
    PrefixSource(const fs::path& file, uint64_t limit): limit(static_cast<qpdf_offset_t>(limit)) {
        name = file.string();
#ifdef _WIN32
        f = _wfopen(file.wstring().c_str(), L"rb");
#else
        f = std::fopen(file.string().c_str(), "rb");
#endif
        if (!f) {
            throw std::runtime_error("Cannot read \"" + name + "\"");
        }
    }
    ~PrefixSource() override {
        if (f) {
            std::fclose(f);
        }
    }
    qpdf_offset_t findAndSkipNextEOL() override {
        qpdf_offset_t result = 0;
        bool done = false;
        char buf[4096];
        while (!done) {
            const qpdf_offset_t cur = pos;
            const size_t len = read(buf, sizeof buf);
            if (len == 0) {
                done = true;
                result = pos;
            } else {
                char* p1 = static_cast<char*>(std::memchr(buf, '\r', len));
                char* p2 = static_cast<char*>(std::memchr(buf, '\n', len));
                char* p = (p1 && p2) ? std::min(p1, p2) : p1 ? p1 : p2;
                if (p) {
                    result = cur + (p - buf);
                    pos = result + 1;
                    char ch = 0;
                    while (!done) {
                        if (read(&ch, 1) == 0) {
                            done = true;
                        } else if (ch != '\r' && ch != '\n') {
                            unreadCh(ch);
                            done = true;
                        }
                    }
                }
            }
        }
        return result;
    }
    std::string const& getName() const override { return name; }
    qpdf_offset_t tell() override { return pos; }
    void seek(qpdf_offset_t offset, int whence) override {
        switch (whence) {
            case SEEK_SET: pos = offset; break;
            case SEEK_END: pos = limit + offset; break;
            default: pos += offset; break;
        }
        if (pos < 0) {
            throw std::runtime_error(name + ": seek before the start");
        }
    }
    void rewind() override { pos = 0; }
    size_t read(char* buffer, size_t length) override {
        last_offset = pos;
        if (pos >= limit) {
            return 0;
        }
        length = std::min(length, static_cast<size_t>(limit - pos));
#ifdef _WIN32
        _fseeki64(f, pos, SEEK_SET);
#else
        fseeko(f, pos, SEEK_SET);
#endif
        const size_t n = std::fread(buffer, 1, length, f);
        pos += static_cast<qpdf_offset_t>(n);
        return n;
    }
    void unreadCh(char) override {
        if (pos > 0) {
            --pos;
        }
    }

private:
    std::FILE* f = nullptr;
    std::string name;
    qpdf_offset_t limit;
    qpdf_offset_t pos = 0;
};

}  // namespace

const Revision* Chain::at(uint64_t end) const {
    for (const auto& r: revisions) {
        if (r.end == end) {
            return &r;
        }
    }
    return nullptr;
}

Chain read(const fs::path& file) {
    Chain chain;
    const std::string d = bytesOf(file);
    chain.size = d.size();
    if (d.empty()) {
        chain.error = "The file is empty or cannot be read.";
        return chain;
    }
    // Every "%%EOF" with the "startxref" before it: the section it names, and where that revision ends
    struct Eof {
        uint64_t startxref;
        uint64_t end;
    };
    std::vector<Eof> eofs;
    for (size_t at = d.find("%%EOF"); at != std::string::npos; at = d.find("%%EOF", at + 5)) {
        const size_t sx = d.rfind("startxref", at);
        if (sx == std::string::npos || at - sx > 64) {
            continue;
        }
        size_t i = sx + 9;
        skipSpace(d, i);
        uint64_t offset = 0;
        if (!readUInt(d, i, offset)) {
            continue;
        }
        while (i < at && isSpace(d[i])) {  // (not skipSpace: "%%EOF" is no comment here)
            ++i;
        }
        if (i != at) {
            continue;
        }
        size_t end = at + 5;
        if (end < d.size() && d[end] == '\r') {
            ++end;
        }
        if (end < d.size() && d[end] == '\n') {
            ++end;
        }
        eofs.push_back({offset, end});
    }
    // The chain from the last "%%EOF" whose sections all read; else from the one before it (a damaged tail)
    for (auto last = eofs.rbegin(); last != eofs.rend(); ++last) {
        if (last->startxref == 0) {
            continue;  // (a linearized file's first-page trailer)
        }
        struct Walked {
            uint64_t offset;
            Section section;
        };
        std::vector<Walked> walked;  // newest first
        std::set<uint64_t> seen;
        bool ok = true;
        for (long long at = static_cast<long long>(last->startxref); at >= 0;) {
            if (!seen.insert(static_cast<uint64_t>(at)).second) {
                break;
            }
            Section s;
            if (!parseSection(d, static_cast<uint64_t>(at), s)) {
                ok = false;
                break;
            }
            if (s.xrefStm > 0) {
                Section hybrid;
                if (parseSection(d, static_cast<uint64_t>(s.xrefStm), hybrid)) {
                    s.entries.insert(s.entries.end(), hybrid.entries.begin(), hybrid.entries.end());
                }
            }
            const long long prev = s.prev;
            walked.push_back({static_cast<uint64_t>(at), std::move(s)});
            at = prev;
        }
        if (!ok || walked.empty()) {
            continue;
        }
        // Revisions: a section with an "%%EOF" of its own after it; one without belongs to the newer one before it
        std::vector<Revision> newestFirst;
        for (const auto& w: walked) {
            uint64_t end = 0;
            for (const auto& e: eofs) {
                if (e.startxref == w.offset && e.end > w.offset && (end == 0 || e.end < end)) {
                    end = e.end;
                }
            }
            if (end == 0 && !newestFirst.empty()) {
                for (const auto& e: w.section.entries) {
                    newestFirst.back().objects.emplace_back(e.num, e.gen);
                }
                continue;
            }
            Revision r;
            r.end = end != 0 ? end : last->end;
            r.startxref = w.offset;
            r.xrefStream = w.section.stream;
            r.size = w.section.size;
            for (const auto& e: w.section.entries) {
                r.objects.emplace_back(e.num, e.gen);
            }
            newestFirst.push_back(std::move(r));
        }
        if (newestFirst.front().end != last->end) {
            continue;
        }
        std::vector<Revision> revs(newestFirst.rbegin(), newestFirst.rend());
        bool rising = true;
        for (size_t k = 1; k < revs.size(); ++k) {
            rising = rising && revs[k].end > revs[k - 1].end;
        }
        if (!rising) {
            // (sections that do not follow each other in the file: one revision, the file as it is)
            Revision whole = revs.back();
            for (size_t k = 0; k + 1 < revs.size(); ++k) {
                whole.objects.insert(whole.objects.end(), revs[k].objects.begin(), revs[k].objects.end());
            }
            revs = {whole};
        }
        for (size_t k = 0; k < revs.size(); ++k) {
            revs[k].start = k == 0 ? 0 : revs[k - 1].end;
        }
        chain.revisions = std::move(revs);
        size_t i = static_cast<size_t>(chain.end());
        while (i < d.size() && isSpace(d[i])) {
            ++i;
        }
        chain.garbage = i < d.size();
        return chain;
    }
    chain.error = "The file has no readable cross-reference section.";
    return chain;
}

IncrementalPdf::Tail tailOf(const fs::path& file, const Revision& r) {
    IncrementalPdf::Tail t;
    t.size = r.end;
    t.startxref = r.startxref;
    t.xrefStream = r.xrefStream;
    t.eol = true;
    if (r.end > 0) {
        std::ifstream in(file, std::ios::binary);
        in.seekg(static_cast<std::streamoff>(r.end - 1));
        char c = 0;
        in.get(c);
        t.eol = c == '\n' || c == '\r';
    }
    return t;
}

std::shared_ptr<InputSource> prefixSource(const fs::path& file, uint64_t end) {
    return std::make_shared<PrefixSource>(file, end);
}

void open(QPDF& q, const fs::path& file, uint64_t end) {
    q.setSuppressWarnings(true);
    PdfEncryption::openQpdf(q, prefixSource(file, end), file);  // (an encrypted file: with its password)
}

std::string isoOfPdfDate(const std::string& pdfDate) {
    std::string s = pdfDate;
    if (s.rfind("D:", 0) == 0) {
        s = s.substr(2);
    }
    auto num = [&](size_t at, size_t n, int fallback) {
        if (s.size() < at + n) {
            return fallback;
        }
        int v = 0;
        for (size_t k = at; k < at + n; ++k) {
            if (!std::isdigit(static_cast<unsigned char>(s[k]))) {
                return fallback;
            }
            v = v * 10 + (s[k] - '0');
        }
        return v;
    };
    std::tm tm{};
    tm.tm_year = num(0, 4, -1);
    if (tm.tm_year < 0) {
        return {};
    }
    tm.tm_year -= 1900;
    tm.tm_mon = num(4, 2, 1) - 1;
    tm.tm_mday = num(6, 2, 1);
    tm.tm_hour = num(8, 2, 0);
    tm.tm_min = num(10, 2, 0);
    tm.tm_sec = num(12, 2, 0);
    long offset = 0;  // (seconds east of UTC)
    if (s.size() > 14 && (s[14] == '+' || s[14] == '-')) {
        const int h = num(15, 2, 0);
        const int m = s.size() > 18 && s[17] == '\'' ? num(18, 2, 0) : num(17, 2, 0);
        offset = (s[14] == '+' ? 1 : -1) * (h * 3600L + m * 60L);
    }
#ifdef _WIN32
    const std::time_t utc = _mkgmtime(&tm) - offset;
#else
    const std::time_t utc = timegm(&tm) - offset;
#endif
    std::tm out{};
#ifdef _WIN32
    gmtime_s(&out, &utc);
#else
    gmtime_r(&utc, &out);
#endif
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", &out);
    return buf;
}

std::string dateOf(const fs::path& file, uint64_t end) {
    try {
        QPDF q;
        open(q, file, end);
        OH info = q.getTrailer().getKey("/Info");
        OH date = info.isDictionary() ? info.getKey("/ModDate") : OH::newNull();
        if (!date.isString()) {
            date = info.isDictionary() ? info.getKey("/CreationDate") : OH::newNull();
        }
        return date.isString() ? isoOfPdfDate(date.getUTF8Value()) : std::string();
    } catch (const std::exception&) {
        return {};
    }
}

bool extract(const fs::path& file, uint64_t end, const fs::path& out, std::string& error) {
    std::error_code ec;
    if (fs::file_size(file, ec) < end || ec) {
        error = "The file is shorter than that revision.";
        return false;
    }
    fs::create_directories(out.parent_path(), ec);
    fileio::AtomicFile written(out);
    {
        std::ifstream in(file, std::ios::binary);
        std::ofstream o(written.temp(), std::ios::binary | std::ios::trunc);
        char buf[65536];
        uint64_t left = end;
        while (left > 0 && in && o) {
            const auto n = static_cast<std::streamsize>(std::min<uint64_t>(left, sizeof buf));
            in.read(buf, n);
            o.write(buf, in.gcount());
            left -= static_cast<uint64_t>(in.gcount());
            if (in.gcount() == 0) {
                break;
            }
        }
        o.flush();
        if (left != 0 || !o) {
            error = "Could not write \"" + out.string() + "\".";
            return false;
        }
    }
    return written.commit(error);
}

}  // namespace xqt::PdfRevisions
