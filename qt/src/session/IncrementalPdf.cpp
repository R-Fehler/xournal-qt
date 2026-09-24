#include "IncrementalPdf.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <random>
#include <sstream>
#include <stdexcept>
#include <system_error>

#include <zlib.h>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

#include "util/Util.h"

namespace xqt::IncrementalPdf {

std::function<bool(uint64_t)> failWriteAt;

namespace {

using OH = QPDFObjectHandle;

std::string deflate(const std::string& data) {
    uLongf size = compressBound(static_cast<uLong>(data.size()));
    std::string out(size, '\0');
    if (compress2(reinterpret_cast<Bytef*>(out.data()), &size, reinterpret_cast<const Bytef*>(data.data()),
                  static_cast<uLong>(data.size()), Z_DEFAULT_COMPRESSION) != Z_OK) {
        throw std::runtime_error("Could not compress a stream of the update");
    }
    out.resize(size);
    return out;
}

std::string rawData(OH stream) {
    auto buffer = stream.getRawStreamData();
    return std::string(reinterpret_cast<const char*>(buffer->getBuffer()), buffer->getSize());
}

/// What an object of the file is: its text (a stream: its dictionary).
std::string textOf(OH o) { return o.isStream() ? o.getDict().unparse() : o.unparseResolved(); }

/// The indirect objects a direct object (or a stream's dictionary) refers to.
void referencesOf(OH o, std::vector<OH>& out, bool top = true) {
    if (o.isIndirect() && !top) {
        out.push_back(o);
        return;
    }
    if (o.isStream()) {
        OH d = o.getDict();
        for (const auto& k: d.getKeys()) {
            if (k != "/Length") {  // (written as a number)
                referencesOf(d.getKey(k), out, false);
            }
        }
    } else if (o.isArray()) {
        for (int i = 0; i < o.getArrayNItems(); ++i) {
            referencesOf(o.getArrayItem(i), out, false);
        }
    } else if (o.isDictionary()) {
        for (const auto& k: o.getKeys()) {
            referencesOf(o.getKey(k), out, false);
        }
    }
}

std::string newId(uint64_t salt) {
    std::random_device rd;
    std::string id(16, '\0');
    uint64_t a = (uint64_t(rd()) << 32) ^ rd() ^ salt;
    uint64_t b = (uint64_t(rd()) << 32) ^ rd() ^
                 static_cast<uint64_t>(std::chrono::system_clock::now().time_since_epoch().count());
    std::memcpy(id.data(), &a, 8);
    std::memcpy(id.data() + 8, &b, 8);
    return id;
}

/// A big-endian field of `width` bytes.
void putField(std::string& out, uint64_t v, int width) {
    for (int i = width - 1; i >= 0; --i) {
        out.push_back(static_cast<char>((v >> (8 * i)) & 0xff));
    }
}

int widthFor(uint64_t v) {
    int w = 1;
    while (w < 8 && (v >> (8 * w)) != 0) {
        ++w;
    }
    return w;
}

/// A unique temporary name next to `target`.
fs::path partOf(const fs::path& target) {
    static std::atomic<unsigned> counter{0};
    return target.parent_path() / ("." + target.filename().string() + "." + std::to_string(Util::getPid()) + "-" +
                                   std::to_string(++counter) + ".part");
}

}  // namespace

bool readTail(const fs::path& file, Tail& tail, std::string& error) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        error = "The file cannot be read.";
        return false;
    }
    in.seekg(0, std::ios::end);
    const auto size = static_cast<uint64_t>(in.tellg());
    const uint64_t n = std::min<uint64_t>(size, 4096);
    std::string end(n, '\0');
    in.seekg(static_cast<std::streamoff>(size - n));
    in.read(end.data(), static_cast<std::streamsize>(n));
    const auto at = end.rfind("startxref");
    if (at == std::string::npos) {
        error = "The file has no cross-reference section at its end.";
        return false;
    }
    size_t i = at + 9;
    while (i < end.size() && std::isspace(static_cast<unsigned char>(end[i]))) {
        ++i;
    }
    uint64_t offset = 0;
    size_t digits = 0;
    for (; i < end.size() && std::isdigit(static_cast<unsigned char>(end[i])); ++i, ++digits) {
        offset = offset * 10 + static_cast<uint64_t>(end[i] - '0');
    }
    if (digits == 0 || offset >= size) {
        error = "The file's last cross-reference offset is not valid.";
        return false;
    }
    std::string head(32, '\0');
    in.clear();
    in.seekg(static_cast<std::streamoff>(offset));
    in.read(head.data(), static_cast<std::streamsize>(head.size()));
    head.resize(static_cast<size_t>(in.gcount()));
    tail.size = size;
    tail.startxref = offset;
    tail.xrefStream = head.rfind("xref", 0) != 0;  // (else "n g obj": a cross-reference stream)
    tail.eol = size > 0 && (end.back() == '\n' || end.back() == '\r');
    return true;
}

