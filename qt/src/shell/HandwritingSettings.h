/*
 * xournal-qt: the handwriting search in Settings and the library (qt/docs/handwriting-search.md): the switch, the
 * model's state, its download (with the address and size shown first) and removal, and the progress of reading.
 *
 * UI thread.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <memory>

#include <QObject>
#include <QPointer>
#include <QString>

namespace xqt {

class AppContext;
class LibraryInkJob;
class ModelDownload;

namespace hwr {
class HandwritingSearch;
}

class HandwritingSettings final: public QObject {
    Q_OBJECT
    /// The setting (off until switched on)
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY changed)
    /// The recogniser can read (model and runtime there)
    Q_PROPERTY(bool ready READ ready NOTIFY changed)
    /// What the handwriting search does now, or why it cannot read (one line)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString modelFolder READ modelFolder NOTIFY changed)
    /// The model is the app's own (in its data folder): it can be downloaded and removed here
    Q_PROPERTY(bool ownModel READ ownModel NOTIFY changed)
    Q_PROPERTY(bool modelInstalled READ modelInstalled NOTIFY changed)
    /// Where the download comes from, and how big it is ("64 MB")
    Q_PROPERTY(QString downloadSource READ downloadSource CONSTANT)
    Q_PROPERTY(QString downloadSize READ downloadSize CONSTANT)
    /// This build names the model's files (else the model comes from qt/scripts/hwr-model.sh)
    Q_PROPERTY(bool downloadAvailable READ downloadAvailable CONSTANT)
    Q_PROPERTY(bool downloading READ downloading NOTIFY changed)
    Q_PROPERTY(double downloadProgress READ downloadProgress NOTIFY changed)
    Q_PROPERTY(QString downloadError READ downloadError NOTIFY changed)
    /// Pages of open documents waiting to be read; documents of the library left; the computer is on battery
    Q_PROPERTY(int pagesWaiting READ pagesWaiting NOTIFY progressChanged)
    Q_PROPERTY(int libraryLeft READ libraryLeft NOTIFY progressChanged)
    Q_PROPERTY(bool onBattery READ onBattery NOTIFY progressChanged)
public:
    HandwritingSettings(AppContext& app, hwr::HandwritingSearch& search, LibraryInkJob* library,
                        QObject* parent = nullptr);
    ~HandwritingSettings() override;

    bool enabled() const;
    void setEnabled(bool on);
    bool ready() const;
    QString status() const;
    QString modelFolder() const;
    bool ownModel() const;
    bool modelInstalled() const;
    QString downloadSource() const;
    QString downloadSize() const;
    bool downloadAvailable() const;
    bool downloading() const;
    double downloadProgress() const;
    QString downloadError() const;
    int pagesWaiting() const;
    int libraryLeft() const;
    bool onBattery() const;

    /// Download the model into the app's data folder (the user agreed: the address and size were shown).
    Q_INVOKABLE void download();
    Q_INVOKABLE void cancelDownload();
    /// Remove the downloaded model (the app's own only); the search reads nothing more until it is there again.
    Q_INVOKABLE bool removeModel();

Q_SIGNALS:
    void changed();
    void progressChanged();

private:
    AppContext& app;
    hwr::HandwritingSearch& search;
    QPointer<LibraryInkJob> library;
    std::unique_ptr<ModelDownload> fetch;
};

}  // namespace xqt
