#include "ImportCheck.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>

namespace xqt::plugins {

namespace {
struct Token {
    enum class Kind { Word, String, Punct, Other };
    Kind kind;
    QString text;
};

bool isWordChar(QChar c) { return c.isLetterOrNumber() || c == '_' || c == '$'; }

/// Whether a '/' at this point starts a regular expression (after an operator, '(' ',' '=' …, or a keyword)
bool regexAllowed(const std::vector<Token>& tokens) {
    if (tokens.empty()) {
        return true;
    }
    const Token& t = tokens.back();
    if (t.kind == Token::Kind::Punct) {
        return t.text != ")" && t.text != "]" && t.text != "}";
    }
    if (t.kind == Token::Kind::Word) {
        static const QSet<QString> keywords{"return", "typeof", "instanceof", "in", "of", "new", "delete",
                                            "void", "throw", "case", "do", "else", "yield", "await"};
        return keywords.contains(t.text);
    }
    return false;
}

std::vector<Token> tokenize(const QString& s) {
    std::vector<Token> out;
    const int n = static_cast<int>(s.size());
    int i = 0;
    auto skipString = [&](QChar quote) {
        QString value;
        ++i;
        while (i < n && s[i] != quote) {
            if (s[i] == '\\' && i + 1 < n) {
                value += s[i + 1];
                i += 2;
                continue;
            }
            value += s[i++];
        }
        ++i;
        return value;
    };
    while (i < n) {
        const QChar c = s[i];
        if (c.isSpace()) {
            ++i;
        } else if (c == '/' && i + 1 < n && s[i + 1] == '/') {
            while (i < n && s[i] != '\n') {
                ++i;
            }
        } else if (c == '/' && i + 1 < n && s[i + 1] == '*') {
            const int end = s.indexOf(QLatin1String("*/"), i + 2);
            i = end < 0 ? n : end + 2;
        } else if (c == '"' || c == '\'') {
            out.push_back({Token::Kind::String, skipString(c)});
        } else if (c == '`') {
            // (a template literal: its ${…} parts are code, but an import inside one is a dynamic one anyway)
            ++i;
            int depth = 0;
            while (i < n && !(s[i] == '`' && depth == 0)) {
                if (s[i] == '\\') {
                    i += 2;
                    continue;
                }
                if (s[i] == '$' && i + 1 < n && s[i + 1] == '{') {
                    ++depth;
                    out.push_back({Token::Kind::Other, "${"});
                } else if (s[i] == '}' && depth > 0) {
                    --depth;
                } else if (depth > 0 && s.mid(i, 6) == QLatin1String("import")) {
                    out.push_back({Token::Kind::Word, "import"});
                    out.push_back({Token::Kind::Punct, "("});  // (counted as dynamic: refused)
                }
                ++i;
            }
            ++i;
            out.push_back({Token::Kind::String, QString()});
        } else if (c == '/' && regexAllowed(out)) {
            ++i;
            bool inClass = false;
            while (i < n && (s[i] != '/' || inClass) && s[i] != '\n') {
                if (s[i] == '\\') {
                    ++i;
                } else if (s[i] == '[') {
                    inClass = true;
                } else if (s[i] == ']') {
                    inClass = false;
                }
                ++i;
            }
            ++i;
            while (i < n && isWordChar(s[i])) {
                ++i;  // (flags)
            }
            out.push_back({Token::Kind::Other, "/re/"});
        } else if (isWordChar(c)) {
            const int start = i;
            while (i < n && isWordChar(s[i])) {
                ++i;
            }
            out.push_back({Token::Kind::Word, s.mid(start, i - start)});
        } else {
            out.push_back({Token::Kind::Punct, QString(c)});
            ++i;
        }
    }
    return out;
}
}  // namespace

QStringList importsOf(const QString& source, bool* dynamic) {
    const std::vector<Token> t = tokenize(source);
    QStringList out;
    bool dyn = false;
    for (size_t i = 0; i < t.size(); ++i) {
        if (t[i].kind != Token::Kind::Word) {
            continue;
        }
        const bool afterDot = i > 0 && t[i - 1].kind == Token::Kind::Punct && t[i - 1].text == ".";
        if (afterDot) {
            continue;  // (obj.import, obj.from)
        }
        if (t[i].text == "import") {
            if (i + 1 < t.size() && t[i + 1].kind == Token::Kind::Punct && t[i + 1].text == "(") {
                dyn = true;
            } else if (i + 1 < t.size() && t[i + 1].kind == Token::Kind::String) {
                out << t[i + 1].text;  // import "x"
            }
        } else if (t[i].text == "from" && i + 1 < t.size() && t[i + 1].kind == Token::Kind::String) {
            out << t[i + 1].text;  // import … from "x", export … from "x"
        }
    }
    if (dynamic) {
        *dynamic = dyn;
    }
    return out;
}

QString checkImports(const QString& folder, const QString& main) {
    const QString root = QFileInfo(folder).canonicalFilePath();
    if (root.isEmpty()) {
        return QStringLiteral("the plugin's folder is missing");
    }
    auto inside = [&root](const QString& path) {
        const QString c = QFileInfo(path).canonicalFilePath();
        return !c.isEmpty() && (c == root || c.startsWith(root + '/'));
    };
    QStringList todo{QDir(root).filePath(main)};
    QSet<QString> seen;
    while (!todo.isEmpty()) {
        const QString file = todo.takeFirst();
        const QString name = QDir(root).relativeFilePath(file);
        if (!inside(file)) {
            return QStringLiteral("%1 is outside the plugin's folder (or missing)").arg(name);
        }
        const QString canonical = QFileInfo(file).canonicalFilePath();
        if (seen.contains(canonical)) {
            continue;
        }
        seen.insert(canonical);
        QFile f(canonical);
        if (!f.open(QIODevice::ReadOnly)) {
            return QStringLiteral("%1 cannot be read").arg(name);
        }
        bool dynamic = false;
        const QStringList imports = importsOf(QString::fromUtf8(f.readAll()), &dynamic);
        if (dynamic) {
            return QStringLiteral("%1: import() is not allowed (only static imports of the plugin's files)").arg(name);
        }
        for (const QString& spec: imports) {
            if (spec == QLatin1String("xournal")) {
                continue;
            }
            if (!spec.startsWith(QLatin1String("./")) && !spec.startsWith(QLatin1String("../"))) {
                return QStringLiteral("%1: imports \"%2\" (only \"xournal\" and files of the plugin, \"./…\")")
                        .arg(name, spec);
            }
            const QString target = QFileInfo(file).dir().filePath(spec);
            if (!inside(target)) {
                return QStringLiteral("%1: imports %2, outside the plugin's folder (or missing)").arg(name, spec);
            }
            todo << QFileInfo(target).canonicalFilePath();
        }
    }
    return {};
}

}  // namespace xqt::plugins
