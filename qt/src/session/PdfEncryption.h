/*
 * xournal-qt: encrypted PDFs (qt/docs/features/hybrid-pdf.md, "Encrypted PDFs").
 *
 * - What a PDF's encryption is (probe): whether it needs a password to open (a user password), whether one given
 *   is right, and what it allows (printing, copying, editing) when it is opened without its owner password.
 * - Protecting a PDF: AES-256 (V5, R6) through qpdf, with a password to open and optional restrictions.
 * - The passwords of the files open in this process, in memory only (never in settings, recent files, logs or crash
 *   reports): the files of open documents and the files made from them in the app cache (the clean copy of a PDF
 *   with notes, a merged PDF of pasted pages, a version cut out of the file, an autosave), each encrypted with the
 *   same key. Only code that works for an open document opens files through it (openQpdf); the
 *   library, previews of cards, tags and the search index read files without a password, so they never hold the
 *   content of a protected PDF.
 * - Appending to an encrypted file (IncrementalPdf): the file's key and how strings and streams are encrypted.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "filesystem.h"

class QPDF;
class QPDFWriter;
class InputSource;
class XojPdfDocument;

namespace xqt::PdfEncryption {

/// What a PDF's encryption is, and whether a password opens it.
struct Status {
    bool readable = false;       ///< it opens: not encrypted, only an owner password, or the password given is right
    bool encrypted = false;
    bool needsPassword = false;  ///< opening it needs a password (a user password is set)
    bool wrongPassword = false;  ///< a password was given and does not open it
    bool owner = false;          ///< opened with the owner password (no restrictions apply)
    int revision = 0;            ///< the security handler's revision (6: AES-256)
    bool aes256 = false;         ///< AES-256 (V5)
    // What it allows when opened without the owner password
    bool allowPrint = true;
    bool allowCopy = true;
    bool allowModify = true;     ///< changing it (annotations, forms, pages)
    std::string error;           ///< not a PDF, or it cannot be read (not about the password)
    /// Protected: a password is needed to open it (what the app treats as confidential)
    bool isProtected() const { return encrypted && needsPassword; }
};

/// Open `pdf` (with `password`; empty: none) far enough to know its encryption (the cross-reference table and the
/// trailer; not the pages).
Status probe(const fs::path& pdf, const std::string& password = {});

/// A protection to write: AES-256 (R6).
struct Protection {
    std::string password;       ///< to open it; never empty
    /// To change the restrictions (needed when there are any, and different from `password`). Empty without
    /// restrictions: a random one nobody knows (the restrictions are none, there is nothing to lift).
    std::string ownerPassword;
    bool allowPrint = true;
    bool allowCopy = true;
    bool allowEdit = true;
    bool restricted() const { return !allowPrint || !allowCopy || !allowEdit; }
};

/// Why a protection cannot be written (empty: it can).
std::string check(const Protection& p);

/// How a file is written with respect to encryption.
struct Encryption {
    enum class Kind {
        Keep,     ///< as the source being written has it (qpdf's default)
        CopyOf,   ///< as the file `from` has it (opened with its password from the registry)
        Set,      ///< `protection`
        None,     ///< not encrypted
    };
    Kind kind = Kind::Keep;
    fs::path from;
    Protection protection;
};
/// Set up `w` (writing `source`) for `how`.
void apply(QPDFWriter& w, QPDF& source, const Encryption& how);

/// Write `pdf` (opened with `password`) again as `target` (may be `pdf`; atomic: a temporary file renamed over it)
/// with `protection` (nullptr: without encryption). The whole file is written anew: earlier revisions go. `strip`:
/// called on the opened PDF before it is written (e.g. to drop a version history). False with `error`.
bool rewrite(const fs::path& pdf, const std::string& password, const fs::path& target, const Protection* protection,
             std::string& error, const std::function<void(QPDF&)>& strip = {});

// --- The passwords of this process (in memory only) ------------------------------------------------------------

/// `file` opens with `password` (a document opened in this process). `owner`: the document's file the password
/// belongs to (`file` itself when empty); forget(owner) forgets every file of it.
void remember(const fs::path& file, const std::string& password, const fs::path& owner = {});
/// `derived` (made from `from`, e.g. its clean copy in the cache) has the same password as `from`. Nothing when
/// `from` has none.
void derive(const fs::path& derived, const fs::path& from);
/// `file` has no password any more (it was removed): forget it, not the files made from it (they keep theirs).
void unset(const fs::path& file);
/// Forget the password of `owner` and of every file made from it (the last document of it was closed). The memory
/// of the password is overwritten.
void forget(const fs::path& owner);
/// Whether this process knows a password for `file` (it is a protected file of an open document, or made from one).
bool isKnown(const fs::path& file);
/// The password known for `file` (empty: none).
std::string passwordOf(const fs::path& file);
/// The document file `file` belongs to (empty: none known).
fs::path ownerOf(const fs::path& file);
/// Keep the password of `owner` (and its files) known while the returned hold lives: the last hold of it forgets them
/// (an open document holds its file; a load result holds it until the document has a session).
std::shared_ptr<void> hold(const fs::path& owner);
/// LoadHandler::pdfPassword: the password of the background PDF of a .xopp being loaded. The PDF asked for is
/// remembered on this thread (askedBackground) so that loading can ask for its password.
std::string backgroundPassword(const fs::path& pdf);
fs::path& askedBackground();
/// An open document of this file is protected: caches that would keep its content on disk leave it out.
bool isProtected(const fs::path& file);

/// Open `file` with qpdf, with the password known for it (none: as an ordinary file). Throws as processFile does.
void openQpdf(QPDF& q, const fs::path& file);
/// The same through an input source (a prefix of the file: a version of it).
void openQpdf(QPDF& q, const std::shared_ptr<InputSource>& source, const fs::path& file);
/// Whether qpdf failed because the password was missing or wrong.
bool isPasswordError(const std::exception& e);

// --- Appending to an encrypted file (IncrementalPdf) ---------------------------------------------------------

/// How the objects of an update to an encrypted file are encrypted: AES-256 (V5, R5 or R6, the standard crypt
/// filter for strings and streams) only; files with another encryption are written in full.
class Encrypter {
public:
    /// For `pdf` (opened with its password). `supported()` false: not encrypted, or not with AES-256.
    explicit Encrypter(QPDF& pdf);
    bool encrypted() const { return on; }
    /// Encrypted and appendable (AES-256 with the standard filters).
    bool supported() const { return on && ok; }
    /// Why not (when encrypted and not supported).
    const std::string& why() const { return reason; }
    /// The bytes encrypted (a random initialisation vector, AES-256-CBC, PKCS#5 padding).
    std::string encrypt(const std::string& plain) const;
    /// Whether metadata streams are encrypted (/EncryptMetadata).
    bool encryptMetadata() const { return metadata; }

private:
    bool on = false;
    bool ok = false;
    bool metadata = true;
    std::string key;
    std::string reason;
};

}  // namespace xqt::PdfEncryption
