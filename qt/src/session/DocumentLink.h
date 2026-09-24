/*
 * xournal-qt: links between documents (qt/docs/links.md) - the link format and where a link leads in a document.
 *
 * A link is one URI, the path relative to the document that holds it, as in Markdown and HTML:
 *
 *     ../Lectures/Kalman%20filter.xopp#page=12&pdfpage=7
 *     ../Lectures/Kalman%20filter.xopp#chapter=Prediction%20step&page=12
 *     ../Notes/turbines.md#heading=blade-design&line=40
 *     ../Notes/turbines.md#blade-design                 (a plain heading, as GitHub and Obsidian write it)
 *     [[turbines#Blade design]]                          (Obsidian's wiki link, inside vaults)
 *
 * The fragment holds what the link points to, in the order it is tried:
 *  - `chapter=` a chapter's title (the PDF outline, Markdown headings, text-mode headings), found by its title, then by
 *    its title ignoring case and punctuation; not found, the saved page is used with a note;
 *  - `pdfpage=` for a page that shows a PDF page: the PDF page (1-based), which stays when notes pages come and go;
 *  - `page=` the page (1-based, the PDF "open parameters" form that other viewers follow), checked against `text=`, a
 *    short fingerprint of the page's text (its first words), when the page has text: when page 12 no longer has it
 *    and page 13 does, page 13;
 *  - `heading=` a heading of a `.md` file (its slug, as GitHub and Obsidian anchors), with `line=` (1-based) as the
 *    fallback.
 *
 * Qt Core only; the resolution works on plain descriptions of the target (chapters, pages), so it is tested alone.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <optional>
#include <string>
#include <vector>

#include <QString>

#include "filesystem.h"

class QMimeData;

namespace xqt::links {

struct Link {
    /// The path as written, decoded ("../Lectures/Kalman filter.xopp"); empty: this document. A wiki link's name
    /// has no extension ("turbines").
    QString path;
    int page = 0;      ///< 1-based page, 0: none
    int pdfPage = 0;   ///< 1-based PDF page, 0: none
    QString chapter;   ///< a chapter's title
    QString text;      ///< the page's fingerprint (its first words, normalised)
    QString heading;   ///< a heading of a .md: its slug, or its text (a plain `#fragment`, a wiki link)
    int line = 0;      ///< 1-based line of a .md, 0: none
    bool wiki = false; ///< read from [[a wiki link]]: the path is a name to look for

    bool operator==(const Link& o) const = default;
    /// Only a document, no place in it.
    bool wholeDocument() const { return page <= 0 && pdfPage <= 0 && chapter.isEmpty() && heading.isEmpty() && line <= 0; }
};

/// A link target as written in a Markdown link (`[t](target)`) or a text: a link to a document (a file the app
/// opens as one: .xopp, .pdf, .md, images, ...), or nothing (a web or mail address, any other scheme, any other
/// file, "#Page:12" of upstream, an empty target). `file://` URLs of documents are documents too.
std::optional<Link> parse(const QString& target);
/// The target of a [[wiki link]] (md4c gives it without the "|label"): "note#heading" or "note".
std::optional<Link> parseWiki(const QString& target);
/// Whether a Markdown link target is a link to a document (parse() takes it).
inline bool isDocumentLink(const QString& target) { return parse(target).has_value(); }

/// The link as a URI: the path percent-encoded where Markdown and URIs need it (space, '%', '#', '?', '(', ')', '<',
/// '>', '[', ']'), then the fragment: chapter, heading, page, pdfpage, line, text.
QString write(const Link& link);
/// `[title](link)`, the title's brackets escaped (an empty title: the file name).
QString markdown(const QString& title, const Link& link);

/// The path of `target` relative to the folder of `from` (the document holding the link), with '/' ("../a/b.xopp").
/// An absolute path when there is no relative one (another drive).
QString relativePath(const fs::path& from, const fs::path& target);
/// The file a link's path points to, from the folder of the document holding it (an absolute path stays as it is).
fs::path resolvePath(const fs::path& folder, const QString& path);

/// GitHub's heading anchor: lower case, letters, digits, '-' and '_' kept, spaces to '-', the rest dropped
/// ("Blade design (v2)" -> "blade-design-v2"). A slug gives itself again.
QString slug(const QString& heading);
/// A title for comparing: case folded, only letters and digits, words separated by one space.
QString normalised(const QString& title);
/// The fingerprint of a page's text: its first `words` words, normalised (at most 48 characters). Empty: no text.
QString fingerprint(const QString& pageText, int words = 5);

// --- where a link leads ------------------------------------------------------------------------------------------

/// A chapter of the target document (its contents).
struct Chapter {
    QString title;
    int page = 0;  ///< 0-based
};
/// A page of the target document.
struct Page {
    int pdfPage = 0;  ///< 1-based PDF page it shows, 0: none
    QString text;     ///< its text (text elements, Markdown), for the fingerprint
};
struct Place {
    int page = 0;  ///< 0-based
    /// Something was not found where the link said (the chapter, the page's text): shown to the reader.
    QString note;
};
/// Where a link leads in a document with these chapters and pages.
Place resolve(const Link& link, const std::vector<Chapter>& chapters, const std::vector<Page>& pages);

/// Where a link leads in a Markdown text: the byte offset of the heading (by its slug), else of the line, else 0.
struct TextPlace {
    size_t offset = 0;
    QString note;
};
TextPlace resolveInText(const Link& link, const std::string& markdown);

// --- the clipboard ----------------------------------------------------------------------------------------------

/// "Copy link" puts a link on the clipboard three ways: the app's own format (the title and the link with the
/// target's absolute path, made relative where it is pasted), Markdown `[title](/absolute/path#…)` as text, and an
/// HTML link to the `file://` URI for rich text editors.
inline constexpr const char* MIME = "application/x-xournalqt-link";
struct Copied {
    QString title;
    Link link;  ///< its path is the target's absolute path
};
/// Fills `mime` with a link to `file` (absolute) at the place `link` says (its path is replaced).
void toMime(QMimeData& mime, const QString& title, const fs::path& file, Link link);
std::optional<Copied> fromMime(const QMimeData* mime);
/// The Markdown link to paste into a document at `holder` (its file; empty: a new document, the path stays absolute).
QString markdownFor(const Copied& copied, const fs::path& holder);
/// A link marker's text: the Markdown link with a chain in front of its title.
QString markerText(const Copied& copied, const fs::path& holder);

}  // namespace xqt::links
