/*
 * xournal-qt: downloads a handwriting model once, with the user's consent (qt/docs/features/handwriting-search.md).
 *
 * The app ships without the models (about 64 MB for English). When the user switches the handwriting search on,
 * Settings offers to download the models its languages need, one by one, showing where from and how big first (as the
 * arXiv search shows its addresses: opt-in, the address always in view). The models this build knows are its
 * catalogue (English: TrOCR-small handwritten; German: the project's own, "not published yet" until it is pinned). A
 * model's files and their sha256 are pinned here (a revision of a Hugging Face repository); each file is
 * downloaded (NetFetch: progress, cancellable), checked against its sha256 and size, and kept in a folder next to the
 * model's ("<model>.part"): a download that failed or was cancelled starts again from the first file not done yet.
 * When all are there, the manifest (model.json: the model's own when it is one of the pinned files, as the training's
 * exports have it; else written as qt/scripts/hwr-model.sh writes it) is in place and the folder takes the model's
 * place, so the app never sees half a model. remove() takes the model away again ("Remove").
 *
 * A model goes into the app's data folder (HandwritingSearch::defaultModelDir(language)); a model chosen in Settings
 * or by XQT_HWR_MODEL / XQT_HWR_MODEL_DE is the user's and is never removed or replaced here.
 *
 * UI thread.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <vector>

#include <QObject>
#include <QString>
#include <QUrl>

namespace xqt {

class ModelDownload final: public QObject {
    Q_OBJECT
public:
    struct File {
        QString path;    ///< in the repository and in the model's folder
        QString sha256;  ///< "" while not pinned
        qint64 size = 0;
    };
    struct Model {
        QString name;      ///< the folder's name ("trocr-small-hw-int8")
        QString language;  ///< the language it is downloaded for ("en"; HandwritingSearch::Slot)
        QString kind = QStringLiteral("trocr");
        QString source;    ///< the repository's page ("https://huggingface.co/Xenova/trocr-small-handwritten")
        QString revision;  ///< a commit of it
        /// (TrOCR: what the manifest the app writes names)
        QString encoder, decoder, tokenizer;
        int start = 2, end = 2;
        std::vector<File> files;  ///< "model.json" among them: the model's own manifest (else the app writes one)
        /// Why it cannot be downloaded while not pinned (shown in Settings)
        QString unpinned;
        /// Where a file is downloaded from: <source>/resolve/<revision>/<path>
        QUrl urlOf(const File& f) const;
        qint64 bytes() const;
        /// Every file has its sha256 (else nothing is downloaded: the build does not know what to expect)
        bool pinned() const;
    };
    /// The models this build downloads (one per language); tests set others (nullptr: this build's again).
    static const std::vector<Model>& catalogue();
    static void setCatalogue(const std::vector<Model>* models);
    /// The catalogue's model for a language (null: none).
    static const Model* modelFor(const QString& language);

    enum class State { Idle, Downloading, Done, Failed, Cancelled };

    ModelDownload(Model model, QString folder, QObject* parent = nullptr);
    ~ModelDownload() override;

    /// Start (or go on after a failure or a cancel).
    void start();
    void cancel();
    State state() const { return now; }
    bool running() const { return now == State::Downloading; }
    /// Bytes done and in all (for a progress bar), the file being downloaded, why it failed.
    qint64 bytesDone() const { return done + current; }
    qint64 bytesTotal() const { return fetching.bytes(); }
    const Model& model() const { return fetching; }
    const QString& file() const { return currentFile; }
    const QString& error() const { return why; }
    const QString& folder() const { return target; }

    /// The model's folder holds a downloaded model (its manifest).
    static bool installed(const QString& folder);
    /// Remove the model (and a download not finished); false and `error` if that failed.
    static bool remove(const QString& folder, QString* error = nullptr);
    /// Bytes on disk.
    static qint64 sizeOnDisk(const QString& folder);
    /// The manifest of the downloaded files (model.json).
    static QByteArray manifestOf(const Model& model);

Q_SIGNALS:
    void changed();
    void finished(bool ok);

private:
    void next();
    void fail(const QString& reason);
    void finish();

    Model fetching;
    QString target;
    QString staging;
    State now = State::Idle;
    size_t index = 0;
    qint64 done = 0, current = 0;
    QString currentFile;
    QString why;
    quint64 generation = 0;
};

}  // namespace xqt
