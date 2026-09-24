#include "FuzzyQuery.h"

#include <algorithm>
#include <atomic>

#include <QCoreApplication>

#include "FuzzyMatch.h"
#include "WordMatch.h"

namespace xqt {

namespace {
constexpr QChar SPACE_MARK(0xE000);   // "\ " while splitting
constexpr QChar OPEN_MARK(0xE001);    // "\("
constexpr QChar CLOSE_MARK(0xE002);   // "\)"

QString tr(const char* text) { return QCoreApplication::translate("FuzzyQuery", text); }

std::atomic<int> typoSetting{wordmatch::DEFAULT_TYPOS};
}  // namespace

/// Tokens, then a recursive descent: and := or (' ' or)* ; or := unary ('|' unary)* ; unary := '(' and ')' |
/// '!(' and ')' | term.
class FuzzyQueryParser {
public:
    struct Token {
        enum Kind { Open, OpenNot, Close, Bar, Word } kind;
        QString text;
    };

    explicit FuzzyQueryParser(FuzzyQuery& q): q(q) {}

    void run() {
        tokenize();
        if (tokens.empty()) {
            return;
        }
        const int top = parseAnd(false);
        if (q.error.isEmpty() && pos < tokens.size()) {
            fail(tr("A ) without its ("));
        }
        if (!q.error.isEmpty()) {
            q.list.clear();
            q.nodes.clear();
            q.root = -1;
            return;
        }
        q.root = top;
        q.polarity.assign(q.list.size(), 0);
        if (top >= 0) {
            q.markPolarity(top, true);
        }
    }

private:
    void fail(const QString& why) {
        if (q.error.isEmpty()) {
            q.error = why;
        }
    }

    void tokenize() {
        QString s = q.text;
        s.replace(QStringLiteral("\\ "), QString(SPACE_MARK));
        s.replace(QStringLiteral("\\("), QString(OPEN_MARK));
        s.replace(QStringLiteral("\\)"), QString(CLOSE_MARK));
        for (const QString& raw: s.simplified().split(u' ', Qt::SkipEmptyParts)) {
            qsizetype i = 0;
            for (;;) {
                if (raw.mid(i).startsWith(QStringLiteral("!("))) {
                    tokens.push_back({Token::OpenNot, {}});
                    i += 2;
                } else if (i < raw.size() && raw[i] == u'(') {
                    tokens.push_back({Token::Open, {}});
                    ++i;
                } else {
                    break;
                }
            }
            qsizetype j = raw.size();
            int closers = 0;
            while (j > i && raw[j - 1] == u')') {
                --j;
                ++closers;
            }
            QString middle = raw.mid(i, j - i);
            if (middle == QStringLiteral("|")) {
                tokens.push_back({Token::Bar, {}});
            } else if (!middle.isEmpty()) {
                middle.replace(SPACE_MARK, u' ').replace(OPEN_MARK, u'(').replace(CLOSE_MARK, u')');
                tokens.push_back({Token::Word, middle});
            }
            for (int c = 0; c < closers; ++c) {
                tokens.push_back({Token::Close, {}});
            }
        }
    }

    bool at(Token::Kind k) const { return pos < tokens.size() && tokens[pos].kind == k; }

    int add(FuzzyQuery::Node n) {
        q.nodes.push_back(std::move(n));
        return static_cast<int>(q.nodes.size()) - 1;
    }

    /// A node of several (left out: the ones that are -1; one left: itself)
    int combine(FuzzyQuery::Node::Op op, std::vector<int> kids) {
        std::erase(kids, -1);
        if (kids.empty()) {
            return -1;
        }
        if (kids.size() == 1) {
            return kids.front();
        }
        return add({op, -1, std::move(kids)});
    }

    /// `nested`: in parentheses
    int parseAnd(bool nested) {
        std::vector<int> kids;
        bool any = false;
        while (pos < tokens.size() && !at(Token::Close) && q.error.isEmpty()) {
            kids.push_back(parseOr());
            any = true;
        }
        if (!any && nested && at(Token::Close)) {
            fail(tr("Empty ( )"));
        }
        return combine(FuzzyQuery::Node::And, std::move(kids));
    }

