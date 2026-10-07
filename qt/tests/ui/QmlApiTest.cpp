/*
 * xournal-qt: the names the QML uses on `app` exist on the C++ side. `app` is an untyped context property
 * (src/app/EngineSetup.h): a renamed or removed AppController member does not fail when the QML is compiled or
 * loaded, it reads `undefined` at run time (or throws when called) where the user meets it. This test reads every
 * .qml and .js file of the UI and checks `app.<a>` against AppController's meta-object (properties, methods,
 * signals), `app.<a>.<b>` against the meta-object of the object `app.<a>` holds (the sub-objects: app.library,
 * app.reference, …) and `app.<a>.<b>.<c>` likewise (app.reference.edit.…). It also checks the interface the pills
 * read from their `target` (CanvasActions: app.edit, or app.reference.edit in the reference view).
 *
 * @license GNU GPLv2 or later
 */
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <regex>
#include <set>
#include <string>
#include <vector>

#include <QMetaMethod>
#include <QtGlobal>
#include <QMetaObject>
#include <QMetaProperty>
#include <gtest/gtest.h>

#include "shell/CanvasActions.h"

#include "AppController.h"

namespace fs = std::filesystem;

namespace {

/// The code of a QML or JS file without its comments and the text of its strings (blanked with spaces, so that line
/// numbers stay): "app.x" in a comment or a message is not a use. Template strings keep their ${…} parts; regular
/// expression literals are blanked too (a quote in one would start a string).
std::string codeOnly(const std::string& src) {
    std::string out = src;
    enum class S { Code, Line, Block, Single, Double, Template, Regex, RegexClass };
    S s = S::Code;
    std::vector<int> templateDepth;  // for each open ${ inside a template string: the braces opened in it
    auto blank = [&](size_t i) {
        if (out[i] != '\n') {
            out[i] = ' ';
        }
    };
    char prev = '\n';  // the last character of code that is not a space (for telling a regex from a division)
    for (size_t i = 0; i < src.size(); ++i) {
        const char c = src[i];
        const char next = i + 1 < src.size() ? src[i + 1] : '\0';
        switch (s) {
            case S::Code:
                if (c == '/' && next == '/') {
                    s = S::Line;
                    blank(i);
                } else if (c == '/' && next == '*') {
                    s = S::Block;
                    blank(i);
                } else if (c == '/' && std::string("(,=:[!&|?{};\n").find(prev) != std::string::npos) {
                    s = S::Regex;
                    blank(i);
                } else if (c == '\'') {
                    s = S::Single;
                } else if (c == '"') {
                    s = S::Double;
                } else if (c == '`') {
                    s = S::Template;
                } else if (c == '{' && !templateDepth.empty()) {
                    ++templateDepth.back();
                } else if (c == '}' && !templateDepth.empty()) {
                    if (templateDepth.back()-- == 0) {
                        templateDepth.pop_back();
                        s = S::Template;
                    }
                }
                if (s == S::Code && c != ' ' && c != '\t') {
                    prev = c;
                }
                break;
            case S::Line:
                if (c == '\n') {
                    s = S::Code;
                    prev = '\n';
                } else {
                    blank(i);
                }
                break;
            case S::Block:
                blank(i);
                if (c == '*' && next == '/') {
                    blank(++i);
                    s = S::Code;
                }
                break;
            case S::Single:
            case S::Double:
                if (c == '\\') {
                    blank(i);
                    if (i + 1 < src.size()) {
                        blank(++i);
                    }
                } else if ((s == S::Single && c == '\'') || (s == S::Double && c == '"') || c == '\n') {
                    s = S::Code;
                    prev = 'x';
                } else {
                    blank(i);
                }
                break;
            case S::Template:
                if (c == '\\') {
                    blank(i);
                    if (i + 1 < src.size()) {
                        blank(++i);
                    }
                } else if (c == '`') {
                    s = S::Code;
                    prev = 'x';
                } else if (c == '$' && next == '{') {
                    ++i;
                    templateDepth.push_back(0);
                    s = S::Code;
                    prev = '{';
                } else {
                    blank(i);
                }
                break;
            case S::Regex:
            case S::RegexClass:
                if (c == '\\') {
                    blank(i);
                    if (i + 1 < src.size()) {
                        blank(++i);
                    }
                } else if (s == S::Regex && c == '[') {
                    s = S::RegexClass;
                    blank(i);
                } else if (s == S::RegexClass && c == ']') {
                    s = S::Regex;
                    blank(i);
                } else if (s == S::Regex && c == '/') {
                    blank(i);
                    s = S::Code;
                    prev = 'x';
                } else if (c == '\n') {  // (not a regex after all)
                    s = S::Code;
                    prev = '\n';
                } else {
                    blank(i);
                }
                break;
        }
    }
    return out;
}

struct Use {
    std::string file;
    int line = 0;
    std::string first;   ///< app.<first>
    std::string second;  ///< app.<first>.<second> ("" when there is none)
    std::string third;   ///< app.<first>.<second>.<third> ("" when there is none)
};

int lineOf(const std::string& text, size_t pos) {
    return 1 + static_cast<int>(std::count(text.begin(), text.begin() + static_cast<std::ptrdiff_t>(pos), '\n'));
}

/// Every `app.a`, `app.a.b` and `app.a.b.c` in the code (not `x.app.a`: a member called app of something else).
std::vector<Use> appUses(const std::string& file, const std::string& source) {
    const std::string code = codeOnly(source);
    static const std::regex use(R"((^|[^\w.$])app\s*\.\s*([A-Za-z_$][\w$]*)(\s*\.\s*([A-Za-z_$][\w$]*))?)"
                                R"((\s*\.\s*([A-Za-z_$][\w$]*))?)");
    std::vector<Use> uses;
    for (auto it = std::sregex_iterator(code.begin(), code.end(), use); it != std::sregex_iterator(); ++it) {
        const auto& m = *it;
        uses.push_back({file, lineOf(code, static_cast<size_t>(m.position(2))), m[2].str(), m[4].str(), m[6].str()});
    }
    return uses;
}

/// Every name a pill reads from its `target` (`target.x` or `pill.target.x`).
std::vector<Use> targetUses(const std::string& file, const std::string& source) {
    const std::string code = codeOnly(source);
    static const std::regex use(R"((^|[^\w.$]|\bpill\s*\.\s*)target\s*\.\s*([A-Za-z_$][\w$]*))");
    std::vector<Use> uses;
    for (auto it = std::sregex_iterator(code.begin(), code.end(), use); it != std::sregex_iterator(); ++it) {
        const auto& m = *it;
        uses.push_back({file, lineOf(code, static_cast<size_t>(m.position(2))), m[2].str(), {}});
    }
    return uses;
}

std::string readAll(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

/// The folder of the UI's QML (XQT_QML_API_DIR: another one, e.g. a copy with a name changed by hand).
fs::path qmlFolder() { return qEnvironmentVariable("XQT_QML_API_DIR", XQT_QML_SOURCE_DIR).toStdString(); }

/// The QML and JS files of the UI (also in folders below).
std::vector<fs::path> uiFiles() {
    std::vector<fs::path> files;
    for (const auto& e: fs::recursive_directory_iterator(qmlFolder())) {
        if (e.is_regular_file() && (e.path().extension() == ".qml" || e.path().extension() == ".js")) {
            files.push_back(e.path());
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

/// Whether the meta-object has a property, method or signal of that name (what QML can reach through it).
bool has(const QMetaObject* mo, const std::string& name) {
    if (!mo) {
        return false;
    }
    if (mo->indexOfProperty(name.c_str()) >= 0) {
        return true;
    }
    for (int i = 0; i < mo->methodCount(); ++i) {
        const QMetaMethod m = mo->method(i);
        if (m.access() != QMetaMethod::Private && m.name() == QByteArray::fromStdString(name)) {
            return true;
        }
    }
    return false;
}

/// The meta-object of what `<object>.<name>` holds when it is an object (the live one: most are declared as
/// QObject*); `live`: that object, when there is one.
const QMetaObject* objectBehind(QObject* object, const std::string& name, QObject** live = nullptr) {
    if (live) {
        *live = nullptr;
    }
    if (!object) {
        return nullptr;
    }
    const QMetaObject* mo = object->metaObject();
    const int p = mo->indexOfProperty(name.c_str());
    if (p < 0) {
        return nullptr;
    }
    const QMetaProperty prop = mo->property(p);
    if (!(prop.metaType().flags() & QMetaType::PointerToQObject)) {
        return nullptr;
    }
    if (QObject* o = prop.read(object).value<QObject*>()) {
        if (live) {
            *live = o;
        }
        return o->metaObject();
    }
    return prop.metaType().metaObject();
}

std::string where(const Use& u) { return fs::path(u.file).filename().string() + ":" + std::to_string(u.line); }

/// Names on `app` that are not members on purpose, each with why.
const std::map<std::string, std::string>& dynamicFirst() {
    static const std::map<std::string, std::string> names{
            // (none today: add a name here only when it is reached on purpose without a C++ member, and say why)
    };
    return names;
}
/// `app.<a>.<b>` that are not members of the sub-object on purpose ("a.b"), each with why.
const std::map<std::string, std::string>& dynamicSecond() {
    static const std::map<std::string, std::string> names{
            // (none today)
    };
    return names;
}

}  // namespace

// The checker itself: a name that does not exist is found; comments, strings and other objects' `app` are not uses
TEST(QmlApiTest, theCheckerFindsUsesAndIgnoresCommentsAndStrings) {
    const std::string qml = "Item {\n"
                            "    // app.inAComment\n"
                            "    property string s: \"app.inAString\" + 'app.inAnother'\n"
                            "    /* app.inABlock\n"
                            "       app.stillInIt */\n"
                            "    property var r: s.replace(/[\"']app.inARegex/g, \"\")\n"
                            "    property var m: e.app.memberOfSomethingElse\n"
                            "    property var t: `${app.inATemplate.part} app.notInIt`\n"
                            "    onClicked: app.someMethod(app.library.count)\n"
                            "}\n";
    std::vector<std::string> found;
    for (const Use& u: appUses("x.qml", qml)) {
        found.push_back(u.first + (u.second.empty() ? "" : "." + u.second) + "@" + std::to_string(u.line));
    }
    EXPECT_EQ(found, (std::vector<std::string>{"inATemplate.part@8", "someMethod@9", "library.count@9"}));
    const auto deep = appUses("x.qml", "onClicked: app.reference.edit.fitWidth()\n");
    ASSERT_EQ(deep.size(), 1u);
    EXPECT_EQ(deep[0].first + "." + deep[0].second + "." + deep[0].third, "reference.edit.fitWidth");

    AppController controller;
    EXPECT_TRUE(has(controller.metaObject(), "openPath"));
    EXPECT_FALSE(has(controller.metaObject(), "openPathRenamed")) << "a renamed member is found missing";
}

// Every app.<a> the QML uses exists on AppController, and every app.<a>.<b> on the object app.<a> holds
TEST(QmlApiTest, everyNameTheQmlUsesOnAppExists) {
    AppController controller;
    controller.newDocument();  // (app.view is the current document's)
    const auto files = uiFiles();
    ASSERT_GT(files.size(), 50u) << qmlFolder();
    size_t checked = 0;
    std::set<std::string> missing;
    for (const auto& f: files) {
        for (const Use& u: appUses(f.string(), readAll(f))) {
            ++checked;
            if (!has(controller.metaObject(), u.first)) {
                if (!dynamicFirst().count(u.first)) {
                    missing.insert("app." + u.first + " (" + where(u) + ")");
                }
                continue;
            }
            if (u.second.empty()) {
                continue;
            }
            QObject* live = nullptr;
            const QMetaObject* sub = objectBehind(&controller, u.first, &live);
            if (!sub) {
                continue;  // (a value, a list or a map: its members are JavaScript's, not ours)
            }
            if (!has(sub, u.second) && !dynamicSecond().count(u.first + "." + u.second)) {
                missing.insert("app." + u.first + "." + u.second + " (" + where(u) + ", on " + sub->className() + ")");
                continue;
            }
            const QMetaObject* subSub = u.third.empty() ? nullptr : objectBehind(live, u.second);
            if (subSub && !has(subSub, u.third)) {
                missing.insert("app." + u.first + "." + u.second + "." + u.third + " (" + where(u) + ", on " +
                               subSub->className() + ")");
            }
        }
    }
    EXPECT_GT(checked, 1000u) << "the QML reads app this often: is the folder right?";
    std::string list;
    for (const auto& m: missing) {
        list += "\n  " + m;
    }
    EXPECT_TRUE(missing.empty()) << "names the QML uses on app that C++ does not have:" << list;
}

// The pills (selection, note, PDF text, context menu, PDF text handles) work on their `target`: what acts on a canvas
// (CanvasActions: the document's app.edit or, in the reference view, app.reference.edit). It has every name a pill
// reads, and both targets are one.
TEST(QmlApiTest, thePillsFindTheirInterfaceOnBothTargets) {
    {
        AppController controller;
        EXPECT_STREQ(objectBehind(&controller, "edit")->className(), "xqt::CanvasActions");
        QObject* reference = nullptr;
        objectBehind(&controller, "reference", &reference);
        ASSERT_NE(reference, nullptr);
        EXPECT_STREQ(objectBehind(reference, "edit")->className(), "xqt::CanvasActions");
    }
    const std::vector<std::string> pills{"ContextPill.qml", "NotePill.qml", "PdfTextHandles.qml", "PdfTextPill.qml",
                                         "SelectionPill.qml"};
    std::set<std::string> missing;
    size_t checked = 0;
    for (const auto& f: uiFiles()) {
        if (std::find(pills.begin(), pills.end(), f.filename().string()) == pills.end()) {
            continue;
        }
        for (const Use& u: targetUses(f.string(), readAll(f))) {
            ++checked;
            if (!has(&xqt::CanvasActions::staticMetaObject, u.first)) {
                missing.insert("CanvasActions." + u.first + " (" + where(u) + ")");
            }
        }
    }
    EXPECT_GT(checked, 50u);
    std::string list;
    for (const auto& m: missing) {
        list += "\n  " + m;
    }
    EXPECT_TRUE(missing.empty()) << "names a pill reads from its target that CanvasActions does not have:" << list;
}
