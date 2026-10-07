/*
 * xournal-qt: the open documents of every window of the process. The windows (AppController: the main window first,
 * then the windows of undocked documents) register here (AppServices holds one); what needs "this file, wherever it
 * is open" asks find() instead of walking the windows' tabs itself (tags and to-dos written into a file, a .xopp
 * replaced by its hybrid PDF, links rewritten, pictures loaded anew).
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <utility>
#include <vector>

#include "filesystem.h"

class AppController;

namespace xqt {

class DocumentSession;

class OpenDocuments {
public:
    /// What counts as showing a file.
    struct Match {
        /// Not this one (the document asking)
        const DocumentSession* except = nullptr;
        /// A text file edited as a document (its Markdown or text file)
        bool textFiles = false;
        /// A plain PDF open without notes (the PDF is its document's background, it has no file of its own)
        bool plainPdf = false;
    };
    using Found = std::vector<std::pair<AppController*, DocumentSession*>>;

    /// A window opened (the first one is the main window) or went away
    void add(AppController* window);
    void remove(AppController* window);
    /// The windows, the main window first
    const std::vector<AppController*>& windows() const { return list; }
    AppController* mainWindow() const { return list.empty() ? nullptr : list.front(); }

    /// The tabs of all windows whose document is `file` (its .xopp or PDF; by `match`, also more)
    Found find(const fs::path& file, const Match& match) const;
    Found find(const fs::path& file) const { return find(file, Match{}); }
    /// Every open document of every window
    std::vector<DocumentSession*> all() const;

private:
    std::vector<AppController*> list;
};

}  // namespace xqt
