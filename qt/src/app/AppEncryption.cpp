/*
 * xournal-qt: encrypted PDFs in the window (qt/docs/hybrid-pdf.md, "Encrypted PDFs"): the password asked for when a
 * protected PDF is opened (or a protected document's autosave is recovered), and protecting the current document's
 * PDF with a password, changing it or removing it.
 *
 * @license GNU GPLv2 or later
 */
#include <algorithm>
#include <cctype>
#include <system_error>

#include <qpdf/QPDF.hh>

#include "AppController.h"
#include "model/Document.h"
#include "session/DocumentImages.h"
#include "session/DocumentSession.h"
#include "session/HybridPdf.h"
#include "session/PdfEncryption.h"
#include "session/VersionCache.h"
#include "shell/DocumentFiles.h"
#include "shell/Library.h"
#include "shell/LibraryModel.h"
#include "shell/Previews.h"
#include "shell/PageSketches.h"
#include "shell/TabManager.h"
#include "shell/Thumbnails.h"
#include "util/PathUtil.h"

using namespace xqt;

namespace {
bool isPdf(const fs::path& p) {
    std::string e = p.extension().string();
    std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return e == ".pdf";
}

void scrub(std::string& s) {
    std::fill(s.begin(), s.end(), '\0');
    s.clear();
}
}  // namespace

bool AppController::protectedDocument() const {
    DocumentSession* s = session();
    return s && s->isProtected();
}

bool AppController::canProtect() const {
    DocumentSession* s = session();
    if (!s || s->textFile()) {
        return false;
    }
    const fs::path file = s->documentFile();
    // (an archive PDF stays PDF/A, which allows no encryption)
    return isPdf(file) && !HybridPdf::isArchive(file);
}

bool AppController::printAllowed() const {
    DocumentSession* s = session();
    return !s || s->allowsPrinting();
}

bool AppController::copyAllowed() const {
    DocumentSession* s = session();
    return !s || s->allowsCopying();
}

void AppController::askPassword(PendingPassword pending) {
    pendingPasswords.push_back(std::move(pending));
    if (pendingPasswords.size() == 1) {
        const PendingPassword& p = pendingPasswords.front();
        const fs::path& shown = p.passwordFile.empty() ? p.file : p.passwordFile;
        Q_EMIT passwordNeeded(QString::fromStdString(shown.filename().string()), false);
    }
}

bool AppController::openWithPassword(const QString& password) {
    if (pendingPasswords.empty()) {
        return false;
    }
    const PendingPassword p = pendingPasswords.front();
    const fs::path& asked = p.passwordFile.empty() ? p.file : p.passwordFile;
    std::string pw = password.toStdString();
    auto result = DocumentSession::loadFile(p.file, false, pw);
    scrub(pw);
    if (result.needsPassword) {
        Q_EMIT passwordNeeded(QString::fromStdString(asked.filename().string()), true);  // (asked again)
        return false;
    }
    pendingPasswords.pop_front();
    bool ok = false;
    if (p.recovering) {
        // A protected document's autosave: recovered as unsaved changes of its document
        if (result.document) {
            tabs->addTab(std::make_unique<DocumentSession>(*app, std::move(result.document)));
            DocumentSession* s = tabs->currentSession();
            s->setPermissions(result.allowPrint, result.allowCopy);
            s->markRecovered(p.recoverTo);
            setHomeVisible(false);
            ok = true;
        } else {
            Q_EMIT message(tr("Cannot recover"), QString::fromStdString(result.error), true);
        }
        std::error_code ec;
        fs::remove(p.file, ec);
    } else {
        ok = openLoaded(p.file, p.path, std::move(result), p.shown);
    }
    if (!pendingPasswords.empty()) {
        const PendingPassword& next = pendingPasswords.front();
        const fs::path& shown = next.passwordFile.empty() ? next.file : next.passwordFile;
        Q_EMIT passwordNeeded(QString::fromStdString(shown.filename().string()), false);
    }
    return ok;
}

void AppController::cancelPassword() {
    if (pendingPasswords.empty()) {
        return;
    }
    const PendingPassword p = pendingPasswords.front();
    pendingPasswords.pop_front();
    if (p.recovering) {
        std::error_code ec;
        fs::remove(p.file, ec);  // (not recovered, as when recovery is declined)
    }
    if (!pendingPasswords.empty()) {
        const PendingPassword& next = pendingPasswords.front();
        const fs::path& shown = next.passwordFile.empty() ? next.file : next.passwordFile;
        Q_EMIT passwordNeeded(QString::fromStdString(shown.filename().string()), false);
    }
}

QString AppController::checkProtection(const QString& password, const QString& ownerPassword, bool allowPrint,
                                       bool allowCopy, bool allowEdit) const {
    PdfEncryption::Protection p;
    p.password = password.toStdString();
    p.ownerPassword = ownerPassword.toStdString();
    p.allowPrint = allowPrint;
    p.allowCopy = allowCopy;
    p.allowEdit = allowEdit;
    const std::string why = PdfEncryption::check(p);
    scrub(p.password);
    scrub(p.ownerPassword);
    if (why.empty()) {
        return {};
    }
    // (the same reasons, translated)
    if (password.isEmpty()) {
        return tr("Choose a password.");
    }
    if (ownerPassword.isEmpty()) {
        return tr("Restrictions need a second password, to change them later.");
    }
    return tr("The password for the restrictions must differ from the password to open it.");
}

