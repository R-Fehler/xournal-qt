#include "ZipFile.h"

#include <cstring>
#include <fstream>
#include <limits>

#include <zip.h>

namespace xqt::Zip {

namespace {

constexpr zip_uint16_t EXTENDED_TIMESTAMP = 0x5455;
constexpr unsigned UNIX_TYPE_MASK = 0170000;
constexpr unsigned UNIX_SYMLINK = 0120000;

#if defined(LIBZIP_VERSION_MAJOR) && (LIBZIP_VERSION_MAJOR > 1 || (LIBZIP_VERSION_MAJOR == 1 && LIBZIP_VERSION_MINOR >= 7))
#define XQT_ZIP_HAS_METHOD_SUPPORTED 1
#endif
#if defined(LIBZIP_VERSION_MAJOR) && (LIBZIP_VERSION_MAJOR > 1 || (LIBZIP_VERSION_MAJOR == 1 && LIBZIP_VERSION_MINOR >= 6))
#define XQT_ZIP_HAS_CANCEL 1
#endif

std::string errorOf(zip_t* z) {
    const char* e = z ? zip_strerror(z) : nullptr;
    return e ? std::string(e) : std::string("zip error");
}

std::string errorOf(int code) {
    zip_error_t e;
    zip_error_init_with_code(&e, code);
    std::string s = zip_error_strerror(&e);
    zip_error_fini(&e);
    return s;
}

/// Open an archive by its path (wide on Windows: libzip's char paths are in the ANSI code page there).
zip_t* openArchive(const fs::path& file, int flags, std::string& error) {
#ifdef _WIN32
    zip_error_t err;
    zip_error_init(&err);
    zip_source_t* src = zip_source_win32w_create(file.c_str(), 0, -1, &err);
    if (!src) {
        error = zip_error_strerror(&err);
        zip_error_fini(&err);
        return nullptr;
    }
    zip_t* z = zip_open_from_source(src, flags, &err);
    if (!z) {
        error = zip_error_strerror(&err);
        zip_source_free(src);
    }
    zip_error_fini(&err);
    return z;
#else
    int code = 0;
    zip_t* z = zip_open(file.c_str(), flags, &code);
    if (!z) {
        error = errorOf(code);
    }
    return z;
#endif
}

zip_source_t* fileSource(zip_t* z, const fs::path& file) {
#ifdef _WIN32
    return zip_source_win32w(z, file.c_str(), 0, -1);
#else
    return zip_source_file(z, file.c_str(), 0, -1);
#endif
}

void progressCallback(zip_t*, double done, void* state) {
    auto* w = static_cast<std::function<bool(double)>*>(state);
    if (w && *w) {
        (*w)(done);  // (cancel is asked separately)
    }
}

#ifdef XQT_ZIP_HAS_CANCEL
int cancelCallback(zip_t*, void* state) {
    auto* w = static_cast<std::function<bool(double)>*>(state);
    return w && *w && !(*w)(-1) ? 1 : 0;
}
#endif

}  // namespace

bool aesAvailable() {
#ifdef XQT_ZIP_HAS_METHOD_SUPPORTED
    return zip_encryption_method_supported(ZIP_EM_AES_256, 1) && zip_encryption_method_supported(ZIP_EM_AES_256, 0);
#else
    return false;
#endif
}

std::string extendedTimestamp(std::time_t mtime) {
    const auto t = static_cast<int64_t>(mtime);
    if (t < 0 || t > std::numeric_limits<int32_t>::max()) {
        return {};  // (the field holds 32 bits: before 1970 or after 2038 only the DOS time)
    }
    const auto u = static_cast<uint32_t>(t);
    std::string f(5, '\0');
    f[0] = 1;  // the modification time is present
    for (int i = 0; i < 4; ++i) {
        f[1 + i] = static_cast<char>((u >> (8 * i)) & 0xff);
    }
    return f;
}

// --- Writer -------------------------------------------------------------------------------------------------------

Writer::Writer(const fs::path& file) { archive = openArchive(file, ZIP_CREATE | ZIP_TRUNCATE, failure); }

Writer::~Writer() { discard(); }

void Writer::discard() {
    if (archive) {
        zip_discard(archive);
        archive = nullptr;
    }
    buffers.clear();
}

void Writer::setPassword(const std::string& pw) { password = pw; }

bool Writer::added(int64_t index, const std::string& name, std::time_t mtime, bool compress) {
    if (index < 0) {
        failure = name + ": " + errorOf(archive);
        return false;
    }
    const auto i = static_cast<zip_uint64_t>(index);
    zip_set_file_compression(archive, i, compress ? ZIP_CM_DEFLATE : ZIP_CM_STORE, 0);
    zip_file_set_mtime(archive, i, mtime, 0);
    if (const std::string ut = extendedTimestamp(mtime); !ut.empty()) {
        zip_file_extra_field_set(archive, i, EXTENDED_TIMESTAMP, ZIP_EXTRA_FIELD_NEW,
                                 reinterpret_cast<const zip_uint8_t*>(ut.data()), static_cast<zip_uint16_t>(ut.size()),
                                 ZIP_FL_LOCAL | ZIP_FL_CENTRAL);
    }
    if (!password.empty() && (name.empty() || name.back() != '/')) {
        if (zip_file_set_encryption(archive, i, ZIP_EM_AES_256, password.c_str()) < 0) {
            failure = name + ": " + errorOf(archive);
            return false;
        }
    }
    return true;
}

bool Writer::addFile(const std::string& name, const fs::path& source, std::time_t mtime, bool compress) {
    if (!ok()) {
        return false;
    }
    zip_source_t* src = fileSource(archive, source);
    if (!src) {
        failure = name + ": " + errorOf(archive);
        return false;
    }
    const zip_int64_t i = zip_file_add(archive, name.c_str(), src, ZIP_FL_ENC_UTF_8);
    if (i < 0) {
        zip_source_free(src);
    }
    return added(i, name, mtime, compress);
}

bool Writer::addData(const std::string& name, std::string data, std::time_t mtime, bool compress) {
    if (!ok()) {
        return false;
    }
    auto bytes = std::make_shared<std::string>(std::move(data));
    buffers.push_back(bytes);
    zip_source_t* src = zip_source_buffer(archive, bytes->data(), bytes->size(), 0);
    if (!src) {
        failure = name + ": " + errorOf(archive);
        return false;
    }
    const zip_int64_t i = zip_file_add(archive, name.c_str(), src, ZIP_FL_ENC_UTF_8);
    if (i < 0) {
        zip_source_free(src);
    }
    return added(i, name, mtime, compress);
}

bool Writer::addFolder(const std::string& name, std::time_t mtime) {
    if (!ok()) {
        return false;
    }
    const std::string dir = name.empty() || name.back() == '/' ? name : name + '/';
    const zip_int64_t i = zip_dir_add(archive, dir.c_str(), ZIP_FL_ENC_UTF_8);
    if (i < 0) {
        failure = dir + ": " + errorOf(archive);
        return false;
    }
    zip_file_set_mtime(archive, static_cast<zip_uint64_t>(i), mtime, 0);
    if (const std::string ut = extendedTimestamp(mtime); !ut.empty()) {
        zip_file_extra_field_set(archive, static_cast<zip_uint64_t>(i), EXTENDED_TIMESTAMP, ZIP_EXTRA_FIELD_NEW,
                                 reinterpret_cast<const zip_uint8_t*>(ut.data()), static_cast<zip_uint16_t>(ut.size()),
                                 ZIP_FL_LOCAL | ZIP_FL_CENTRAL);
    }
    return true;
}

bool Writer::close(const std::function<bool(double)>& progress) {
    if (!archive) {
        return false;
    }
    if (!failure.empty()) {
        discard();
        return false;
    }
    onProgress = progress;
    bool cancelled = false;
    std::function<bool(double)> watch = [this, &cancelled](double done) {
        if (!onProgress) {
            return true;
        }
        const bool go = onProgress(done < 0 ? -1 : done);
        cancelled = cancelled || !go;
        return go;
    };
    zip_register_progress_callback_with_state(archive, 0.005, progressCallback, nullptr, &watch);
#ifdef XQT_ZIP_HAS_CANCEL
    zip_register_cancel_callback_with_state(archive, cancelCallback, nullptr, &watch);
#endif
    if (zip_close(archive) < 0) {
        failure = cancelled ? std::string("cancelled") : errorOf(archive);
        discard();
        return false;
    }
    archive = nullptr;
    buffers.clear();
    return true;
}

// --- Reader -------------------------------------------------------------------------------------------------------

Reader::Reader(const fs::path& file) {
    archive = openArchive(file, ZIP_RDONLY, failure);
    if (!archive) {
        return;
    }
    const zip_int64_t n = zip_get_num_entries(archive, 0);
    for (zip_int64_t i = 0; i < n; ++i) {
        zip_stat_t st;
        zip_stat_init(&st);
        if (zip_stat_index(archive, static_cast<zip_uint64_t>(i), 0, &st) < 0 || !(st.valid & ZIP_STAT_NAME)) {
            continue;
        }
        Entry e;
        e.index = i;
        e.name = st.name;
        e.size = (st.valid & ZIP_STAT_SIZE) ? st.size : 0;
        e.compressed = (st.valid & ZIP_STAT_COMP_SIZE) ? st.comp_size : 0;
        e.mtime = (st.valid & ZIP_STAT_MTIME) ? st.mtime : 0;
        e.folder = !e.name.empty() && e.name.back() == '/';
        if (st.valid & ZIP_STAT_ENCRYPTION_METHOD) {
            e.encrypted = st.encryption_method != ZIP_EM_NONE;
            e.aes = st.encryption_method == ZIP_EM_AES_128 || st.encryption_method == ZIP_EM_AES_192 ||
                    st.encryption_method == ZIP_EM_AES_256;
        }
        zip_uint8_t opsys = 0;
        zip_uint32_t attributes = 0;
        if (zip_file_get_external_attributes(archive, static_cast<zip_uint64_t>(i), 0, &opsys, &attributes) == 0 &&
            opsys == ZIP_OPSYS_UNIX && ((attributes >> 16) & UNIX_TYPE_MASK) == UNIX_SYMLINK) {
            e.symlink = true;
        }
        for (zip_flags_t where: {ZIP_FL_CENTRAL, ZIP_FL_LOCAL}) {
            zip_uint16_t len = 0;
            const zip_uint8_t* f = zip_file_extra_field_get_by_id(archive, static_cast<zip_uint64_t>(i),
                                                                  EXTENDED_TIMESTAMP, 0, &len, where);
            if (f && len >= 5 && (f[0] & 1)) {
                uint32_t u = 0;
                for (int b = 0; b < 4; ++b) {
                    u |= static_cast<uint32_t>(f[1 + b]) << (8 * b);
                }
                e.mtime = static_cast<std::time_t>(static_cast<int32_t>(u));
                e.utc = true;
                break;
            }
        }
        list.push_back(std::move(e));
    }
}

Reader::~Reader() {
    if (archive) {
        zip_discard(archive);
    }
    std::fill(password.begin(), password.end(), '\0');
}

bool Reader::encrypted() const {
    for (const Entry& e: list) {
        if (e.encrypted) {
            return true;
        }
    }
    return false;
}

bool Reader::passwordWorks() {
    for (const Entry& e: list) {
        if (!e.encrypted || e.folder) {
            continue;
        }
        zip_file_t* f = zip_fopen_index_encrypted(archive, static_cast<zip_uint64_t>(e.index), 0, password.c_str());
        if (!f) {
            return false;
        }
        // AES checks the password when the file is opened (its verifier); a short read confirms it
        char buf[64];
        const zip_int64_t n = zip_fread(f, buf, sizeof buf);
        zip_fclose(f);
        return n >= 0;
    }
    return true;
}

bool Reader::extract(const Entry& entry, const fs::path& target, const std::function<bool()>& cancel,
                     uint64_t& written, std::string& error) {
    zip_file_t* f = entry.encrypted ? zip_fopen_index_encrypted(archive, static_cast<zip_uint64_t>(entry.index), 0,
                                                                password.c_str())
                                    : zip_fopen_index(archive, static_cast<zip_uint64_t>(entry.index), 0);
    if (!f) {
        error = errorOf(archive);
        return false;
    }
    std::ofstream out(target, std::ios::binary | std::ios::trunc);
    if (!out) {
        zip_fclose(f);
        error = "cannot write " + target.filename().string();
        return false;
    }
    std::vector<char> buf(256 * 1024);
    uint64_t got = 0;
    bool ok = true;
    while (true) {
        if (cancel && cancel()) {
            error = "cancelled";
            ok = false;
            break;
        }
        const zip_int64_t n = zip_fread(f, buf.data(), buf.size());
        if (n < 0) {
            error = zip_file_strerror(f);
            ok = false;
            break;
        }
        if (n == 0) {
            break;
        }
        got += static_cast<uint64_t>(n);
        if (got > entry.size) {
            error = "more data than it says";  // (a damaged or crafted zip)
            ok = false;
            break;
        }
        out.write(buf.data(), static_cast<std::streamsize>(n));
        if (!out) {
            error = "cannot write " + target.filename().string();
            ok = false;
            break;
        }
        written += static_cast<uint64_t>(n);
    }
    zip_fclose(f);
    out.close();
    if (ok && got != entry.size) {
        error = "incomplete";
        ok = false;
    }
    if (!ok) {
        std::error_code ec;
        fs::remove(target, ec);
    }
    return ok;
}

std::string Reader::read(const Entry& entry, uint64_t limit, std::string& error) {
    if (entry.size > limit) {
        error = "too big";
        return {};
    }
    zip_file_t* f = entry.encrypted ? zip_fopen_index_encrypted(archive, static_cast<zip_uint64_t>(entry.index), 0,
                                                                password.c_str())
                                    : zip_fopen_index(archive, static_cast<zip_uint64_t>(entry.index), 0);
    if (!f) {
        error = errorOf(archive);
        return {};
    }
    std::string data(static_cast<size_t>(entry.size), '\0');
    const zip_int64_t n = data.empty() ? 0 : zip_fread(f, data.data(), data.size());
    char extra = 0;
    const zip_int64_t more = zip_fread(f, &extra, 1);
    zip_fclose(f);
    if (n < 0 || static_cast<uint64_t>(n) != entry.size || more != 0) {
        error = "cannot read " + entry.name;
        return {};
    }
    return data;
}

}  // namespace xqt::Zip
