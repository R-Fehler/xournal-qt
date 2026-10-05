/*
 * xournal-qt: the handwriting search in Settings and the library (qt/docs/handwriting-search.md): the switch, the
 * languages read, per language its model (state, download with the address and size shown first, removal, a folder of
 * the user's own), and the progress of reading.
 *
 * UI thread.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <map>
#include <memory>

#include <QObject>
#include <QPointer>
#include <QString>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

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
    /// The languages read: "en", "de" or "en+de" (English and German, the default)
    Q_PROPERTY(QString languages READ languages WRITE setLanguages NOTIFY changed)
    /// The recogniser can read (a model and the runtime there)
    Q_PROPERTY(bool ready READ ready NOTIFY changed)
    /// What the handwriting search does now, or why it cannot read (one line)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    /// Per language (English, German): its model, as a map (see models())
    Q_PROPERTY(QVariantList models READ models NOTIFY changed)
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
    QString languages() const;
    void setLanguages(const QString& languages);
    bool ready() const;
    QString status() const;
    /// Per language of the app's catalogue (English, German), a map:
    ///   language ("en"), label ("English"), needed (the languages read include it), name (of the model downloaded
    ///   for it), folder (its model's folder), own (the app's folder: downloaded and removed here), installed (a model
    ///   for the language is in the folder), inUse (it reads now), state (one line), source and size (of the
    ///   download), downloadAvailable (this build names its files), unpinned (else why not), downloading, progress
    ///   (0..1), error (of the last download)
    QVariantList models() const;
    /// One of them (tests)
    QVariantMap modelOf(const QString& language) const;
    int pagesWaiting() const;
    int libraryLeft() const;
    bool onBattery() const;

    /// Download a language's model into the app's data folder (the user agreed: the address and size were shown).
    Q_INVOKABLE void download(const QString& language);
    Q_INVOKABLE void cancelDownload(const QString& language);
    /// Remove a language's downloaded model (the app's own only); its language is read no more until it is there again.
    Q_INVOKABLE bool removeModel(const QString& language);
    /// Use the model in a folder of the user's for a language (never downloaded over or removed here); an empty one:
    /// the app's own again.
    Q_INVOKABLE void chooseFolder(const QString& language, const QUrl& folder);

Q_SIGNALS:
    void changed();
    void progressChanged();

private:
    ModelDownload* downloadOf(const QString& language) const;
    bool anyDownloading() const;

    AppContext& app;
    hwr::HandwritingSearch& search;
    QPointer<LibraryInkJob> library;
    std::map<QString, std::unique_ptr<ModelDownload>> fetches;  ///< by language
};

}  // namespace xqt
