/*
 * xournal-qt: snippet cards of Markdown files (extended library search).
 *
 * A search hit in a Markdown file is shown as a card per passage with hits (MdPassages.h): the passage alone (a
 * paragraph, a list item in its list, a table row under its header, a code block), drawn by our Markdown renderer,
 * with the hits marked as on the pages of other documents (the first one, which opening the card makes current, in
 * orange). A long passage is cut to a few lines around its first hit. The headings above it come from the index.
 *
 * MdSnippetProvider is an asynchronous QML image provider, "image://mdsnippet/<path>/<stamp>/<query>/<passage>" (see
 * baseUrl()); the requested size is the card's: its width, and the most it may be high. The parsed files are kept
 * (the last 8 used), so the cards of one file come from one parse.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <QImage>
#include <QQuickAsyncImageProvider>
#include <QString>

#include "filesystem.h"
#include "DocumentFiles.h"

namespace xqt {

class MdSnippetProvider final: public QQuickAsyncImageProvider {
public:
    /// Let the workers finish before the application goes away (they draw with Pango and Cairo).
    static void shutdown();
    QQuickImageResponse* requestImageResponse(const QString& id, const QSize& requestedSize) override;

    /// URL of the cards of a Markdown file for a search; append "/<passage>" (0-based, see md::passages).
    static QString baseUrl(const DocumentItem& item, const QString& query);
    /// Draw passage `passage` of a Markdown file `width` pixels wide and at most `maxHeight` high (0: all of it),
    /// with the hits of `query` marked. Blocks; any thread.
    static QImage render(const fs::path& file, int passage, const QString& query, int width, int maxHeight);
    /// Forget the kept files (tests).
    static void clearCaches();
    /// Files parsed so far (tests: the cache works).
    static int parseCount();
};

}  // namespace xqt
