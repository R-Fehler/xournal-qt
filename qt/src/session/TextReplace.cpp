#include "TextReplace.h"

#include <algorithm>

#include <QRegularExpression>

namespace xqt::replace {

namespace {

/// The pattern of a query that is not a regular expression: its words escaped, the whitespace between them a run of
/// spaces with at most one line break
QString literalPattern(const QString& query) {
    const QStringList words = query.simplified().split(u' ', Qt::SkipEmptyParts);
    QStringList escaped;
    escaped.reserve(words.size());
    for (const QString& w: words) {
        escaped.push_back(QRegularExpression::escape(w));
    }
    return escaped.join(QStringLiteral("(?:[^\\S\\n]+\\n?[^\\S\\n]*|\\n[^\\S\\n]*)"));
}

/// The replacement of a match of a regular expression (see TextReplace.h)
QString expand(const QString& with, const QRegularExpressionMatch& m) {
    QString out;
    out.reserve(with.size());
    const qsizetype n = with.size();
    const auto group = [&](qsizetype& i, qsizetype maxDigits) {
        // digits from i on: the group's text (i after them); -1 if none
        int number = 0;
        qsizetype k = i;
        while (k < n && k - i < maxDigits && with[k].isDigit() && number * 10 + with[k].digitValue() <= m.lastCapturedIndex()) {
            number = number * 10 + with[k].digitValue();
            ++k;
        }
        if (k == i) {
            return false;
        }
        out += m.captured(number);
        i = k;
        return true;
    };
    for (qsizetype i = 0; i < n;) {
        const QChar c = with[i];
        if (c == u'\\' && i + 1 < n) {
            const QChar d = with[i + 1];
            if (d == u'n') {
                out += u'\n';
                i += 2;
            } else if (d == u't') {
                out += u'\t';
                i += 2;
            } else if (d.isDigit()) {
                qsizetype j = i + 1;
                if (!group(j, 1)) {
                    j = i + 2;  // (a group the expression does not have: nothing)
                }
                i = j;
            } else {
                out += d;  // \\, \$ and the like: the character itself
                i += 2;
            }
        } else if (c == u'$' && i + 1 < n) {
            const QChar d = with[i + 1];
            if (d == u'$') {
                out += u'$';
                i += 2;
            } else if (d.isDigit()) {
                qsizetype j = i + 1;
                if (!group(j, 2)) {
                    j = i + 2;  // (a group the expression does not have: nothing)
                }
                i = j;
            } else if (d == u'{') {
                const qsizetype close = with.indexOf(u'}', i + 2);
                const QString name = close > i + 2 ? with.mid(i + 2, close - i - 2) : QString();
                bool number = false;
                const int g = name.toInt(&number);
                if (close < 0 || name.isEmpty()) {
                    out += c;
                    ++i;
                } else {
                    out += number ? (g <= m.lastCapturedIndex() ? m.captured(g) : QString()) : m.captured(name);
                    i = close + 1;
                }
            } else {
                out += c;
                ++i;
            }
        } else {
            out += c;
            ++i;
        }
    }
    return out;
}

/// UTF-16 offsets of a text as UTF-8 byte offsets, read forward (the matches come in order)
class Bytes {
public:
    explicit Bytes(const QString& text): text(text) {}
    size_t of(qsizetype at) {
        if (at < pos) {
            pos = 0;
            bytes = 0;
        }
        for (; pos < at; ++pos) {
            const char16_t c = text[pos].unicode();
            if (c < 0x80) {
                bytes += 1;
            } else if (c < 0x800) {
                bytes += 2;
            } else if (QChar::isHighSurrogate(c) && pos + 1 < text.size() && QChar::isLowSurrogate(text[pos + 1].unicode())) {
                bytes += 4;
                ++pos;  // (the low surrogate: no bytes of its own)
            } else {
                bytes += 3;
            }
        }
        return bytes;
    }

private:
    const QString& text;
    qsizetype pos = 0;
    size_t bytes = 0;
};

}  // namespace

std::vector<std::pair<size_t, size_t>> hiddenRanges(std::string_view source) {
    std::vector<std::pair<size_t, size_t>> out;
    constexpr std::string_view open = "<!-- xqt:";
    constexpr std::string_view close = "-->";
    for (size_t at = source.find(open); at != std::string_view::npos; at = source.find(open, at + 1)) {
        const size_t end = source.find(close, at + open.size());
        const size_t to = end == std::string_view::npos ? source.size() : end + close.size();
        out.emplace_back(at, to);
        at = to - 1;
    }
    return out;
}

std::vector<Match> find(std::string_view source, const QString& query, const QString& with, const Options& options) {
    std::vector<Match> out;
    if (query.trimmed().isEmpty() && !(options.regex && !query.isEmpty())) {
        return out;
    }
    QString pattern = options.regex ? query : literalPattern(query);
    if (options.wholeWord) {
        pattern = QStringLiteral("(?<![\\p{L}\\p{N}])(?:%1)(?![\\p{L}\\p{N}])").arg(pattern);
    }
    QRegularExpression::PatternOptions flags =
            QRegularExpression::UseUnicodePropertiesOption | QRegularExpression::MultilineOption;
    if (!options.caseSensitive) {
        flags |= QRegularExpression::CaseInsensitiveOption;
    }
    const QRegularExpression re(pattern, flags);
    if (!re.isValid()) {
        return out;
    }
    const QString text = QString::fromUtf8(source.data(), static_cast<qsizetype>(source.size()));
    const auto hidden = hiddenRanges(source);
    auto nextHidden = hidden.begin();
    Bytes bytes(text);
    const std::string literal = options.regex ? std::string() : with.toStdString();
    QRegularExpressionMatchIterator it = re.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        if (m.capturedLength() == 0) {
            continue;
        }
        const size_t begin = bytes.of(m.capturedStart());
        const size_t end = bytes.of(m.capturedEnd());
        while (nextHidden != hidden.end() && nextHidden->second <= begin) {
            ++nextHidden;
        }
        if (nextHidden != hidden.end() && nextHidden->first < end) {
            continue;  // (in a comment of the app's own, or across one)
        }
        out.push_back({begin, end, options.regex ? expand(with, m).toStdString() : literal});
    }
    return out;
}

std::string apply(std::string_view source, const std::vector<Match>& matches) {
    std::string out;
    out.reserve(source.size());
    size_t at = 0;
    for (const Match& m: matches) {
        if (m.begin < at || m.end > source.size()) {
            continue;
        }
        out.append(source.substr(at, m.begin - at));
        out += m.with;
        at = m.end;
    }
    out.append(source.substr(at));
    return out;
}

}  // namespace xqt::replace