Update::Update(QPDF& pdf): pdf(pdf) {
    // (not getObjectCount(): see above)
    for (const auto& [og, entry]: pdf.getXRefTable()) {
        objects.insert(og);
        maxId = std::max(maxId, og.getObj());
    }
    if (OH size = pdf.getTrailer().getKey("/Size"); size.isInteger()) {
        maxId = std::max(maxId, static_cast<int>(size.getIntValue()) - 1);
    }
    nextId = maxId;
}

bool Update::isNew(QPDFObjectHandle object) const {
    return object.isIndirect() && object.getObjectID() > maxId;
}

QPDFObjectHandle Update::add(QPDFObjectHandle value) {
    const QPDFObjGen og(++nextId, 0);
    pdf.replaceObject(og, value);
    return pdf.getObjectByObjGen(og);
}

QPDFObjectHandle Update::addStream(QPDFObjectHandle dictionary, std::string data) {
    OH o = add(dictionary);
    streamData[o.getObjectID()] = std::move(data);
    return o;
}

bool Update::isStream(QPDFObjectHandle object) const {
    return object.isStream() || (isNew(object) && streamData.count(object.getObjectID()));
}

QPDFObjectHandle Update::streamDictionary(QPDFObjectHandle stream) {
    return stream.isStream() ? stream.getDict() : stream;
}

QPDFObjectHandle Update::copyStream(QPDFObjectHandle stream) {
    if (stream.isStream()) {
        return addStream(stream.getDict().shallowCopy(), rawData(stream));
    }
    return addStream(stream.shallowCopy(), streamData.at(stream.getObjectID()));
}

QPDFObjectHandle Update::copy(QPDFObjectHandle foreign) { return copyValue(foreign, true); }

QPDFObjectHandle Update::copyValue(QPDFObjectHandle o, bool top) {
    if (o.isIndirect()) {
        const auto key = std::make_pair(o.getOwningQPDF(), o.getObjGen());
        if (auto it = copied.find(key); it != copied.end()) {
            return it->second;
        }
        if (!top && o.isDictionary() && o.getKey("/Type").isName() && o.getKey("/Type").getName() == "/Page") {
            return OH::newNull();  // (another page: not copied along, as qpdf's copyForeignObject does)
        }
        // Numbered first (the object may refer back to itself), its value copied, then put in place
        const QPDFObjGen og(++nextId, 0);
        OH handle = pdf.getObjectByObjGen(og);
        copied[key] = handle;
        if (o.isStream()) {
            OH dict = copyValue(o.getDict(), false);
            if (dict.hasKey("/Length")) {
                dict.removeKey("/Length");  // (written as a number)
            }
            pdf.replaceObject(og, dict);
            streamData[og.getObj()] = rawData(o);
        } else {
            OH value = o.shallowCopy();  // (resolved, direct)
            pdf.replaceObject(og, copyValue(value, top));
        }
        return handle;
    }
    if (o.isArray()) {
        OH a = OH::newArray();
        for (int i = 0; i < o.getArrayNItems(); ++i) {
            a.appendItem(copyValue(o.getArrayItem(i), false));
        }
        return a;
    }
    if (o.isDictionary()) {
        OH d = OH::newDictionary();
        const bool page = o.getKey("/Type").isName() && o.getKey("/Type").getName() == "/Page";
        for (const auto& k: o.getKeys()) {
            if (top && page && k == "/Parent") {
                continue;
            }
            d.replaceKey(k, copyValue(o.getKey(k), false));
        }
        return d;
    }
    return o.shallowCopy();  // (a number, a name, a string, a boolean, null)
}