bool AppController::protectDocument(const QString& password, const QString& ownerPassword, bool allowPrint,
                                    bool allowCopy, bool allowEdit) {
    PdfEncryption::Protection p;
    p.password = password.toStdString();
    p.allowPrint = allowPrint;
    p.allowCopy = allowCopy;
    p.allowEdit = allowEdit;
    p.ownerPassword = p.restricted() ? ownerPassword.toStdString() : std::string();  // (else a random one)
    if (const QString why = checkProtection(password, ownerPassword, allowPrint, allowCopy, allowEdit); !why.isEmpty()) {
        Q_EMIT message(tr("Not protected"), why, true);
        return false;
    }
    const bool ok = applyProtection(session(), &p);
    scrub(p.password);
    scrub(p.ownerPassword);
    return ok;
}

bool AppController::removeProtection() { return applyProtection(session(), nullptr); }

bool AppController::applyProtection(DocumentSession* s, const PdfEncryption::Protection* protection) {
    if (!s || s != session() || !canProtect()) {
        return false;
    }
    const fs::path file = s->documentFile();
    const QString name = QString::fromStdString(file.filename().string());
    const QString failed = protection ? tr("Could not protect %1").arg(name) : tr("Could not remove the password");
    // Unsaved changes go into the PDF first (it is the file that is protected); a PDF without notes stays one
    if (s->isModified()) {
        const auto r = s->isHybrid() ? s->save() : s->saveAsHybrid(file);
        if (!r.ok) {
            Q_EMIT message(failed, QString::fromStdString(r.error), true);
            return false;
        }
    }
    s->waitForSaves();
    // The whole file written anew (earlier revisions, and with them the versions, cannot stay: they would keep the
    // old password, or none)
    std::string error;
    const std::string old = PdfEncryption::passwordOf(file);
    if (!PdfEncryption::rewrite(file, old, file, protection, error, [](QPDF& q) { HybridPdf::forgetHistory(q); })) {
        Q_EMIT message(failed, QString::fromStdString(error), true);
        return false;
    }
    // In memory: the new password (the files made from the old file keep theirs), or none
    if (protection) {
        PdfEncryption::remember(file, protection->password);
    } else {
        PdfEncryption::unset(file);
    }
    // Tabs that show versions of it (cut out of the file before, with no password or another one): closed
    for (int i = tabs->count() - 1; i >= 0; --i) {
        DocumentSession* t = tabs->session(i);
        if (t && t != s && VersionCache::instance().sourceOf(t->documentFile()) == file) {
            closeTab(i);
        }
    }
    if (!reloadDocument(s)) {
        return false;
    }
    // Nothing the app made of it before stays in its caches: written with no password or another one (also when the
    // password is changed or removed: they belong to a file that is gone)
    forgetDerivatives(file, protection != nullptr);
    library->refresh();  // (its card: a lock, its text out of the index; or read again)
    Q_EMIT titleChanged();
    Q_EMIT pageActionDone(protection ? tr("%1 is protected with a password").arg(name)
                                     : tr("The password of %1 was removed").arg(name),
                          false);
    return true;
}

void AppController::forgetDerivatives(const fs::path& file, bool locked) {
    std::error_code ec;
    PageSketches::instance().forgetFile(file);  // (the pictures of its pages, every version)
    HybridPdf::forgetCopies(file);              // (clean copies with their .xopp, pictures and recordings)
    DocumentSession::forgetOriginal(file);      // (the original kept in PDF files mode)
    VersionCache::instance().forget(file);      // (versions cut out of it)
    fs::remove_all(DocumentImages::workFolder(file), ec);  // (the pictures its Markdown carried)
    const fs::path share = Util::getCacheSubfolder("share");  // (copies shared from it)
    fs::remove(share / file.filename(), ec);
    const DocumentItem item = DocumentFiles::itemOf(file);
    PreviewCache::forget(item);  // (its card's picture, in the library's pack or for Recent)
    PreviewCache::flush();
    if (LibraryIndex* index = library->searchIndex(); index && locked) {
        index->documentProtected(file);  // (its text, title, tags, handwriting: an empty, locked entry)
        index->flush();
    }
}

bool AppController::sharePdfProtected(const QString& password, bool toClipboard) {
    DocumentSession* s = session();
    if (!s || (s->textFile() && !s->hasFilePath()) || password.isEmpty()) {
        return false;
    }
    PdfEncryption::Encryption how;
    how.kind = PdfEncryption::Encryption::Kind::Set;
    how.protection.password = password.toStdString();
    // In the app cache (the clipboard or a share sheet takes it from there), named like the document
    fs::path name(s->getDisplayName());
    name.replace_extension(".pdf");
    const fs::path copy = Util::getCacheSubfolder("share") / name;
    nextSaveEncryption = how;
    scrub(how.protection.password);
    QPointer<AppController> guard(this);
    const bool started = startSave(SaveWay::ExportHybrid, copy, [guard, copy, toClipboard](bool ok) {
        if (ok && guard) {
            guard->handOver({QString::fromStdString(copy.string())}, toClipboard);
        }
    });
    if (nextSaveEncryption) {  // (not started: not for a later save)
        scrub(nextSaveEncryption->protection.password);
        nextSaveEncryption.reset();
    }
    return started;
}
