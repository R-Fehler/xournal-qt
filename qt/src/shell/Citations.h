/*
 * xournal-qt: citations, the QML side (`app.citations`, qt/docs/citations.md).
 *
 * Selected text (PDF text, or the text being written) can be looked up: Google Scholar and a translator in the
 * browser. Every web address is shown before it is opened (the menu shows it; a confirmation shows it whole unless
 * "Don't ask again" was chosen); the browser is reached through SystemApps, which tests replace.
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

Q_SIGNALS:
    /// A web address was opened (tests; the note).
    void webOpened(const QString& url);

private:
    Settings& settings;
    LibraryModel* library;
};

}  // namespace xqt
