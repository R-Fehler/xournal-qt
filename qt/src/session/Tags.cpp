#include "Tags.h"

#include <algorithm>

#include <QRegularExpression>

#include "MdDocument.h"

namespace xqt::tags {

namespace {
bool tagChar(QChar c) { return c.isLetterOrNumber() || c == u'-' || c == u'_' || c == u'/'; }

/// The characters before a `#` that may start a tag
bool opens(QChar c) {
    static const QString marks = QStringLiteral("([{\"'“‘«,;:");
    return c.isSpace() || marks.contains(c);
}

/// A name as a tag: empty parts of a path dropped, no `/` at its ends, at least one letter ("" if not)
QString cleaned(QStringView name) {
    QStringList parts;
    for (const QStringView part: name.split(u'/', Qt::SkipEmptyParts)) {
        parts << part.toString();
    }
    QString tag = parts.join(u'/');
    if (tag.size() > MAX_LENGTH) {
        tag.truncate(MAX_LENGTH);
        while (tag.endsWith(u'/')) {
            tag.chop(1);
        }
    }
    const bool letter = std::any_of(tag.begin(), tag.end(), [](QChar c) { return c.isLetter(); });
    return letter ? tag : QString();
}

void add(QStringList& into, const QString& tag) {
    if (!tag.isEmpty() && !contains(into, tag)) {
        into << tag;
    }
}

std::string_view trimmed(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r')) {
        s.remove_prefix(1);
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) {
        s.remove_suffix(1);
    }
    return s;
}

/// Where the front matter ends (the offset after its closing line; 0: none)
size_t frontMatterEnd(std::string_view source) {
    size_t at = source.substr(0, 3) == "\xEF\xBB\xBF" ? 3 : 0;
    const size_t first = source.find('\n', at);
    if (first == std::string_view::npos || trimmed(source.substr(at, first - at)) != "---") {
        return 0;
    }
    at = first + 1;
    while (at < source.size()) {
        const size_t nl = source.find('\n', at);
        const size_t end = nl == std::string_view::npos ? source.size() : nl;
        const std::string_view line = trimmed(source.substr(at, end - at));
        if (line == "---" || line == "...") {
            return nl == std::string_view::npos ? source.size() : nl + 1;
        }
        at = end + 1;
    }
    return 0;  // (not closed: no front matter)
}

/// A value of the front matter's list without its quotes and `#`
QString item(QStringView v) {
    v = v.trimmed();
    if (v.size() >= 2 && (v.front() == u'"' || v.front() == u'\'') && v.back() == v.front()) {
        v = v.mid(1, v.size() - 2).trimmed();
    }
    return fromKeyword(v);
}

void collect(const md::Block& b, std::string& text) {
    switch (b.kind) {
        case md::BlockKind::CodeBlock:
        case md::BlockKind::Html:
            return;
        default:
            break;
    }
    for (const md::Run& r: b.runs) {
        if (r.flags & (md::Code | md::Math | md::Html | md::Image | md::Marker)) {
            text += ' ';
        } else {
            text += r.text;
        }
    }
    text += '\n';
    for (const md::Block& child: b.children) {
        collect(child, text);
    }
}
}  // namespace

QStringList inText(QStringView text) {
    QStringList out;
    for (qsizetype i = 0; i < text.size(); ++i) {
        if (text[i] != u'#' || (i > 0 && !opens(text[i - 1]))) {
            continue;
        }
        qsizetype end = i + 1;
        while (end < text.size() && tagChar(text[end])) {
            ++end;
        }
        if (end < text.size() && text[end] == u'#') {
            i = end;  // ("#a#b": none)
            continue;
        }
        add(out, cleaned(text.mid(i + 1, end - i - 1)));
        i = end - 1;
    }
    return out;
}

