/*
 * xournal-qt: citations: queries and addresses (see Citation.h).
 *
 * @license GNU GPLv2 or later
 */
#include "Citation.h"

#include <algorithm>

#include <QLocale>
#include <QRegularExpression>

#include "TextMatch.h"
#include "WordMatch.h"

namespace xqt::cite {

QString cleanText(const QString& text, int max) {
    QString t = text;
    // A word broken at a line end: "hyphen-\nation" (a lower-case letter after the break: not "Kalman-\nBucy")
    static const QRegularExpression broken(QStringLiteral("(\\p{L})[-\\x{00AD}\\x{2010}]\\s*\\n\\s*(\\p{Ll})"));
    t.replace(broken, QStringLiteral("\\1\\2"));
    t.remove(QChar(0x00AD));  // soft hyphens elsewhere
    t = t.simplified();
    if (max > 0 && t.size() > max) {
        qsizetype cut = t.lastIndexOf(QLatin1Char(' '), max);
        t = t.left(cut > max / 2 ? cut : max);
    }
    return t;
}

QUrl scholarUrl(const QString& text) {
    QUrl url(QStringLiteral("https://scholar.google.com/scholar"));
    const QString q = cleanText(text, SEARCH_CHARS);
    url.setQuery(QStringLiteral("q=") + QString::fromLatin1(QUrl::toPercentEncoding(q)), QUrl::StrictMode);
    return url;
}

const std::vector<Translator>& translators() {
    static const std::vector<Translator> list{
            {QStringLiteral("google"), QStringLiteral("Google Translate"),
             QStringLiteral("https://translate.google.com/?sl=auto&tl={lang}&text={text}&op=translate")},
            {QStringLiteral("deepl"), QStringLiteral("DeepL"),
             QStringLiteral("https://www.deepl.com/translator#auto/{lang}/{text}")},
            {QStringLiteral("bing"), QStringLiteral("Bing Translator"),
             QStringLiteral("https://www.bing.com/translator?from=auto-detect&to={lang}&text={text}")},
    };
    return list;
}

QString translatorPattern(const QString& setting) {
    const QString s = setting.trimmed();
    for (const Translator& t: translators()) {
        if (t.key == s) {
            return t.pattern;
        }
    }
    const QUrl url(QString(s).replace(QStringLiteral("{text}"), QStringLiteral("x"))
                           .replace(QStringLiteral("{lang}"), QStringLiteral("en")));
    return s.contains(QStringLiteral("{text}")) && isWebAddress(url) ? s : QString();
}

QUrl translateUrl(const QString& pattern, const QString& text, const QString& language) {
    if (pattern.isEmpty()) {
        return {};
    }
    const QString lang = language.isEmpty() ? systemLanguage() : language;
    QString address = pattern;
    address.replace(QStringLiteral("{lang}"), QString::fromLatin1(QUrl::toPercentEncoding(lang)));
    address.replace(QStringLiteral("{text}"),
                    QString::fromLatin1(QUrl::toPercentEncoding(cleanText(text, TRANSLATE_CHARS))));
    return QUrl::fromEncoded(address.toUtf8(), QUrl::StrictMode);
}

QString systemLanguage() {
    const QLocale locale = QLocale::system();
    const QString name = locale.name();  // "de_DE", "zh_CN", "C"
    QString lang = name.section(QLatin1Char('_'), 0, 0);
    if (lang.isEmpty() || lang == QLatin1String("C")) {
        return QStringLiteral("en");
    }
    if (lang == QLatin1String("zh")) {
        // Translators tell the two scripts apart
        return locale.script() == QLocale::TraditionalChineseScript ? QStringLiteral("zh-TW") : QStringLiteral("zh-CN");
    }
    return lang;
}

bool isWebAddress(const QUrl& url) {
    const QString scheme = url.scheme().toLower();
    return url.isValid() && (scheme == QLatin1String("http") || scheme == QLatin1String("https")) &&
           !url.host().isEmpty();
}

// --- the title of a bibliography entry --------------------------------------------------------------------------

namespace {
/// Words that end with a period without ending a sentence (lower case, without the period)
bool abbreviation(const QString& word) {
    static const QSet<QString> list{
            QStringLiteral("vs"),   QStringLiteral("e.g"),  QStringLiteral("i.e"),  QStringLiteral("etc"),
            QStringLiteral("al"),   QStringLiteral("no"),   QStringLiteral("vol"),  QStringLiteral("pp"),
            QStringLiteral("fig"),  QStringLiteral("eq"),   QStringLiteral("dr"),   QStringLiteral("prof"),
            QStringLiteral("u.a"),  QStringLiteral("z.b"),  QStringLiteral("bzw"),  QStringLiteral("ca"),
            QStringLiteral("vgl"),  QStringLiteral("inc"),  QStringLiteral("ltd"),  QStringLiteral("jr"),
            QStringLiteral("sr"),   QStringLiteral("st"),   QStringLiteral("nr"),   QStringLiteral("hrsg"),
            QStringLiteral("aufl"), QStringLiteral("bd"),   QStringLiteral("ed"),   QStringLiteral("eds"),
            QStringLiteral("proc"), QStringLiteral("conf"), QStringLiteral("int"),  QStringLiteral("trans"),
    };
    return list.contains(word.toLower());
}

/// Where the sentence that starts at `from` ends (the index after its last character: after a '?' or '!', before a
/// '.'), or the end of the text. A period after an initial ("A.") or an abbreviation does not end it.
qsizetype sentenceEnd(const QString& t, qsizetype from) {
    for (qsizetype i = from; i < t.size(); ++i) {
        const QChar c = t[i];
        if ((c != QLatin1Char('.') && c != QLatin1Char('?') && c != QLatin1Char('!')) ||
            (i + 1 < t.size() && t[i + 1] != QLatin1Char(' '))) {
            continue;
        }
        if (c != QLatin1Char('.')) {
            return i + 1;
        }
        const qsizetype start = t.lastIndexOf(QLatin1Char(' '), i - 1) + 1;
        const QString word = t.mid(std::max(start, from), i - std::max(start, from));
        if ((word.size() == 1 && word[0].isUpper()) || abbreviation(word)) {
            continue;
        }
        return i;
    }
    return t.size();
}

/// A title as found: without quotes, spaces and the punctuation around it (a '?' or '!' at its end stays).
QString trimmedTitle(QString t) {
    static const QString marks = QStringLiteral(" \"'\u201C\u201D\u201E\u201A\u2018\u2019\u00AB\u00BB,.;:");
    while (!t.isEmpty() && marks.contains(t.front())) {
        t.remove(0, 1);
    }
    while (!t.isEmpty() && marks.contains(t.back())) {
        t.chop(1);
    }
    return t;
}

int wordCount(const QString& t) { return static_cast<int>(t.split(QLatin1Char(' '), Qt::SkipEmptyParts).size()); }

/// A segment of an entry that is (part of) its author list: names, initials, "and", "et al.".
bool authorish(QString seg) {
    seg = seg.trimmed();
    static const QRegularExpression lead(QStringLiteral("^(and|und|&)\\s+"));
    seg.remove(lead);
    if (seg.isEmpty()) {
        return true;
    }
    static const QRegularExpression etAl(QStringLiteral("\\bet al\\b|\\bu\\.\\s?a\\b|\\bet\\.? al\\b"));
    if (etAl.match(seg).hasMatch()) {
        return true;
    }
    // An initial ("D", "Ch", "M.-W"), or names ending with one ("Kingma, D", "Tom B")
    static const QRegularExpression initial(QStringLiteral("^\\p{Lu}\\p{Ll}?$|^\\p{Lu}\\.?-\\p{Lu}$"));
    static const QRegularExpression endsInitial(QStringLiteral("[\\s,;]\\p{Lu}\\.?(-\\p{Lu})?$"));
    if (initial.match(seg).hasMatch() || endsInitial.match(seg).hasMatch()) {
        return true;
    }
    if (seg.contains(QRegularExpression(QStringLiteral("\\d")))) {
        return false;
    }
    // Names: every part (between commas, semicolons, "and", "&") has at most 4 words, all capitalised (or
    // particles, initials); without separators at most 3 words
    static const QRegularExpression separators(QStringLiteral("\\s*(?:[,;&]|\\band\\b|\\bund\\b)\\s*"));
    const QStringList parts = seg.split(separators, Qt::SkipEmptyParts);
    static const QSet<QString> particles{QStringLiteral("van"), QStringLiteral("von"), QStringLiteral("der"),
                                         QStringLiteral("den"), QStringLiteral("de"),  QStringLiteral("di"),
                                         QStringLiteral("da"),  QStringLiteral("la"),  QStringLiteral("le"),
                                         QStringLiteral("del"), QStringLiteral("dos"), QStringLiteral("du"),
                                         QStringLiteral("ten"), QStringLiteral("ter"), QStringLiteral("zu"),
                                         QStringLiteral("al"),  QStringLiteral("bin"), QStringLiteral("y")};
    if (parts.isEmpty() || (parts.size() == 1 && wordCount(parts.front()) > 3)) {
        return false;
    }
    for (const QString& part: parts) {
        const QStringList words = part.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (words.size() > 4) {
            return false;
        }
        for (const QString& w: words) {
            if (!w.front().isUpper() && !particles.contains(w)) {
                return false;
            }
        }
    }
    return true;
}

/// Only a year (or a date): "2017", "(2017a)", "Jun. 2016", "n.d."
bool yearish(const QString& seg) {
    static const QRegularExpression year(QStringLiteral(
            "^\\(?(\\d{4}[a-z]?|n\\.\\s?d\\.?)\\)?[.,:]?$|^\\p{Lu}\\p{Ll}{2,8}\\.?\\s+\\d{4}$"));
    return year.match(seg.trimmed()).hasMatch();
}

/// Where a paper appeared, not what it is called: "In Proc…", "arXiv preprint", "Journal of…", "vol. 3", numbers.
bool venueish(const QString& seg) {
    static const QRegularExpression venue(
            QStringLiteral("^(In\\b|In:|Proc|Proceedings|arXiv|ArXiv|CoRR|Journal|J\\.|IEEE\\s+Trans|ACM\\s+Trans|Trans\\.|"
                           "Advances in|Adv\\.|Conference|Conf\\.|Workshop|Symposium|Springer|Elsevier|Technical\\s+[Rr]eport|"
                           "Tech\\.|PhD|Ph\\.D|Master|Diss|Vol|vol|pp|S\\.\\s*\\d|Hrsg|Verlag|Available|URL|https?:|doi|DOI|"
                           "Retrieved|Accessed|Aufl|Bd\\.|Commun\\.)"));
    const QString s = seg.trimmed();
    if (venue.match(s).hasMatch() || s.contains(QStringLiteral("pp.")) || s.contains(QStringLiteral("vol."))) {
        return true;
    }
    const auto digits = std::count_if(s.begin(), s.end(), [](QChar c) { return c.isDigit(); });
    return s.isEmpty() || digits * 10 >= s.size() * 3;  // (mostly numbers: "521, 436–444 (2015)")
}

/// "Title, in Proc. …" (IEEE without quotes): the title before the venue
QString withoutVenue(QString t) {
    static const QRegularExpression in(QStringLiteral(",\\s+(in|In|In:|arXiv|Proc\\.)\\s"));
    if (const auto m = in.match(t); m.hasMatch() && m.capturedStart() > 10) {
        t.truncate(m.capturedStart());
    }
    return t;
}

/// "Authors: Title" (LNCS, German DIN styles): the part after the colon, if the part before it is authors
QString afterAuthorsColon(const QString& seg) {
    const qsizetype colon = seg.indexOf(QStringLiteral(": "));
    if (colon <= 0) {
        return {};
    }
    const QString before = seg.left(colon);
    static const QRegularExpression names(QStringLiteral("[,;&]|\\band\\b|\\bund\\b|\\p{Lu}\\.$|\\bal\\.$|\\ba\\.$"));
    if (!names.match(before).hasMatch() || !authorish(before.endsWith(QLatin1Char('.')) ? before.chopped(1) : before)) {
        return {};
    }
    return seg.mid(colon + 2);
}
}  // namespace

TitleGuess guessTitle(const QString& entry) {
    TitleGuess g;
    QString t = cleanText(entry);
    // The label: "[12]", "[Vas+17]", "(12)", "12."
    static const QRegularExpression label(QStringLiteral("^(\\[[^\\]]{1,16}\\]|\\(\\d{1,4}\\)|\\d{1,4}\\.)\\s+"));
    t.remove(label);
    g.raw = t;
    g.title = t;
    auto found = [&](const QString& title, const char* how) {
        const QString clean = trimmedTitle(withoutVenue(trimmedTitle(title)));
        if (wordCount(clean) < 1 || clean.size() < 4) {
            return false;
        }
        g.title = clean;
        g.how = QString::fromLatin1(how);
        return true;
    };

    // Quoted (IEEE, Chicago, many German styles): the first quoted part of two words or more
    static const QRegularExpression quoted(QStringLiteral(
            "\u201C([^\u201D]+)\u201D|\u201E([^\u201C\u201D]+)[\u201C\u201D]|\u201A([^\u2018\u2019]+)[\u2018\u2019]|"
            "\u00AB([^\u00BB]+)\u00BB|\u2018([^\u2019]+)\u2019|\"([^\"]+)\"|(?:^|\\s)'([^']+)'(?=[,.;:]?(?:\\s|$))"));
    for (auto it = quoted.globalMatch(t); it.hasNext();) {
        const auto m = it.next();
        for (int i = 1; i <= m.lastCapturedIndex(); ++i) {
            if (const QString q = m.captured(i); !q.isEmpty() && wordCount(trimmedTitle(q)) >= 2 && found(q, "quoted")) {
                return g;
            }
        }
    }

    // After the year: "(2019). Title." (APA; German "(2018): Title."), "Names. 2017. Title." (ACM)
    static const QRegularExpression year(QStringLiteral(
            "\\((?:\\d{4}[a-z]?|n\\.\\s?d\\.)\\)[.:,]?\\s+|\\.\\s\\d{4}[a-z]?\\.\\s+"));
    if (const auto m = year.match(t); m.hasMatch()) {
        const qsizetype from = m.capturedEnd();
        if (found(t.mid(from, sentenceEnd(t, from) - from), "year")) {
            return g;
        }
    }

    // The first sentence after the authors (sentences split at ". " - initials make short ones, which count as
    // authors), or the part after the authors' colon ("Vaswani, A., Shazeer, N.: Attention is all you need.")
    QStringList segments;
    {
        qsizetype start = 0;
        for (qsizetype i = 0; i < t.size(); ++i) {
            const QChar c = t[i];
            if ((c == QLatin1Char('.') || c == QLatin1Char('?') || c == QLatin1Char('!')) &&
                (i + 1 == t.size() || t[i + 1] == QLatin1Char(' '))) {
                // (not after an abbreviation that goes with the next word: "et al.", "vs.")
                const qsizetype w = t.lastIndexOf(QLatin1Char(' '), i - 1) + 1;
                const QString word = t.mid(w, i - w).toLower();
                if (c == QLatin1Char('.') && (word == QLatin1String("vs") || word == QLatin1String("e.g") ||
                                              word == QLatin1String("i.e"))) {
                    continue;
                }
                segments << t.mid(start, i - start + (c == QLatin1Char('.') ? 0 : 1)).trimmed();
                start = i + 1;
            }
        }
        if (start < t.size()) {
            segments << t.mid(start).trimmed();
        }
    }
    bool authors = true;
    for (const QString& seg: segments) {
        if (seg.isEmpty()) {
            continue;
        }
        if (const QString rest = afterAuthorsColon(seg); !rest.isEmpty() && found(rest, "colon")) {
            return g;
        }
        if (authors && authorish(seg)) {
            continue;
        }
        authors = false;
        if (yearish(seg) || venueish(seg)) {
            continue;
        }
        if (found(seg, "sentence")) {
            return g;
        }
    }
    return g;
}

// --- matching titles -----------------------------------------------------------------------------------------------

QStringList titleWords(const QString& text) {
    static const QSet<QString> stop{
            // English
            QStringLiteral("a"), QStringLiteral("an"), QStringLiteral("the"), QStringLiteral("of"), QStringLiteral("for"),
            QStringLiteral("and"), QStringLiteral("or"), QStringLiteral("in"), QStringLiteral("on"), QStringLiteral("at"),
            QStringLiteral("to"), QStringLiteral("with"), QStringLiteral("by"), QStringLiteral("from"), QStringLiteral("as"),
            QStringLiteral("is"), QStringLiteral("are"), QStringLiteral("be"), QStringLiteral("via"), QStringLiteral("into"),
            QStringLiteral("its"), QStringLiteral("it"), QStringLiteral("this"), QStringLiteral("that"),
            QStringLiteral("these"), QStringLiteral("using"), QStringLiteral("we"), QStringLiteral("our"),
            QStringLiteral("vs"), QStringLiteral("than"), QStringLiteral("do"),
            // German
            QStringLiteral("der"), QStringLiteral("die"), QStringLiteral("das"), QStringLiteral("den"), QStringLiteral("dem"),
            QStringLiteral("des"), QStringLiteral("ein"), QStringLiteral("eine"), QStringLiteral("einer"),
            QStringLiteral("eines"), QStringLiteral("einem"), QStringLiteral("einen"), QStringLiteral("und"),
            QStringLiteral("oder"), QStringLiteral("im"), QStringLiteral("ins"), QStringLiteral("an"), QStringLiteral("am"),
            QStringLiteral("auf"), QStringLiteral("aus"), QStringLiteral("bei"), QStringLiteral("mit"), QStringLiteral("nach"),
            QStringLiteral("von"), QStringLiteral("vom"), QStringLiteral("zu"), QStringLiteral("zum"), QStringLiteral("zur"),
            QStringLiteral("für"), QStringLiteral("über"), QStringLiteral("unter"), QStringLiteral("durch"),
            QStringLiteral("als"), QStringLiteral("ist"), QStringLiteral("sind"), QStringLiteral("wie"),
    };
    QStringList out;
    textmatch::words(QStringView(text), [&](qsizetype, qsizetype, QStringView word) {
        if (word.size() >= 2 && !stop.contains(word.toString())) {
            out << word.toString();
        }
    });
    return out;
}

double wordLikeness(const QString& a, const QString& b, int typos) {
    if (a == b) {
        return 1.0;
    }
    const QString& shorter = a.size() <= b.size() ? a : b;
    const QString& longer = a.size() <= b.size() ? b : a;
    if (shorter.size() >= 4 && longer.size() - shorter.size() <= 3 && longer.startsWith(shorter)) {
        return 0.9;
    }
    const int allowed = wordmatch::typosAllowed(static_cast<int>(shorter.size()), typos);
    if (allowed > 0 && wordmatch::editDistance(a, b, allowed) <= allowed) {
        return 0.7;
    }
    return 0.0;
}

namespace {
/// The share of `words` found in `in` (each word its best likeness)
double shareFound(const QStringList& words, const QStringList& in, const QSet<QString>& inSet, int typos) {
    if (words.isEmpty() || in.isEmpty()) {
        return 0.0;
    }
    double sum = 0;
    for (const QString& w: words) {
        if (inSet.contains(w)) {
            sum += 1.0;
            continue;
        }
        double best = 0;
        for (const QString& o: in) {
            best = std::max(best, wordLikeness(w, o, typos));
            if (best >= 0.9) {
                break;
            }
        }
        sum += best;
    }
    return sum / static_cast<double>(words.size());
}
}  // namespace

double titleMatch(const QStringList& query, const QStringList& candidate, bool titleLike, int typos) {
    if (query.isEmpty() || candidate.isEmpty()) {
        return 0.0;
    }
    const QSet<QString> candidateSet(candidate.begin(), candidate.end());
    const double found = shareFound(query, candidate, candidateSet, typos);
    double score = 0.9 * found;
    if (titleLike) {
        const QSet<QString> querySet(query.begin(), query.end());
        score = 0.75 * found + 0.25 * shareFound(candidate, query, querySet, typos);
    }
    return query.size() == 1 ? score * 0.8 : score;  // (one word says little)
}

double titleInText(const QStringList& title, const QSet<QString>& text, int typos) {
    if (title.size() < 3 || text.isEmpty()) {
        return 0.0;
    }
    return shareFound(title, QStringList(text.begin(), text.end()), text, typos);
}

}  // namespace xqt::cite