    int parseOr() {
        std::vector<int> kids{parseUnary()};
        while (at(Token::Bar) && q.error.isEmpty()) {
            ++pos;
            if (pos >= tokens.size() || at(Token::Bar) || at(Token::Close)) {
                fail(tr("A | without a term on both sides"));
                return -1;
            }
            kids.push_back(parseUnary());
        }
        return combine(FuzzyQuery::Node::Or, std::move(kids));
    }

    int parseUnary() {
        if (pos >= tokens.size()) {
            fail(tr("A | without a term on both sides"));
            return -1;
        }
        const Token& t = tokens[pos];
        switch (t.kind) {
            case Token::Bar:
                fail(tr("A | without a term on both sides"));
                return -1;
            case Token::Close:
                fail(tr("A ) without its ("));
                return -1;
            case Token::Open:
            case Token::OpenNot: {
                ++pos;
                const int inner = parseAnd(true);
                if (!q.error.isEmpty()) {
                    return -1;
                }
                if (!at(Token::Close)) {
                    fail(tr("A ( is not closed"));
                    return -1;
                }
                ++pos;
                if (t.kind == Token::OpenNot && inner >= 0) {
                    return add({FuzzyQuery::Node::Not, -1, {inner}});
                }
                return inner;
            }
            case Token::Word:
                ++pos;
                return term(t.text);
        }
        return -1;
    }

    /// A term with fzf's marks (pattern.go, parseTerms)
    int term(QString s) {
        FuzzyQuery::Term t;
        if (s.startsWith(u'!')) {
            t.negated = true;
            t.type = FuzzyQuery::Type::Exact;
            s.remove(0, 1);
        }
        if (s != QStringLiteral("$") && s.endsWith(u'$')) {
            t.type = FuzzyQuery::Type::Suffix;
            s.chop(1);
        }
        if (s.size() > 2 && s.startsWith(u'\'') && s.endsWith(u'\'')) {
            t.type = FuzzyQuery::Type::Boundary;
            s = s.mid(1, s.size() - 2);
        } else if (s.startsWith(u'\'')) {
            t.type = t.negated ? FuzzyQuery::Type::Fuzzy : FuzzyQuery::Type::Exact;  // (flips exactness)
            s.remove(0, 1);
        } else if (s.startsWith(u'^')) {
            t.type = t.type == FuzzyQuery::Type::Suffix ? FuzzyQuery::Type::Equal : FuzzyQuery::Type::Prefix;
            s.remove(0, 1);
        }
        t.text = textmatch::prepare(s);
        t.typos = FuzzyQuery::typoTolerance();
        if (t.text.isEmpty()) {
            return -1;  // (left out, as fzf leaves it out)
        }
        q.list.push_back(std::move(t));
        return add({FuzzyQuery::Node::TermOp, static_cast<int>(q.list.size()) - 1, {}});
    }