QStringList inFrontMatter(std::string_view source) {
    QStringList out;
    const size_t end = frontMatterEnd(source);
    if (end == 0) {
        return out;
    }
    size_t at = source.find('\n') + 1;
    bool inList = false;  // (after "tags:" with nothing behind it: "  - name" lines)
    while (at < end) {
        const size_t nl = source.find('\n', at);
        const size_t lineEnd = nl == std::string_view::npos ? source.size() : nl;
        const std::string_view raw = source.substr(at, lineEnd - at);
        const std::string_view line = trimmed(raw);
        at = lineEnd + 1;
        if (inList) {
            if (line.substr(0, 1) == "-" && (raw.front() == ' ' || raw.front() == '\t' || raw.front() == '-')) {
                add(out, item(QString::fromUtf8(line.substr(1))));
                continue;
            }
            if (line.empty()) {
                continue;
            }
            inList = false;
        }
        const size_t colon = line.find(':');
        if (colon == std::string_view::npos || raw.front() == ' ' || raw.front() == '\t') {
            continue;
        }
        const QString name = QString::fromUtf8(trimmed(line.substr(0, colon))).toLower();
        if (name != QLatin1String("tags") && name != QLatin1String("tag")) {
            continue;
        }
        QString value = QString::fromUtf8(trimmed(line.substr(colon + 1)));
        if (value.isEmpty()) {
            inList = true;
            continue;
        }
        if (value.startsWith(u'[') && value.endsWith(u']')) {
            value = value.mid(1, value.size() - 2);
        }
        static const QRegularExpression separators(QStringLiteral("[,\\s]+"));
        for (const QString& v: value.split(separators, Qt::SkipEmptyParts)) {
            add(out, item(v));
        }
    }
    return out;
}

QStringList inMarkdown(std::string_view source) {
    QStringList out = inFrontMatter(source);
    const std::string_view body = source.substr(frontMatterEnd(source));
    if (md::isPlain(body)) {
        merge(out, inText(QString::fromUtf8(body)));
        return out;
    }
    if (body.find('#') == std::string_view::npos) {
        return out;
    }
    const md::Document doc = md::parse(body);
    std::string text;
    collect(doc.root, text);
    merge(out, inText(QString::fromUtf8(text)));
    return out;
}

QString fromKeyword(QStringView keyword) {
    keyword = keyword.trimmed();
    while (keyword.startsWith(u'#')) {
        keyword = keyword.mid(1);
    }
    QString tag;
    for (const QChar c: keyword) {
        if (c == u'/' && tag.endsWith(u'-')) {
            tag.back() = u'/';  // ("a - / b": "a/b")
        } else if (tagChar(c)) {
            tag += c;
        } else if (!tag.isEmpty() && !tag.endsWith(u'-') && !tag.endsWith(u'/')) {
            tag += u'-';
        }
    }
    while (tag.endsWith(u'-')) {
        tag.chop(1);
    }
    return cleaned(tag);
}

QStringList fromKeywords(QStringView keywords) {
    QStringList out;
    const bool listed = keywords.contains(u',') || keywords.contains(u';');
    static const QRegularExpression lists(QStringLiteral("[,;]"));
    static const QRegularExpression spaces(QStringLiteral("\\s+"));
    const QString all = keywords.toString();
    for (const QString& k: all.split(listed ? lists : spaces, Qt::SkipEmptyParts)) {
        add(out, fromKeyword(k));
    }
    return out;
}

QString key(QStringView tag) { return tag.toString().toCaseFolded(); }

void merge(QStringList& into, const QStringList& more) {
    for (const QString& t: more) {
        add(into, t);
    }
}

bool contains(const QStringList& list, QStringView tag) {
    return std::any_of(list.begin(), list.end(),
                       [&](const QString& t) { return t.compare(tag, Qt::CaseInsensitive) == 0; });
}

bool matches(QStringView tag, QStringView query) {
    while (query.startsWith(u'#')) {
        query = query.mid(1);
    }
    if (query.isEmpty()) {
        return false;
    }
    if (query.endsWith(u'/')) {
        return tag.size() > query.size() && tag.startsWith(query, Qt::CaseInsensitive);
    }
    if (tag.size() == query.size()) {
        return tag.compare(query, Qt::CaseInsensitive) == 0;
    }
    return tag.size() > query.size() && tag[query.size()] == u'/' && tag.startsWith(query, Qt::CaseInsensitive);
}

bool anyMatches(const QStringList& list, QStringView query) {
    return std::any_of(list.begin(), list.end(), [&](const QString& t) { return matches(t, query); });
}

Query splitQuery(const QString& query) {
    Query q;
    QStringList rest;
    for (const QString& word: query.split(u' ', Qt::SkipEmptyParts)) {
        if (word.startsWith(QLatin1String("tag:"), Qt::CaseInsensitive) && word.size() > 4) {
            q.tags << word.mid(4);
        } else {
            rest << word;
        }
    }
    q.rest = rest.join(u' ');
    return q;
}

}  // namespace xqt::tags
