/*
 * xournal-qt: links between documents kept working when documents are renamed or moved in the app
 * (qt/docs/links.md, "Keeping links working").
 *
 * The library index knows the links of every document (Markdown files, Markdown boxes and link markers of .xopp
 * files). After a rename or move, `plan` works out which links now point elsewhere - links to a moved document, and
 * the relative links of a moved document itself - and how they read at the new places; `rewriteMarkdown` changes
 * exactly those link targets in a Markdown text and nothing else (the other bytes stay as they are).
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <string>
#include <utility>
#include <vector>

#include <QString>
#include <QStringList>

#include "filesystem.h"

class Document;

namespace xqt::LinkRewrite {

/// The links a document holds (LibraryIndex::linkSources).
struct Source {
    fs::path file;          ///< as the index has it (before or after the move: plan() finds out)
    QStringList links;      ///< Markdown link targets as written
    QStringList wikiLinks;  ///< [[wiki link]] targets
};
/// One link target to write anew.
struct Change {
    QString from;  ///< as written
    QString to;
    bool wiki = false;
};
struct Plan {
    fs::path file;  ///< the document holding the links, at its new place
    std::vector<Change> changes;
};
using Moves = std::vector<std::pair<fs::path, fs::path>>;

/// The links to write anew after these moves (old, new: files and folders).
std::vector<Plan> plan(const std::vector<Source>& sources, const Moves& moves);

/// Change the link targets in a Markdown text: `[t](from…)`, `[t](<from>)`, `[id]: from`, `[[from#…|…]]`. Returns
/// how many were changed.
int rewriteMarkdown(std::string& text, const std::vector<Change>& changes);

/// The same in the Markdown boxes and link markers of a document (the caller locks it; not undoable: for a document
/// that is not open). Returns how many links were changed.
int rewriteDocument(Document& doc, const std::vector<Change>& changes);

/// Rewrite a file that is not open: a Markdown file through its text (byte for byte where nothing changed), a .xopp
/// loaded and written again. Returns how many links were changed (-1: it could not be written; `error` says why).
int rewriteFile(const fs::path& file, const std::vector<Change>& changes, std::string& error);

}  // namespace xqt::LinkRewrite
