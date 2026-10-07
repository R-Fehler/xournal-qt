/*
 * xournal-qt: "Share folder…" / "Share library…": a folder of the library (with its subfolders) or the whole library
 * as one zip (qt/docs/features/library.md, "Sharing a folder or the library").
 *
 * Three formats:
 *  - xournal-qt (as is): the documents as they are, with the library's readings of exactly these documents written
 *    fresh into the zip's cache folders (".xournal_library/" packs: the handwriting read, the covers, the
 *    notes with the text of text elements, and on request the PDF text), so the recipient's search is fast at once.
 *    Nothing else of the cache goes along: no entries of other documents, no autosaves, no stale entries;
 *  - for Xournal++: notes as .xopp + PDF (a PDF with notes becomes "name.xopp" + "name.xopp.bg.pdf");
 *  - plain PDFs: every notes document as a PDF with the ink drawn into the pages.
 * Markdown files, images, text and other files go as they are in every format. PDFs with notes go without their
 * version history (a compacted copy, as Share sends them) unless it is asked for; recordings go along unless they
 * are left out; PDFs protected with a password go as they are (encrypted). Files outside the folder that documents
 * need (a .xopp's PDF, a picture of a page background or of a Markdown file) go into "_attached/" in the zip, with
 * the paths in the copies rewritten (never in the user's files); links to documents outside the folder are listed.
 * Every entry carries its UTC time (the extended timestamp field) and a manifest with each file's size and content
 * hash is in the zip's top cache folder. An optional password encrypts the zip (AES-256; Zip::aesAvailable()).
 *
 * It runs on a worker at low priority, with progress and Cancel; libzip writes the zip into a temporary file next to
 * it and renames it at the end. The zip goes into the app cache ("share/", like Share's copies; zips older than a day
 * are removed when the next one is written), from where it is shown in the file manager or saved elsewhere.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <QObject>
#include <QString>
#include <QVariantMap>

#include "DocumentFiles.h"
#include "FileStamps.h"
#include "LibraryIndex.h"
#include "filesystem.h"

namespace xqt {

class LibraryShare: public QObject {
    Q_OBJECT
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(int done READ done NOTIFY progressChanged)
    Q_PROPERTY(int total READ total NOTIFY progressChanged)
    Q_PROPERTY(QString current READ current NOTIFY progressChanged)
    /// What the folder holds (survey()): "documents", "files", "bytes", "recordings", "recordingBytes"; empty while it
    /// is looked at.
    Q_PROPERTY(QVariantMap survey READ surveyMap NOTIFY surveyChanged)
    /// A zip password can be set here (Zip::aesAvailable()).
    Q_PROPERTY(bool passwordAvailable READ passwordAvailable CONSTANT)
public:
    enum class Format { App, Xournal, Pdf };
    struct Options {
        Format format = Format::App;
        bool readings = true;     ///< handwriting readings, covers, notes (xournal-qt format)
        bool pdfText = false;     ///< also the PDF text ("faster search, bigger file")
        bool history = false;     ///< PDFs with notes with their version history
        bool recordings = true;
        std::string password;     ///< "": no password
    };
    /// "app", "xournal", "pdf" (QML)
    static Format formatNamed(const QString& name);

    struct Plan {
        fs::path source;   ///< the folder shared
        fs::path library;  ///< the library it is in
        std::string name;  ///< the zip's top folder (and file name)
        fs::path zip;      ///< where the zip is written
        Options options;
        std::vector<DocumentItem> items;  ///< every document and file in it (all kinds)
        std::vector<fs::path> folders;    ///< its subfolders (also empty ones)
        /// The library's cache of each document as it is now, by its main file (xournal-qt format with readings)
        std::map<fs::path, LibraryIndex::Snapshot> cache;
    };
    struct Summary {
        fs::path zip;
        uint64_t bytes = 0;     ///< of the zip
        int documents = 0;      ///< documents and files in it
        int readings = 0;       ///< documents whose readings went along
        std::vector<std::string> attached;      ///< "_attached/x.pdf (for Lectures/a.xopp)"
        std::vector<std::string> outsideLinks;  ///< "Lectures/a.md → ../../Other/b.pdf"
        std::vector<std::string> notes;         ///< "Lectures/s.pdf: protected with a password, shared as it is"
        std::vector<std::string> failed;        ///< "Lectures/x.xopp: why"
        bool cancelled = false;
        std::string error;  ///< the zip could not be written
    };
    struct Survey {
        int documents = 0, files = 0;
        uint64_t bytes = 0;
        int recordings = 0;
        uint64_t recordingBytes = 0;
    };

    /// The folder "_attached" in the zip: files outside the shared folder that its documents need.
    static constexpr const char* ATTACHED = "_attached";
    /// The manifest pack in the zip's top cache folder (".xournal_library/share-manifest.pack"): per file its size,
    /// content hash (contentHash) and UTC time; the format, the app, when, and the options.
    static const QString MANIFEST_PACK;
    static constexpr int MANIFEST_FORMAT = 1;
    /// Zips in the share folder older than this are removed when the next one is written.
    static constexpr int KEEP_HOURS = 24;

    /// The plan for sharing `source` (the library `library` or a folder in it) as `zip`. `index`: the library's index
    /// (the readings; nullptr: none). Empty `error` on success.
    static Plan plan(const fs::path& source, const fs::path& library, const std::string& name, const fs::path& zip,
                     const Options& options, LibraryIndex* index, std::string& error);
    /// Carry it out on this thread. `progress(done, total, current)`; `cancel` is looked at between files and while
    /// the zip is compressed.
    static Summary run(const Plan& plan, const std::atomic<bool>& cancel,
                       const std::function<void(int, int, const std::string&)>& progress = {});
    /// What `source` holds (the dialog shows the recordings' size). Reads the .xopp files and PDFs with notes.
    static Survey survey(const fs::path& source);
    /// Where the zips go ("<cache>/share") and the zip for `name` there.
    static fs::path zipFor(const std::string& name);

    explicit LibraryShare(QObject* parent = nullptr);
    ~LibraryShare() override;

    bool running() const { return state != nullptr; }
    int done() const { return doneCount; }
    int total() const { return totalCount; }
    QString current() const { return currentName; }
    QVariantMap surveyMap() const { return surveyed; }
    bool passwordAvailable() const;

    /// Look at `source` on a worker (surveyChanged).
    void startSurvey(const fs::path& source);
    /// Share `source` on a worker. False (with `error`) if it cannot start.
    bool start(const fs::path& source, const fs::path& library, const std::string& name, const Options& options,
               LibraryIndex* index, std::string& error);
    void cancel();

Q_SIGNALS:
    void runningChanged();
    void progressChanged();
    void surveyChanged();
    /// Done or cancelled: "zip", "bytes", "documents", "readings", "attached", "outsideLinks", "notes", "failed" (lists
    /// of strings), "cancelled", "error".
    void finished(const QVariantMap& summary);

private:
    struct State;
    std::shared_ptr<State> state;
    int doneCount = 0, totalCount = 0;
    QString currentName;
    QVariantMap surveyed;
    quint64 surveyGeneration = 0;
};

}  // namespace xqt
