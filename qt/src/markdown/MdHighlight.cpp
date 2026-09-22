#include "MdHighlight.h"

#ifdef XQT_HAVE_KSYNTAXHIGHLIGHTING

#include <algorithm>
#include <map>
#include <mutex>

#include <KSyntaxHighlighting/AbstractHighlighter>
#include <KSyntaxHighlighting/Definition>
#include <KSyntaxHighlighting/Format>
#include <KSyntaxHighlighting/Repository>
#include <KSyntaxHighlighting/State>
#include <KSyntaxHighlighting/Theme>
#include <QColor>
#include <QString>

namespace xqt::md {

namespace {
namespace ksh = KSyntaxHighlighting;

/// The repository is not thread-safe (definitions load lazily): one for all, used under a lock. Highlighting runs
/// seldom (layouts are cached), so the lock costs nothing noticeable.
std::mutex& lock() {
    static std::mutex m;
    return m;
}
ksh::Repository& repository() {
    static ksh::Repository* repo = new ksh::Repository();  // (never destroyed: render threads may outlive statics)
    return *repo;
}
ksh::Theme theme() {
    static const ksh::Theme t = [] {
        ksh::Theme github = repository().theme(QStringLiteral("GitHub Light"));
        return github.isValid() ? github : repository().defaultTheme(ksh::Repository::LightTheme);
    }();
    return t;
}

/// "python", "C++", "js", "sh": a definition by its name (any case), a common short name, or a file extension.
ksh::Definition definitionFor(const std::string& language) {
    static std::map<std::string, ksh::Definition> known;
    if (auto it = known.find(language); it != known.end()) {
        return it->second;
    }
    static const std::map<std::string, const char*> aliases = {
            {"c++", "C++"},    {"sh", "Bash"},     {"shell", "Bash"},       {"console", "Bash"}, {"zsh", "Zsh"},
            {"js", "JavaScript"}, {"ts", "TypeScript"}, {"py", "Python"}, {"yml", "YAML"},   {"md", "Markdown"},
            {"tex", "LaTeX"},  {"rust", "Rust"},   {"golang", "Go"},        {"cs", "C#"},        {"csharp", "C#"},
            {"objc", "Objective-C"}, {"kt", "Kotlin"}, {"rb", "Ruby"},      {"ps1", "PowerShell"}};
    const QString wanted = QString::fromStdString(language);
    const auto alias = aliases.find(wanted.toLower().toStdString());
    ksh::Definition def = repository().definitionForName(alias != aliases.end() ? QString(alias->second) : wanted);
    if (!def.isValid()) {
        for (const ksh::Definition& d: repository().definitions()) {
            if (d.name().compare(wanted, Qt::CaseInsensitive) == 0) {
                def = d;
                break;
            }
        }
    }
    if (!def.isValid()) {
        def = repository().definitionForFileName(QStringLiteral("code.") + wanted);
    }
    known.emplace(language, def);
    return def;
}

class Collector final: public ksh::AbstractHighlighter {
public:
    explicit Collector(std::vector<CodeSpan>& out): out(out) {}

    void run(const std::string& code) {
        ksh::State state;
        size_t lineStart = 0;
        while (lineStart <= code.size()) {
            size_t end = code.find('\n', lineStart);
            if (end == std::string::npos) {
                end = code.size();
            }
            line = QString::fromUtf8(code.data() + lineStart, static_cast<qsizetype>(end - lineStart));
            // UTF-16 index -> byte offset in the line
            bytes.assign(static_cast<size_t>(line.size()) + 1, 0);
            for (qsizetype i = 0; i < line.size(); ++i) {
                const bool pair = line.at(i).isHighSurrogate() && i + 1 < line.size();
                const int n = pair ? 4 : static_cast<int>(QString(line.at(i)).toUtf8().size());
                bytes[static_cast<size_t>(i) + 1] = bytes[static_cast<size_t>(i)] + n;
                if (pair) {
                    ++i;
                    bytes[static_cast<size_t>(i) + 1] = bytes[static_cast<size_t>(i)];
                }
            }
            byteOffset = static_cast<int>(lineStart);
            state = highlightLine(line, state);
            lineStart = end + 1;
        }
    }

protected:
    void applyFormat(int offset, int length, const ksh::Format& format) override {
        if (length <= 0 || !format.isValid() || format.isDefaultTextStyle(theme())) {
            return;
        }
        const auto from = static_cast<size_t>(std::clamp(offset, 0, static_cast<int>(line.size())));
        const auto to = static_cast<size_t>(std::clamp(offset + length, 0, static_cast<int>(line.size())));
        CodeSpan s;
        s.start = byteOffset + bytes[from];
        s.length = bytes[to] - bytes[from];
        if (format.hasTextColor(theme())) {
            const QColor c = format.textColor(theme());
            s.color = Color(static_cast<uint8_t>(c.red()), static_cast<uint8_t>(c.green()),
                            static_cast<uint8_t>(c.blue()));
        } else {
            s.color = Color(0, 0, 0);
        }
        s.bold = format.isBold(theme());
        s.italic = format.isItalic(theme());
        if (s.length > 0) {
            out.push_back(s);
        }
    }

private:
    std::vector<CodeSpan>& out;
    QString line;
    std::vector<int> bytes;
    int byteOffset = 0;
};
}  // namespace

std::vector<CodeSpan> highlight(const std::string& code, const std::string& language) {
    std::vector<CodeSpan> spans;
    if (language.empty() || code.empty()) {
        return spans;
    }
    std::lock_guard guard(lock());
    const ksh::Definition def = definitionFor(language);
    if (!def.isValid()) {
        return spans;
    }
    Collector c(spans);
    c.setDefinition(def);
    c.setTheme(theme());
    c.run(code);
    return spans;
}

bool highlightingAvailable() { return true; }

}  // namespace xqt::md

#else

namespace xqt::md {
std::vector<CodeSpan> highlight(const std::string&, const std::string&) { return {}; }
bool highlightingAvailable() { return false; }
}  // namespace xqt::md

#endif