void Update::touch(QPDFObjectHandle object) {
    if (!object.isIndirect() || isNew(object)) {
        return;
    }
    const QPDFObjGen og = object.getObjGen();
    if (touched.count(og)) {
        return;
    }
    touched[og] = Before{textOf(object), false};
}

void Update::touchData(QPDFObjectHandle stream) {
    if (!stream.isIndirect() || isNew(stream)) {
        return;
    }
    touch(stream);
    touched[stream.getObjGen()].data = true;
}

std::string Update::serialize(const Tail& tail, Stats* stats) {
    // What is written: the changed objects of the file, and the new objects they reach
    std::map<QPDFObjGen, OH> written;
    std::vector<OH> queue;
    Stats st;
    for (const auto& [og, before]: touched) {
        OH o = pdf.getObjectByObjGen(og);
        if (before.data || textOf(o) != before.text) {
            written[og] = o;
            queue.push_back(o);
            ++st.changed;
        }
    }
    OH trailer = pdf.getTrailer();
    OH root = trailer.getKey("/Root");
    OH info = trailer.getKey("/Info");
    if (!root.isIndirect()) {
        throw std::runtime_error("The document catalog is not an indirect object.");
    }
    for (OH o: {root, info}) {
        if (isNew(o) && !written.count(o.getObjGen())) {
            written[o.getObjGen()] = o;
            queue.push_back(o);
        }
    }
    while (!queue.empty()) {
        OH o = queue.back();
        queue.pop_back();
        std::vector<OH> refs;
        referencesOf(o, refs);
        for (OH r: refs) {
            if (isNew(r) && !written.count(r.getObjGen())) {
                if (r.isNull()) {
                    continue;  // (a reference to nothing: written as null by the reader)
                }
                written[r.getObjGen()] = r;
                queue.push_back(r);
            }
        }
    }
    int nextId = this->nextId;
    for (const auto& [og, o]: written) {
        nextId = std::max(nextId, og.getObj());
    }
    ++nextId;

    std::string out;
    if (!tail.eol) {
        out += "\n";
    }
    const uint64_t base = tail.size;
    struct Entry {
        int type = 1;           ///< 1: at an offset, 2: in an object stream
        uint64_t offset = 0;    ///< type 1; type 2: the object stream's number
        int gen = 0;            ///< type 1; type 2: the index in it
    };
    std::map<int, Entry> xref;
    std::vector<OH> packed;  // (into object streams)
    for (auto& [og, o]: written) {
        const bool standIn = streamData.count(og.getObj()) && og.getObj() > maxId;
        if (!o.isStream() && !standIn) {
            if (tail.xrefStream && og.getGen() == 0) {
                packed.push_back(o);
                continue;
            }
            xref[og.getObj()] = {1, base + out.size(), og.getGen()};
            out += std::to_string(og.getObj()) + " " + std::to_string(og.getGen()) + " obj\n" + o.unparseResolved() +
                   "\nendobj\n";
            continue;
        }
        ++st.streams;
        OH dict = standIn ? o.shallowCopy() : o.getDict().shallowCopy();
        std::string data = standIn ? streamData[og.getObj()] : rawData(o);
        OH filter = dict.getKey("/Filter");
        const bool metadata = dict.getKey("/Type").isName() && dict.getKey("/Type").getName() == "/Metadata";
        if ((filter.isNull() || (filter.isArray() && filter.getArrayNItems() == 0)) && !metadata && data.size() > 64) {
            data = deflate(data);
            dict.replaceKey("/Filter", OH::newName("/FlateDecode"));
            if (dict.hasKey("/DecodeParms")) {
                dict.removeKey("/DecodeParms");
            }
        }
        dict.replaceKey("/Length", OH::newInteger(static_cast<long long>(data.size())));
        xref[og.getObj()] = {1, base + out.size(), og.getGen()};
        // (an end of line before "endstream", which PDF/A wants and /Length does not count)
        out += std::to_string(og.getObj()) + " " + std::to_string(og.getGen()) + " obj\n" + dict.unparse() +
               "\nstream\n" + data + "\nendstream\nendobj\n";
    }
    // Object streams (a cross-reference stream only), 100 objects each, as qpdf writes them
    for (size_t first = 0; first < packed.size(); first += 100) {
        const size_t last = std::min(packed.size(), first + 100);
        const int id = nextId++;
        std::string header, body;
        for (size_t i = first; i < last; ++i) {
            header += std::to_string(packed[i].getObjectID()) + " " + std::to_string(body.size()) + " ";
            body += packed[i].unparseResolved() + "\n";
            xref[packed[i].getObjectID()] = {2, static_cast<uint64_t>(id), static_cast<int>(i - first)};
        }
        header += "\n";
        const std::string data = deflate(header + body);
        xref[id] = {1, base + out.size(), 0};
        out += std::to_string(id) + " 0 obj\n<< /Type /ObjStm /N " + std::to_string(last - first) + " /First " +
               std::to_string(header.size()) + " /Filter /FlateDecode /Length " + std::to_string(data.size()) +
               " >>\nstream\n" + data + "\nendstream\nendobj\n";
    }
    st.added = written.size() - st.changed;

    // The trailer
    long long size = nextId;
    if (OH old = trailer.getKey("/Size"); old.isInteger()) {
        size = std::max(size, old.getIntValue());
    }
    OH ids = OH::newArray();
    {
        OH old = trailer.getKey("/ID");
        const std::string fresh = newId(tail.size);
        if (old.isArray() && old.getArrayNItems() >= 1 && old.getArrayItem(0).isString()) {
            ids.appendItem(OH::newString(old.getArrayItem(0).getStringValue()));
        } else {
            ids.appendItem(OH::newString(fresh));
        }
        ids.appendItem(OH::newString(newId(tail.size + 1)));
    }
    OH dict = OH::newDictionary();
    dict.replaceKey("/Root", root);
    if (info.isIndirect()) {
        dict.replaceKey("/Info", info);
    }
    dict.replaceKey("/ID", ids);
    dict.replaceKey("/Prev", OH::newInteger(static_cast<long long>(tail.startxref)));
    const uint64_t xrefAt = base + out.size();
    if (!tail.xrefStream) {
        dict.replaceKey("/Size", OH::newInteger(std::max<long long>(size, nextId)));
        out += "xref\n";
        for (auto it = xref.begin(); it != xref.end();) {
            auto end = it;
            int n = 0;
            for (int id = it->first; end != xref.end() && end->first == id; ++end, ++id) {
                ++n;
            }
            out += std::to_string(it->first) + " " + std::to_string(n) + "\n";
            for (; it != end; ++it) {
                char line[24];
                std::snprintf(line, sizeof line, "%010llu %05d n\r\n", static_cast<unsigned long long>(it->second.offset),
                              it->second.gen);
                out += line;
            }
        }
        out += "trailer\n" + dict.unparse() + "\nstartxref\n" + std::to_string(xrefAt) + "\n%%EOF\n";
    } else {
        const int id = nextId++;
        xref[id] = {1, xrefAt, 0};
        size = std::max<long long>(size, nextId);
        uint64_t widest = 0;
        for (const auto& [n, e]: xref) {
            widest = std::max(widest, e.offset);
        }
        const int w = widthFor(widest);
        std::string rows;
        OH index = OH::newArray();
        for (auto it = xref.begin(); it != xref.end();) {
            auto end = it;
            int n = 0;
            for (int k = it->first; end != xref.end() && end->first == k; ++end, ++k) {
                ++n;
            }
            index.appendItem(OH::newInteger(it->first));
            index.appendItem(OH::newInteger(n));
            for (; it != end; ++it) {
                putField(rows, static_cast<uint64_t>(it->second.type), 1);
                putField(rows, it->second.offset, w);
                putField(rows, static_cast<uint64_t>(it->second.gen), 2);
            }
        }
        const std::string data = deflate(rows);
        dict.replaceKey("/Type", OH::newName("/XRef"));
        dict.replaceKey("/Size", OH::newInteger(size));
        dict.replaceKey("/Index", index);
        dict.replaceKey("/W", OH::parse("[1 " + std::to_string(w) + " 2]"));
        dict.replaceKey("/Filter", OH::newName("/FlateDecode"));
        dict.replaceKey("/Length", OH::newInteger(static_cast<long long>(data.size())));
        out += std::to_string(id) + " 0 obj\n" + dict.unparse() + "\nstream\n" + data + "\nendstream\nendobj\n";
        out += "startxref\n" + std::to_string(xrefAt) + "\n%%EOF\n";
    }
    st.bytes = out.size();
    if (stats) {
        *stats = st;
    }
    return out;
}