    FuzzyQuery& q;
    std::vector<Token> tokens;
    size_t pos = 0;
};

textmatch::Term FuzzyQuery::Term::textTerm() const {
    switch (type) {
        case Type::Prefix: return {text, textmatch::WordStart};
        case Type::Suffix: return {text, textmatch::WordEnd};
        case Type::Equal:
        case Type::Boundary: return {text, textmatch::Word};
        case Type::Fuzzy:
            if (wordmatch::perWord(text)) {
                return {text, textmatch::Fuzzy | textmatch::typoBits(typos)};
            }
            return {text, textmatch::Anywhere};
        default: return {text, textmatch::Anywhere};
    }
}

FuzzyQuery::FuzzyQuery(const QString& query): text(query) { FuzzyQueryParser(*this).run(); }

void FuzzyQuery::setTypoTolerance(int typos) { typoSetting = std::clamp(typos, 0, wordmatch::MAX_TYPOS); }

int FuzzyQuery::typoTolerance() { return typoSetting.load(); }

void FuzzyQuery::markPolarity(int node, bool positive) {
    const Node& n = nodes[static_cast<size_t>(node)];
    switch (n.op) {
        case Node::TermOp: {
            const auto t = static_cast<size_t>(n.term);
            polarity[t] = positive != list[t].negated;
            break;
        }
        case Node::Not: markPolarity(n.kids.front(), !positive); break;
        default:
            for (const int k: n.kids) {
                markPolarity(k, positive);
            }
    }
}

bool FuzzyQuery::eval(int node, const std::function<bool(size_t)>& found) const {
    const Node& n = nodes[static_cast<size_t>(node)];
    switch (n.op) {
        case Node::TermOp: {
            const auto t = static_cast<size_t>(n.term);
            return found(t) != list[t].negated;
        }
        case Node::Not: return !eval(n.kids.front(), found);
        case Node::And:
            return std::all_of(n.kids.begin(), n.kids.end(), [&](int k) { return eval(k, found); });
        case Node::Or:
            return std::any_of(n.kids.begin(), n.kids.end(), [&](int k) { return eval(k, found); });
    }
    return false;
}

bool FuzzyQuery::evaluate(const std::function<bool(size_t)>& found) const {
    return isValid() && eval(root, found);
}

std::vector<textmatch::Term> FuzzyQuery::markTerms() const {
    std::vector<textmatch::Term> out;
    for (size_t i = 0; i < list.size(); ++i) {
        if (positive(i)) {
            const textmatch::Term t = list[i].textTerm();
            if (std::find(out.begin(), out.end(), t) == out.end()) {
                out.push_back(t);
            }
        }
    }
    return out;
}

namespace {
fuzzy::Result matchOne(QStringView s, const FuzzyQuery::Term& t) {
    switch (t.type) {
        case FuzzyQuery::Type::Fuzzy: return fuzzy::match(s, t.text);
        case FuzzyQuery::Type::Exact: return fuzzy::exact(s, t.text);
        case FuzzyQuery::Type::Boundary: return fuzzy::exact(s, t.text, true);
        case FuzzyQuery::Type::Prefix: return fuzzy::prefix(s, t.text);
        case FuzzyQuery::Type::Suffix: return fuzzy::suffix(s, t.text);
        case FuzzyQuery::Type::Equal: return fuzzy::equal(s, t.text);
    }
    return {};
}
}  // namespace

FuzzyQuery::NameMatch FuzzyQuery::matchName(QStringView name, QStringView folder) const {
    NameMatch m;
    m.found.assign(list.size(), 0);
    QString path;
    for (size_t i = 0; i < list.size(); ++i) {
        const Term& t = list[i];
        fuzzy::Result r = matchOne(name, t);
        int offset = 0;
        int weight = 1;
        if (!r.matched() && !folder.isEmpty()) {
            if (path.isEmpty()) {
                path = folder.toString() + u'/' + name.toString();
            }
            r = matchOne(path, t);
            offset = static_cast<int>(folder.size()) + 1;
            weight = 4;
        }
        if (!r.matched()) {
            continue;
        }
        m.found[i] = 1;
        if (positive(i)) {
            m.score += std::max(1, r.score / weight);
            for (const int p: r.positions) {
                if (p >= offset) {
                    m.positions.push_back(p - offset);
                }
            }
        }
    }
    std::sort(m.positions.begin(), m.positions.end());
    m.positions.erase(std::unique(m.positions.begin(), m.positions.end()), m.positions.end());
    return m;
}

bool FuzzyQuery::matchesName(QStringView name, QStringView folder) const {
    const NameMatch m = matchName(name, folder);
    return evaluate([&](size_t t) { return m.found[t] != 0; });
}

std::vector<textmatch::Term> FuzzyQuery::textTerms(const QString& query, bool fuzzy) {
    if (fuzzy) {
        const FuzzyQuery q(query);
        if (q.isValid()) {
            return q.markTerms();
        }
    }
    const QString plain = textmatch::prepare(query);
    if (plain.isEmpty()) {
        return {};
    }
    return {{plain, textmatch::Anywhere}};
}

}  // namespace xqt
