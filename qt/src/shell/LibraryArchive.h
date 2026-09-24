/*
 * xournal-qt: "Export library as archive…" (qt/docs/hybrid-pdf.md, "Archive PDF").
 *
 * Every document of a library (or of one of its folders, with its subfolders) as an archive PDF (PDF/A-3b,
 * HybridPdf::writeArchive) in a new folder "<name> archive <date>" inside a folder the user chose, keeping the folder
 * structure. Other files (images, Markdown, text and all others) are copied as they are; a README.txt says what the
 * files are and how to open them again. Links between documents lead to their archive PDFs. It runs on a worker,
 * with progress, and can be cancelled; it never writes into the library.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <QObject>
#include <QString>
#include <QVariantMap>

#include "filesystem.h"

namespace xqt {

class LibraryArchive: public QObject {
    Q_OBJECT
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(int done READ done NOTIFY progressChanged)
    Q_PROPERTY(int total READ total NOTIFY progressChanged)
    Q_PROPERTY(QString current READ current NOTIFY progressChanged)
public:
    /// What to do: each document written as an archive PDF, each other file copied.
    struct Plan {
        fs::path source;  ///< the library or folder exported
        fs::path target;  ///< the new folder the archive goes into
        std::string name; ///< the library's (or folder's) name, for the README
        struct Document {
            fs::path file;                  ///< the file to open (a .xopp, a PDF with notes, a PDF)
            std::vector<fs::path> sources;  ///< its files that links may point at (the .xopp, its PDF)
            fs::path archive;               ///< the archive PDF
        };
        std::vector<Document> documents;
        std::vector<std::pair<fs::path, fs::path>> copies;  ///< other files: from, to
    };
    struct Summary {
        int archived = 0;  ///< archive PDFs written
        int pdfa = 0;      ///< of them PDF/A-3b
        int copied = 0;    ///< other files copied
        std::vector<std::pair<fs::path, std::vector<std::string>>> notPdfA;  ///< archive PDF (relative), why
        std::vector<std::pair<fs::path, std::string>> failed;                 ///< file (relative), why
        bool cancelled = false;
        fs::path target;
    };

    /// The plan for exporting `source` (a library root or one of its folders) into a new folder inside `into`. Empty
    /// `error` on success. It is refused when `into` is inside `library` (the archive never goes into the library).
    static Plan plan(const fs::path& source, const fs::path& into, const fs::path& library, const std::string& name,
                     std::string& error);
    /// Carry it out on this thread. `progress(done, total, current)` after each step; `cancel` is looked at between
    /// files.
    static Summary run(const Plan& plan, const std::atomic<bool>& cancel,
                       const std::function<void(int, int, const fs::path&)>& progress = {});

    explicit LibraryArchive(QObject* parent = nullptr);
    ~LibraryArchive() override;

    bool running() const { return state != nullptr; }
    int done() const { return doneCount; }
    int total() const { return totalCount; }
    QString current() const { return currentName; }

    /// Export `source` into a new folder inside `into` on a worker. False (with `error`) if it cannot start.
    bool start(const fs::path& source, const fs::path& into, const fs::path& library, const std::string& name,
               std::string& error);
    void cancel();

Q_SIGNALS:
    void runningChanged();
    void progressChanged();
    /// Done or cancelled: "target", "archived", "pdfa", "copied", "notPdfA" (list of "file: reason; reason"), "failed"
    /// (list of "file: reason"), "cancelled".
    void finished(const QVariantMap& summary);

private:
    struct State;
    std::shared_ptr<State> state;
    int doneCount = 0, totalCount = 0;
    QString currentName;
};

}  // namespace xqt
