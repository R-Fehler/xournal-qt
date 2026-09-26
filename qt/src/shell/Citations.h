/*
 * xournal-qt: citations, the QML side (`app.citations`, qt/docs/citations.md).
 *
 * Selected text (PDF text, or the text being written) can be looked up: Google Scholar and a translator in the
 * browser. Every web address is shown before it is opened (the menu shows it; a confirmation shows it whole unless
 * "Don't ask again" was chosen); the browser is reached through SystemApps, which tests replace.
 *
 * A bibliography entry finds its paper in the library: its title is guessed (cite::guessTitle), and the library's
 * documents are matched by their titles (LibraryIndex::findTitle) on a worker thread; the hits come as `paperHits`.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>

class Settings;

namespace xqt {

class LibraryModel;

class Citations final: public QObject {
    Q_OBJECT
    /// The documents found for the last findPapers(): [{ path, title, folder (relative to the library, "" at the
    /// top), fileName, score (0-100), matched ("title", "heading", "name", "text") }], best first
    Q_PROPERTY(QVariantList paperHits READ paperHits NOTIFY papersChanged)
    /// findPapers() is still matching
    Q_PROPERTY(bool searchingPapers READ searchingPapers NOTIFY papersChanged)
public:
    /// `library`: the window's library (its index, its folders; may be null in tests).
    Citations(Settings& settings, LibraryModel* library, QObject* parent = nullptr);
    ~Citations() override;

    // --- looking up selected text in the browser ------------------------------------------------------------------
    /// Google Scholar's address for the text ("" for an empty text).
    Q_INVOKABLE QString scholarUrl(const QString& text) const;
    /// The translator's address for the text (Settings: `translateService`, `translateLanguage`); "" for an empty
    /// text or a custom translator address that is not valid.
    Q_INVOKABLE QString translateUrl(const QString& text) const;
    /// Open a web address (http, https) in the browser. The caller showed it first. False for anything else.
    Q_INVOKABLE bool openWeb(const QString& url);
    /// An address to read: its escapes decoded ("q=Attention is all"); what opens is the address itself.
    Q_INVOKABLE QString displayUrl(const QString& url) const;
    /// Where an address goes ("scholar.google.com").
    Q_INVOKABLE QString hostOf(const QString& url) const;
    /// Put text (an address) on the clipboard.
    Q_INVOKABLE void copyText(const QString& text) const;
    /// The translators Settings offers: [{ key, name }]
    Q_INVOKABLE QVariantList translators() const;
    /// The language translations go into while the setting is "" (the system's).
    Q_INVOKABLE QString systemLanguage() const;
    /// The selection as the look-up actions take it: hyphens at line ends joined, whitespace collapsed.
    Q_INVOKABLE QString cleanText(const QString& text) const;

    // --- the paper of a reference in the library --------------------------------------------------------------------
    /// The likely title of a bibliography entry: { title, raw (the cleaned entry), how }
    Q_INVOKABLE QVariantMap guessTitle(const QString& entry) const;
    /// Match the library's documents with this title (and the whole entry: `raw`), in the background; the result
    /// is `paperHits`. A newer call replaces an older one. `exclude`: files left out (the document the reference is
    /// in, which has its title too).
    Q_INVOKABLE void findPapers(const QString& title, const QString& raw, const QStringList& exclude = {});
    QVariantList paperHits() const { return hits; }
    bool searchingPapers() const { return searching; }

Q_SIGNALS:
    /// A web address was opened (tests; the note).
    void webOpened(const QString& url);
    void papersChanged();

private:
    Settings& settings;
    LibraryModel* library;
    QVariantList hits;
    bool searching = false;
    quint64 searchGeneration = 0;
};

}  // namespace xqt
