#include "DocumentLink.h"

#include <algorithm>
#include <cstdlib>

#include <QCoreApplication>
#include <QMimeData>
#include <QStringList>
#include <QUrl>

#include "MdDocument.h"
#include "MdPassages.h"

namespace xqt::links {

namespace {
QString tr(const char* text) { return QCoreApplication::translate("DocumentLink", text); }

/// Extensions of the files a link to a document may point to (documents the app opens itself); anything else is no
/// link to a document ("example.org/page" stays a web address, "/home/x.sh" is not opened).
bool knownExtension(const QString& path) {
    static const QStringList known{"xopp", "xoj",  "pdf",  "md",  "markdown", "txt", "png",
                                   "jpg",  "jpeg", "webp", "heic", "heif",    "tex", "org", "rst"};
    const qsizetype slash = path.lastIndexOf(QLatin1Char('/'));
    const qsizetype dot = path.lastIndexOf(QLatin1Char('.'));
    return dot > slash + 1 && known.contains(path.mid(dot + 1).toLower());
}

bool isMarkdownPath(const QString& path) {
    const QString lower = path.toLower();
    return lower.endsWith(QLatin1String(".md")) || lower.endsWith(QLatin1String(".markdown"));
}

QString decode(const QString& s) { return QUrl::fromPercentEncoding(s.toUtf8()); }

/// Percent-encodes what a Markdown link destination or a URI fragment cannot hold as it is (`extra`: more of it).
QString encode(const QString& s, const QString& extra) {
    QString out;
    out.reserve(s.size());
    for (const QChar c: s) {
        const char16_t u = c.unicode();
        if (u < 0x20 || u == 0x7f || u == ' ' || u == '%' || u == '#' || u == '?' || u == '(' || u == ')' ||
            u == '<' || u == '>' || u == '[' || u == ']' || u == '\\' || u == '"' || extra.contains(c)) {
            out += QStringLiteral("%%1").arg(static_cast<unsigned>(u), 2, 16, QLatin1Char('0')).toUpper();
        } else {
            out += c;
        }
    }
    return out;
}

/// Reads the fragment's "key=value&..." (or a plain fragment) into the link.
void readFragment(const QString& fragment, Link& link) {
    if (fragment.isEmpty()) {
        return;
    }
    if (!fragment.contains(QLatin1Char('='))) {
        // A plain fragment: a heading of a .md (GitHub, Obsidian), else a chapter (a named place)
        (isMarkdownPath(link.path) || link.wiki ? link.heading : link.chapter) = decode(fragment);
        return;
    }
    for (const QString& pair: fragment.split(QLatin1Char('&'), Qt::SkipEmptyParts)) {
        const qsizetype eq = pair.indexOf(QLatin1Char('='));
        const QString key = (eq < 0 ? pair : pair.left(eq)).toLower();
        const QString value = eq < 0 ? QString() : decode(pair.mid(eq + 1));
        if (key == QLatin1String("page")) {
            link.page = std::max(0, value.toInt());
        } else if (key == QLatin1String("pdfpage")) {
            link.pdfPage = std::max(0, value.toInt());
        } else if (key == QLatin1String("chapter")) {
            link.chapter = value;
        } else if (key == QLatin1String("heading")) {
            link.heading = value;
        } else if (key == QLatin1String("line")) {
            link.line = std::max(0, value.toInt());
        } else if (key == QLatin1String("text")) {
            link.text = value;
        }
    }
}

/// The byte offset where line `line` (1-based) starts; the end of the text after its last line.
size_t lineOffset(const std::string& text, int line) {
    size_t at = 0;
    for (int l = 1; l < line; ++l) {
        const size_t nl = text.find('\n', at);
        if (nl == std::string::npos) {
            return text.size();
        }
        at = nl + 1;
    }
    return at;
}
}  // namespace

std::optional<Link> parse(const QString& written) {
    QString target = written.trimmed();
    if (target.startsWith(QLatin1Char('<')) && target.endsWith(QLatin1Char('>'))) {
        target = target.mid(1, target.size() - 2).trimmed();
    }
    if (target.isEmpty() || target.startsWith(QLatin1String("#Page:"))) {
        return std::nullopt;
    }
    Link link;
    // A scheme: only file:// is a document (a drive letter "C:" is a path)
    if (const qsizetype colon = target.indexOf(QLatin1Char(':')); colon > 1) {
        const QString scheme = target.left(colon);
        const bool isScheme = std::all_of(scheme.begin(), scheme.end(), [](QChar c) {
            return c.isLetterOrNumber() || c == QLatin1Char('+') || c == QLatin1Char('-') || c == QLatin1Char('.');
        });
        if (isScheme) {
            if (scheme.toLower() != QLatin1String("file")) {
                return std::nullopt;
            }
            const QUrl url(target);
            if (!url.isValid() || !url.isLocalFile()) {
                return std::nullopt;
            }
            link.path = url.toLocalFile();
            if (!knownExtension(link.path)) {
                return std::nullopt;
            }
            readFragment(url.fragment(QUrl::FullyEncoded), link);
            return link;
        }
    }
    const qsizetype hash = target.indexOf(QLatin1Char('#'));
    const QString path = hash < 0 ? target : target.left(hash);
    const QString fragment = hash < 0 ? QString() : target.mid(hash + 1);
    link.path = decode(path);
    if (link.path.isEmpty()) {
        // This document: only with a place in it ("#page=5"); "#anchor" is a heading of the same text
        if (!fragment.contains(QLatin1Char('='))) {
            return std::nullopt;
        }
        readFragment(fragment, link);
        return link.wholeDocument() ? std::nullopt : std::optional<Link>(link);
    }
    // Only files the app opens as documents: a link never opens anything else on this computer with a tap
    if (!knownExtension(link.path)) {
        return std::nullopt;
    }
    readFragment(fragment, link);
    return link;
}

std::optional<Link> parseWiki(const QString& target) {
    const QString t = target.trimmed();
    const qsizetype hash = t.indexOf(QLatin1Char('#'));
    Link link;
    link.wiki = true;
    link.path = (hash < 0 ? t : t.left(hash)).trimmed();
    if (link.path.isEmpty()) {
        return std::nullopt;
    }
    if (hash >= 0) {
        link.heading = t.mid(hash + 1).trimmed();
    }
    return link;
}

QString write(const Link& link) {
    QString out = encode(link.path, QString());
    QStringList parts;
    const QString value = QStringLiteral("&=+");
    if (!link.chapter.isEmpty()) {
        parts << QStringLiteral("chapter=") + encode(link.chapter, value);
    }
    if (!link.heading.isEmpty()) {
        parts << QStringLiteral("heading=") + encode(link.heading, value);
    }
    if (link.page > 0) {
        parts << QStringLiteral("page=%1").arg(link.page);
    }
    if (link.pdfPage > 0) {
        parts << QStringLiteral("pdfpage=%1").arg(link.pdfPage);
    }
    if (link.line > 0) {
        parts << QStringLiteral("line=%1").arg(link.line);
    }
    if (!link.text.isEmpty()) {
        parts << QStringLiteral("text=") + encode(link.text, value);
    }
    if (!parts.isEmpty()) {
        out += QLatin1Char('#') + parts.join(QLatin1Char('&'));
    }
    return out;
}

QString markdown(const QString& title, const Link& link) {
    QString shown = title.trimmed();
    if (shown.isEmpty()) {
        shown = link.path.section(QLatin1Char('/'), -1);
    }
    shown.replace(QLatin1Char('\\'), QLatin1String("\\\\"));
    shown.replace(QLatin1Char('['), QLatin1String("\\["));
    shown.replace(QLatin1Char(']'), QLatin1String("\\]"));
    shown.replace(QLatin1Char('\n'), QLatin1Char(' '));
    return QStringLiteral("[%1](%2)").arg(shown, write(link));
}

QString relativePath(const fs::path& from, const fs::path& target) {
    const fs::path rel = target.lexically_normal().lexically_relative(from.parent_path().lexically_normal());
    const fs::path chosen = rel.empty() ? target : rel;
    return QString::fromStdString(chosen.generic_string());
}

fs::path resolvePath(const fs::path& folder, const QString& path) {
    const fs::path p(path.toStdString());
    if (p.is_absolute()) {
        return p.lexically_normal();
    }
    return (folder / p).lexically_normal();
}

QString slug(const QString& heading) {
    QString out;
    bool space = false;
    for (const QChar c: heading.trimmed().toLower()) {
        if (c.isSpace()) {
            space = true;
            continue;
        }
        if (!c.isLetterOrNumber() && c != QLatin1Char('-') && c != QLatin1Char('_') && !c.isMark()) {
            continue;
        }
        if (space && !out.isEmpty()) {
            out += QLatin1Char('-');
        }
        space = false;
        out += c;
    }
    return out;
}

QString normalised(const QString& title) {
    QString out;
    bool gap = false;
    for (const QChar c: title.toCaseFolded()) {
        if (c.isLetterOrNumber() || c.isMark()) {
            if (gap && !out.isEmpty()) {
                out += QLatin1Char(' ');
            }
            gap = false;
            out += c;
        } else {
            gap = true;
        }
    }
    return out;
}

QString fingerprint(const QString& pageText, int words) {
    const QString all = normalised(pageText);
    QString out;
    int count = 0;
    for (const QString& word: all.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
        if (count == words) {
            break;
        }
        if (out.isEmpty() && word.size() > 48) {
            return word.left(48);
        }
        if (out.size() + 1 + word.size() > 48) {
            break;
        }
        if (!out.isEmpty()) {
            out += QLatin1Char(' ');
        }
        out += word;
        ++count;
    }
    return out;
}

Place resolve(const Link& link, const std::vector<Chapter>& chapters, const std::vector<Page>& pages) {
    Place place;
    const int count = static_cast<int>(pages.size());
    const auto clamp = [&](int page) { return count == 0 ? 0 : std::clamp(page, 0, count - 1); };
    QString missing;  // what was not found (the note starts with it)
    if (!link.chapter.isEmpty()) {
        for (const Chapter& c: chapters) {
            if (c.title == link.chapter) {
                place.page = clamp(c.page);
                return place;
            }
        }
        const QString wanted = normalised(link.chapter);
        for (const Chapter& c: chapters) {
            if (normalised(c.title) == wanted) {
                place.page = clamp(c.page);
                return place;
            }
        }
        missing = tr("Chapter \"%1\" not found").arg(link.chapter);
    }
    const auto withNote = [&](int page) {
        place.page = clamp(page);
        if (!missing.isEmpty()) {
            place.note = link.page > 0 || link.pdfPage > 0 ? tr("%1, opened page %2").arg(missing).arg(place.page + 1)
                                                            : missing;
        }
        return place;
    };
    if (link.pdfPage > 0) {
        for (int i = 0; i < count; ++i) {
            if (pages[static_cast<size_t>(i)].pdfPage == link.pdfPage) {
                return withNote(i);
            }
        }
        if (missing.isEmpty()) {
            missing = tr("PDF page %1 not found").arg(link.pdfPage);
        }
    }
    if (link.page <= 0) {
        return withNote(0);
    }
    const int wanted = link.page - 1;
    if (!link.text.isEmpty()) {
        const QString print = normalised(link.text);
        const auto has = [&](int i) {
            return i >= 0 && i < count && normalised(pages[static_cast<size_t>(i)].text).contains(print);
        };
        // The page itself, else the nearest one with the text (before it on a tie: pages were inserted before it)
        for (int distance = 0; distance < count; ++distance) {
            if (has(wanted - distance)) {
                return withNote(wanted - distance);
            }
            if (distance > 0 && has(wanted + distance)) {
                return withNote(wanted + distance);
            }
        }
    }
    if (wanted >= count && count > 0 && missing.isEmpty()) {
        missing = tr("Page %1 not found").arg(link.page);
    }
    return withNote(wanted);
}

TextPlace resolveInText(const Link& link, const std::string& markdown) {
    TextPlace place;
    if (!link.heading.isEmpty()) {
        const QString wanted = slug(link.heading);
        const md::Document doc = md::parse(markdown);
        for (const md::Passage& p: md::passages(doc)) {
            if (p.kind == md::Passage::Kind::Heading && p.begin != md::NO_SOURCE &&
                slug(QString::fromStdString(p.text)) == wanted) {
                const size_t nl = markdown.rfind('\n', p.begin == 0 ? 0 : p.begin - 1);
                place.offset = p.begin == 0 || nl == std::string::npos ? 0 : nl + 1;
                return place;
            }
        }
        place.note = link.line > 0 ? tr("Heading \"%1\" not found, opened line %2").arg(link.heading).arg(link.line)
                                   : tr("Heading \"%1\" not found").arg(link.heading);
    }
    if (link.line > 0) {
        place.offset = lineOffset(markdown, link.line);
    }
    return place;
}

void toMime(QMimeData& mime, const QString& title, const fs::path& file, Link link) {
    link.wiki = false;
    link.path = QString::fromStdString(file.generic_string());
    const QString absolute = write(link);
    mime.setData(MIME, (title + QLatin1Char('\n') + absolute).toUtf8());
    mime.setText(markdown(title, link));
    QUrl url = QUrl::fromLocalFile(link.path);
    const qsizetype hash = absolute.indexOf(QLatin1Char('#'));
    if (hash >= 0) {
        url.setFragment(absolute.mid(hash + 1), QUrl::TolerantMode);
    }
    mime.setHtml(QStringLiteral("<a href=\"%1\">%2</a>")
                         .arg(url.toString(QUrl::FullyEncoded).toHtmlEscaped(), title.toHtmlEscaped()));
}

std::optional<Copied> fromMime(const QMimeData* mime) {
    if (!mime || !mime->hasFormat(MIME)) {
        return std::nullopt;
    }
    const QString data = QString::fromUtf8(mime->data(MIME));
    const qsizetype nl = data.indexOf(QLatin1Char('\n'));
    if (nl < 0) {
        return std::nullopt;
    }
    auto link = parse(data.mid(nl + 1));
    if (!link || link->path.isEmpty()) {
        return std::nullopt;
    }
    return Copied{data.left(nl), *link};
}

QString markdownFor(const Copied& copied, const fs::path& holder) {
    Link link = copied.link;
    if (!holder.empty()) {
        link.path = relativePath(holder, fs::path(link.path.toStdString()));
    }
    return markdown(copied.title, link);
}

QString markerText(const Copied& copied, const fs::path& holder) {
    Copied marked = copied;
    marked.title = QStringLiteral("\U0001F517 ") + copied.title;
    return markdownFor(marked, holder);
}

}  // namespace xqt::links
