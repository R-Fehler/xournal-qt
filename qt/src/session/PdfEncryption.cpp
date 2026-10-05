#include "PdfEncryption.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <map>
#include <mutex>
#include <stdexcept>
#include <system_error>

#include <glib.h>
#include <qpdf/Constants.h>
#include <qpdf/DLL.h>
#include <qpdf/InputSource.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFCryptoProvider.hh>
#include <qpdf/QPDFExc.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFWriter.hh>

#include "pdf/base/XojPdfDocument.h"
#include "util/Util.h"

namespace xqt::PdfEncryption {

namespace {

std::string randomBytes(size_t n) {
    std::string out(n, '\0');
    QPDFCryptoProvider::getImpl()->provideRandomData(reinterpret_cast<unsigned char*>(out.data()), n);
    return out;
}

/// A password nobody knows (the owner password of a file without restrictions).
std::string randomPassword() {
    static const char* digits = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    std::string bytes = randomBytes(32);
    std::string out;
    for (unsigned char c: bytes) {
        out.push_back(digits[c % 62]);
    }
    return out;
}

/// Overwrite a string's memory before it goes.
void scrub(std::string& s) {
    volatile char* p = s.data();
    for (size_t i = 0; i < s.size(); ++i) {
        p[i] = 0;
    }
    s.clear();
}

std::string keyOf(const fs::path& p) {
    std::error_code ec;
    fs::path abs = fs::absolute(p, ec);
    return (ec ? p : abs).lexically_normal().string();
}

struct Entry {
    std::string password;
    std::string owner;  ///< keyOf the document's file
};

struct Registry {
    std::mutex m;
    std::map<std::string, Entry> files;
    std::map<std::string, int> holds;  ///< by owner
};

Registry& registry() {
    static Registry* r = new Registry;  // (never destroyed: workers may still ask while the app quits)
    return *r;
}

/// A temporary name next to `target`.
fs::path partOf(const fs::path& target) {
    static std::atomic<unsigned> counter{0};
    return target.parent_path() / ("." + target.filename().string() + "." + std::to_string(Util::getPid()) + "-" +
                                   std::to_string(++counter) + ".part");
}

}  // namespace

Status probe(const fs::path& pdf, const std::string& password) {
    Status s;
    auto open = [&](QPDF& q, const std::string& pw) {
        q.setSuppressWarnings(true);
        q.processFile(pdf.string().c_str(), pw.empty() ? nullptr : pw.c_str());
    };
    QPDF q;
    try {
        open(q, password);
    } catch (const QPDFExc& e) {
        if (e.getErrorCode() == qpdf_e_password) {
            s.encrypted = true;
            s.needsPassword = true;
            s.wrongPassword = !password.empty();
            return s;
        }
        s.error = e.what();
        return s;
    } catch (const std::exception& e) {
        s.error = e.what();
        return s;
    }
    s.readable = true;
    int R = 0, P = 0, V = 0;
    QPDF::encryption_method_e streams{}, strings{}, files{};
    if (!q.isEncrypted(R, P, V, streams, strings, files)) {
        return s;
    }
    s.encrypted = true;
    s.revision = R;
    s.aes256 = V >= 5;
    s.owner = q.ownerPasswordMatched();
    if (!password.empty()) {
        // Was a password needed at all? (an owner password typed for a file that opens without one)
        try {
            QPDF plain;
            open(plain, {});
        } catch (const std::exception&) {
            s.needsPassword = true;
        }
    }
    if (!s.owner) {
        s.allowPrint = q.allowPrintLowRes();
        s.allowCopy = q.allowExtractAll();
        s.allowModify = q.allowModifyAnnotation() || q.allowModifyOther();
    }
    return s;
}

std::string check(const Protection& p) {
    if (p.password.empty()) {
        return "Choose a password.";
    }
    if (p.restricted()) {
        if (p.ownerPassword.empty()) {
            return "Restrictions need a second password (to change them).";
        }
        if (p.ownerPassword == p.password) {
            return "The password for the restrictions must differ from the password to open it.";
        }
    }
    return {};
}

void apply(QPDFWriter& w, QPDF& source, const Encryption& how) {
    (void)source;
    switch (how.kind) {
        case Encryption::Kind::Keep:
            w.setPreserveEncryption(true);
            break;
        case Encryption::Kind::None:
            w.setPreserveEncryption(false);
            break;
        case Encryption::Kind::CopyOf: {
            QPDF from;
            openQpdf(from, how.from);
            if (from.isEncrypted()) {
                w.copyEncryptionParameters(from);
            } else {
                w.setPreserveEncryption(false);
            }
            break;
        }
        case Encryption::Kind::Set: {
            const Protection& p = how.protection;
            if (const std::string why = check(p); !why.empty()) {
                throw std::runtime_error(why);
            }
            std::string owner = p.restricted() ? p.ownerPassword : randomPassword();
            w.setR6EncryptionParameters(p.password.c_str(), owner.c_str(), /*allow_accessibility=*/true,
                                        /*allow_extract=*/p.allowCopy, /*allow_assemble=*/p.allowEdit,
                                        /*allow_annotate_and_form=*/p.allowEdit, /*allow_form_filling=*/p.allowEdit,
                                        /*allow_modify_other=*/p.allowEdit, p.allowPrint ? qpdf_r3p_full : qpdf_r3p_none,
                                        /*encrypt_metadata_aes=*/true);
            scrub(owner);
            break;
        }
    }
}

bool rewrite(const fs::path& pdf, const std::string& password, const fs::path& target, const Protection* protection,
             std::string& error, const std::function<void(QPDF&)>& strip) {
    const fs::path tmp = partOf(target);
    std::error_code ec;
    try {
        QPDF q;
        q.setSuppressWarnings(true);
        q.processFile(pdf.string().c_str(), password.empty() ? nullptr : password.c_str());
        if (strip) {
            strip(q);
        }
        QPDFWriter w(q, tmp.string().c_str());
        w.setObjectStreamMode(qpdf_o_generate);
        w.setDecodeLevel(qpdf_dl_none);  // (the streams as they are: decrypted and encrypted again only)
        Encryption how;
        if (protection) {
            how.kind = Encryption::Kind::Set;
            how.protection = *protection;
        } else {
            how.kind = Encryption::Kind::None;
        }
        apply(w, q, how);
        scrub(how.protection.password);
        scrub(how.protection.ownerPassword);
        w.write();
    } catch (const std::exception& e) {
        fs::remove(tmp, ec);
        error = e.what();
        return false;
    }
    fs::rename(tmp, target, ec);
    if (ec) {
        fs::remove(tmp, ec);
        error = "Could not write \"" + target.string() + "\": " + ec.message();
        return false;
    }
    return true;
}

// --- The registry ---------------------------------------------------------------------------------------------

void remember(const fs::path& file, const std::string& password, const fs::path& owner) {
    auto& r = registry();
    std::lock_guard lock(r.m);
    Entry& e = r.files[keyOf(file)];
    scrub(e.password);
    e.password = password;
    e.owner = keyOf(owner.empty() ? file : owner);
}

void derive(const fs::path& derived, const fs::path& from) {
    auto& r = registry();
    std::lock_guard lock(r.m);
    auto it = r.files.find(keyOf(from));
    if (it == r.files.end()) {
        return;
    }
    Entry copy = it->second;
    Entry& e = r.files[keyOf(derived)];
    scrub(e.password);
    e = std::move(copy);
}

void unset(const fs::path& file) {
    auto& r = registry();
    std::lock_guard lock(r.m);
    if (auto it = r.files.find(keyOf(file)); it != r.files.end()) {
        scrub(it->second.password);
        r.files.erase(it);
    }
}

void forget(const fs::path& owner) {
    auto& r = registry();
    std::lock_guard lock(r.m);
    const std::string key = keyOf(owner);
    for (auto it = r.files.begin(); it != r.files.end();) {
        if (it->second.owner == key || it->first == key) {
            scrub(it->second.password);
            it = r.files.erase(it);
        } else {
            ++it;
        }
    }
}

std::shared_ptr<void> hold(const fs::path& owner) {
    const std::string key = keyOf(owner);
    {
        auto& r = registry();
        std::lock_guard lock(r.m);
        ++r.holds[key];
    }
    return std::shared_ptr<void>(nullptr, [key](void*) {
        auto& r = registry();
        bool last = false;
        {
            std::lock_guard lock(r.m);
            last = --r.holds[key] <= 0;
            if (last) {
                r.holds.erase(key);
            }
        }
        if (last) {
            forget(fs::path(key));
        }
    });
}

fs::path& askedBackground() {
    thread_local fs::path asked;
    return asked;
}

std::string backgroundPassword(const fs::path& pdf) {
    askedBackground() = pdf;
    return passwordOf(pdf);
}

bool isKnown(const fs::path& file) {
    auto& r = registry();
    std::lock_guard lock(r.m);
    return r.files.count(keyOf(file)) > 0;
}

std::string passwordOf(const fs::path& file) {
    if (file.empty()) {
        return {};
    }
    auto& r = registry();
    std::lock_guard lock(r.m);
    auto it = r.files.find(keyOf(file));
    return it != r.files.end() ? it->second.password : std::string();
}

fs::path ownerOf(const fs::path& file) {
    auto& r = registry();
    std::lock_guard lock(r.m);
    auto it = r.files.find(keyOf(file));
    return it != r.files.end() ? fs::path(it->second.owner) : fs::path();
}

bool isProtected(const fs::path& file) { return !file.empty() && isKnown(file); }

void openQpdf(QPDF& q, const fs::path& file) {
    std::string pw = passwordOf(file);
    q.processFile(file.string().c_str(), pw.empty() ? nullptr : pw.c_str());
    scrub(pw);
}

void openQpdf(QPDF& q, const std::shared_ptr<InputSource>& source, const fs::path& file) {
    std::string pw = passwordOf(file);
    q.processInputSource(source, pw.empty() ? nullptr : pw.c_str());
    scrub(pw);
}

bool isPasswordError(const std::exception& e) {
    const auto* q = dynamic_cast<const QPDFExc*>(&e);
    return q && q->getErrorCode() == qpdf_e_password;
}

bool loadPoppler(XojPdfDocument& pdf, const fs::path& file) {
    GError* error = nullptr;
    std::string pw = passwordOf(file);
    const bool ok = pdf.load(file, pw, &error);
    scrub(pw);
    if (error) {
        g_error_free(error);
    }
    return ok;
}

// --- Encrypter ------------------------------------------------------------------------------------------------

Encrypter::Encrypter(QPDF& pdf) {
    int R = 0, P = 0, V = 0;
    QPDF::encryption_method_e streams{}, strings{}, files{};
    if (!pdf.isEncrypted(R, P, V, streams, strings, files)) {
        return;
    }
    on = true;
    if (V < 5 || R < 5 || streams != QPDF::e_aesv3 || strings != QPDF::e_aesv3 || files != QPDF::e_aesv3) {
        reason = "the file is encrypted with an older method than AES-256";
        return;
    }
    QPDFObjectHandle encrypt = pdf.getTrailer().getKey("/Encrypt");
    if (!encrypt.isIndirect() || !encrypt.isDictionary()) {
        reason = "the encryption dictionary is not an object of its own";
        return;
    }
    if (QPDFObjectHandle m = encrypt.getKey("/EncryptMetadata"); m.isBool() && !m.getBoolValue()) {
        metadata = false;
    }
    key = QPDF::compute_data_key(pdf.getEncryptionKey(), 0, 0, true, V, R);  // (V5: the file's key itself)
    if (key.size() != 32) {
        reason = "the file's key could not be found";
        key.clear();
        return;
    }
    ok = true;
}

std::string Encrypter::encrypt(const std::string& plain) const {
    if (!supported()) {
        throw std::logic_error("Encrypter: not an AES-256 encrypted file");
    }
    constexpr size_t B = 16;
    auto aes = QPDFCryptoProvider::getImpl();
    std::string out = randomBytes(B);  // the initialisation vector, first
    unsigned char block[B];
    unsigned char prev[B];
    std::memcpy(prev, out.data(), B);
    aes->rijndael_init(true, reinterpret_cast<const unsigned char*>(key.data()), key.size(), false, nullptr);
    const size_t pad = B - plain.size() % B;  // (PKCS#5: 1 to 16 bytes, each the count)
    std::string data = plain;
    data.append(pad, static_cast<char>(pad));
    out.reserve(B + data.size());
    for (size_t at = 0; at < data.size(); at += B) {
        for (size_t i = 0; i < B; ++i) {  // CBC: the block before, mixed in
            block[i] = static_cast<unsigned char>(data[at + i]) ^ prev[i];
        }
        aes->rijndael_process(block, prev);
        out.append(reinterpret_cast<const char*>(prev), B);
    }
    aes->rijndael_finalize();
    std::fill(data.begin(), data.end(), '\0');
    return out;
}

}  // namespace xqt::PdfEncryption