Result append(const fs::path& file, const Tail& tail, const std::string& update) {
    Result r;
    std::error_code ec;
    const auto size = fs::file_size(file, ec);
    if (ec || size != tail.size) {
        r.error = "The file was changed meanwhile.";
        return r;
    }
    // Temporary files of this file left by a crash (older than ten minutes): removed, the folder stays clean
    {
        const std::string prefix = "." + file.filename().string() + ".";
        const auto now = fs::file_time_type::clock::now();
        for (auto it = fs::directory_iterator(file.parent_path(), ec); !ec && it != fs::directory_iterator();
             it.increment(ec)) {
            const std::string n = it->path().filename().string();
            std::error_code tec;
            if (n.rfind(prefix, 0) == 0 && it->path().extension() == ".part" &&
                now - fs::last_write_time(it->path(), tec) > std::chrono::minutes(10) && !tec) {
                fs::remove(it->path(), tec);
            }
        }
        ec.clear();
    }
    const fs::path part = partOf(file);
    auto fail = [&](const std::string& why) {
        std::error_code rec;
        fs::remove(part, rec);
        r.ok = false;
        r.error = "Could not write \"" + file.string() + "\": " + why;
        return r;
    };
    // A copy of the file (copy_file_range or a reflink where the file system has one) plus the update
    fs::copy_file(file, part, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        return fail(ec.message());
    }
#ifdef _WIN32
    std::FILE* f = _wfopen(part.wstring().c_str(), L"ab");
#else
    std::FILE* f = std::fopen(part.string().c_str(), "ab");
#endif
    if (!f) {
        return fail("cannot open a temporary file next to it");
    }
    uint64_t done = 0;
    bool ok = true;
    while (ok && done < update.size()) {
        if (failWriteAt && failWriteAt(done)) {
            ok = false;
            break;
        }
        const size_t n = std::min<size_t>(update.size() - done, 64 * 1024);
        ok = std::fwrite(update.data() + done, 1, n, f) == n;
        done += n;
    }
    if (ok && failWriteAt && failWriteAt(done)) {
        ok = false;
    }
    ok = ok && std::fflush(f) == 0;
#ifdef _WIN32
    ok = ok && _commit(_fileno(f)) == 0;
#else
    ok = ok && fsync(fileno(f)) == 0;  // (on the disk before it replaces the file)
#endif
    ok = (std::fclose(f) == 0) && ok;
    if (!ok) {
        return fail("the update could not be written");
    }
    fs::rename(part, file, ec);
    if (ec) {
        return fail(ec.message());
    }
    r.ok = true;
    r.size = tail.size + update.size();
    return r;
}

}  // namespace xqt::IncrementalPdf
