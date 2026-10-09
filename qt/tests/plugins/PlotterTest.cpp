/*
 * xournal-qt: the bundled function plotter (qt/resources/plugins/function-plotter, qt/docs/features/plugins.md, "The
 * function plotter"): its expression parser (precedence, implicit multiplication, functions without parentheses,
 * the decimal comma, errors that say what is wrong), the formulas as LaTeX, the ticks, the adaptive sampling and the
 * breaks at poles and jumps, the exact scale, and the plugin as a whole through the host: the elements it inserts
 * (grouped, colored, Markdown math), Insert and Edit as one undo step each, the permission on first use, rollback.
 *
 * @license GNU GPLv2 or later
 */
#include <cmath>

#include <QElapsedTimer>
#include <QJSEngine>
#include <QJsonDocument>

#include "model/Stroke.h"
#include "model/Text.h"
#include "undo/UndoRedoHandler.h"

#include "PluginTestSupport.h"

using namespace xqt;
using xqt::test::PluginFixture;

namespace {

const QString PLOTTER = QStringLiteral(XQT_PLUGINS_SOURCE_DIR "/function-plotter");

/// The plotter's library modules in an engine of their own; `call` runs one exported function with JSON arguments
class Lib {
public:
    explicit Lib(const QString& module) {
        mod = js.importModule(PLOTTER + "/lib/" + module);
        EXPECT_FALSE(mod.isError()) << mod.toString().toStdString();
    }
    QJSValue call(const QString& fn, const QString& argsJson = "[]") {
        const QJSValue args = js.evaluate("(" + argsJson + ")");
        QJSValueList list;
        for (int i = 0; i < args.property("length").toInt(); ++i) {
            list << args.property(i);
        }
        const QJSValue r = mod.property(fn).call(list);
        EXPECT_FALSE(r.isError()) << fn.toStdString() << ": " << r.toString().toStdString();
        return r;
    }
    /// Evaluates JS with the module's exports as `m`
    QJSValue eval(const QString& code) {
        js.globalObject().setProperty("m", mod);
        const QJSValue r = js.evaluate(code);
        EXPECT_FALSE(r.isError()) << code.toStdString() << ": " << r.toString().toStdString();
        return r;
    }
    QJSEngine js;
    QJSValue mod;
};

/// The value of a formula at x (and parameters), NaN when it does not parse
double value(Lib& p, const QString& text, double x, bool decimalComma = true, const QString& params = "{}") {
    const QJSValue r = p.eval(QStringLiteral("(function(){ const r = m.parse(%1, {decimalComma: %2});"
                                             " if (!r.ok) return NaN; const v = Object.assign({x: %3}, %4);"
                                             " return m.compile(r.tree)(v) })()")
                                      .arg(QString::fromUtf8(QJsonDocument::fromVariant(QVariantList{text}).toJson(QJsonDocument::Compact)).mid(1).chopped(1))
                                      .arg(decimalComma ? "true" : "false")
                                      .arg(x)
                                      .arg(params));
    return r.toNumber();
}

QString errorOf(Lib& p, const QString& text) {
    return p.eval(QStringLiteral("m.parse(%1, {decimalComma: true}).error || ''")
                          .arg(QString::fromUtf8(QJsonDocument::fromVariant(QVariantList{text}).toJson(QJsonDocument::Compact)).mid(1).chopped(1)))
            .toString();
}

QString latexOf(Lib& p, const QString& text, bool decimalComma = true) {
    return p.eval(QStringLiteral("(function(){ const r = m.parse(%1, {decimalComma: %2}); return r.ok ? "
                                 "m.toLatex(r.tree, {decimalComma: %2}) : 'ERROR ' + r.error })()")
                          .arg(QString::fromUtf8(QJsonDocument::fromVariant(QVariantList{text}).toJson(QJsonDocument::Compact)).mid(1).chopped(1))
                          .arg(decimalComma ? "true" : "false"))
            .toString();
}

}  // namespace

TEST(PlotterParse, precedenceAndImplicitMultiplication) {
    Lib p("parse.mjs");
    EXPECT_DOUBLE_EQ(value(p, "2+3*4", 0), 14);
    EXPECT_DOUBLE_EQ(value(p, "-2^2", 0), -4);   // (a power binds before the minus)
    EXPECT_DOUBLE_EQ(value(p, "2^3^2", 0), 512);  // (to the right)
    EXPECT_DOUBLE_EQ(value(p, "2^-1", 0), 0.5);
    EXPECT_DOUBLE_EQ(value(p, "x^2 - 2x + 1", 3), 4);
    EXPECT_DOUBLE_EQ(value(p, "2x", 3), 6);
    EXPECT_DOUBLE_EQ(value(p, "3(x+1)", 2), 9);
    EXPECT_DOUBLE_EQ(value(p, "2pi", 0), 2 * M_PI);
    EXPECT_DOUBLE_EQ(value(p, "2π x", 1), 2 * M_PI);
    EXPECT_DOUBLE_EQ(value(p, "(x+1)(x-1)", 3), 8);
    EXPECT_DOUBLE_EQ(value(p, "x sin x", 1), std::sin(1.0));
    EXPECT_DOUBLE_EQ(value(p, "sin x cos x", 1), std::sin(1.0) * std::cos(1.0));
    EXPECT_DOUBLE_EQ(value(p, "sin 2x", 1), std::sin(2.0));
    EXPECT_DOUBLE_EQ(value(p, "sinx", 1), std::sin(1.0));
    EXPECT_DOUBLE_EQ(value(p, "sin^2 x + cos^2 x", 0.7), 1.0);
    EXPECT_DOUBLE_EQ(value(p, "2sin(x)^2", 1), 2 * std::sin(1.0) * std::sin(1.0));
    EXPECT_DOUBLE_EQ(value(p, "sqrt(x)", 9), 3);
    EXPECT_DOUBLE_EQ(value(p, "sqrt x + 1", 9), 4);
    EXPECT_DOUBLE_EQ(value(p, "|x - 3|", 1), 2);
    EXPECT_DOUBLE_EQ(value(p, "abs(x)", -2), 2);
    EXPECT_DOUBLE_EQ(value(p, "ln(e)", 0), 1);
    EXPECT_DOUBLE_EQ(value(p, "log(100)", 0), 2);
    EXPECT_DOUBLE_EQ(value(p, "log(2, 8)", 0), 3);
    EXPECT_DOUBLE_EQ(value(p, "exp(0)", 0), 1);
    EXPECT_DOUBLE_EQ(value(p, "x²", 3), 9);
    EXPECT_DOUBLE_EQ(value(p, "2·x − 1", 3), 5);
    EXPECT_DOUBLE_EQ(value(p, "(-8)^(1/3)", 0), -2);  // (the real cube root, as on a calculator)
    EXPECT_DOUBLE_EQ(value(p, "a*x^2 + b", 2, true, "{a: 0.5, b: -1}"), 1);
    EXPECT_DOUBLE_EQ(value(p, "ax", 2, true, "{a: 3}"), 6);  // (two letters: a times x)
    // Parameters are the other single letters
    EXPECT_EQ(p.eval("m.parse('a*x^2 + b + k*sin(x)').params.join(',')").toString(), "a,b,k");
    EXPECT_EQ(p.eval("m.parse('2x + pi + e').params.length").toInt(), 0);
}

TEST(PlotterParse, theDecimalCommaWhereItCannotSeparateArguments) {
    Lib p("parse.mjs");
    EXPECT_DOUBLE_EQ(value(p, "1,5x", 2), 3);
    EXPECT_DOUBLE_EQ(value(p, "0,25 * x", 4), 1);
    EXPECT_DOUBLE_EQ(value(p, "1.5x", 2), 3);
    EXPECT_DOUBLE_EQ(value(p, "max(1,5)", 0), 5);       // (a function of several arguments: the comma separates)
    EXPECT_DOUBLE_EQ(value(p, "max(1,5; 2)", 0), 2);    // (with ";" between the arguments, 1,5 is one number)
    EXPECT_DOUBLE_EQ(value(p, "sin(1,5)", 0), std::sin(1.5));
    EXPECT_TRUE(std::isnan(value(p, "1,5x", 2, false)));  // (without the decimal comma: an error)
}

TEST(PlotterParse, errorsSayWhatIsWrong) {
    Lib p("parse.mjs");
    EXPECT_EQ(errorOf(p, "sni(x)"), "unknown function \"sni\" - did you mean sin?");
    EXPECT_EQ(errorOf(p, "sqr x"), "unknown function \"sqr\" - did you mean sqrt?");
    EXPECT_EQ(errorOf(p, "x +"), "\"+\" needs something after it");
    EXPECT_EQ(errorOf(p, "(x + 1"), "\")\" is missing");
    EXPECT_EQ(errorOf(p, "x + 1)"), "\")\" without \"(\"");
    EXPECT_EQ(errorOf(p, "2 3"), "an operator is missing between two numbers");
    EXPECT_EQ(errorOf(p, "y = x^2"), "type only the right side (\"x^2\", not \"y = x^2\")");
    EXPECT_EQ(errorOf(p, "sin"), "sin needs an argument: sin(x)");
    EXPECT_EQ(errorOf(p, "x # 2"), "\"#\" is not part of a formula");
    EXPECT_EQ(errorOf(p, ""), "empty");
    EXPECT_EQ(errorOf(p, "max(x)"), "max takes 2 to 9 arguments");
}

TEST(PlotterParse, formulasAsLatex) {
    Lib p("parse.mjs");
    EXPECT_EQ(latexOf(p, "x^2 - 2x + 1"), "x^{2} - 2x + 1");
    EXPECT_EQ(latexOf(p, "sqrt(x)"), "\\sqrt{x}");
    EXPECT_EQ(latexOf(p, "sin(x)"), "\\sin\\left(x\\right)");
    EXPECT_EQ(latexOf(p, "sin(x)^2"), "\\sin^{2}\\left(x\\right)");
    EXPECT_EQ(latexOf(p, "(x+1)/(x-1)"), "\\frac{x + 1}{x - 1}");
    EXPECT_EQ(latexOf(p, "1/x"), "\\frac{1}{x}");
    EXPECT_EQ(latexOf(p, "a*x^2 + b"), "a x^{2} + b");
    EXPECT_EQ(latexOf(p, "2(x+1)^2"), "2\\left(x + 1\\right)^{2}");
    EXPECT_EQ(latexOf(p, "1,5x"), "1{,}5x");
    EXPECT_EQ(latexOf(p, "1.5x", false), "1.5x");
    EXPECT_EQ(latexOf(p, "2pi x"), "2\\pi x");
    EXPECT_EQ(latexOf(p, "exp(-x^2)"), "e^{-x^{2}}");
    EXPECT_EQ(latexOf(p, "|x|"), "\\left|x\\right|");
    EXPECT_EQ(latexOf(p, "-x^2"), "-x^{2}");
    EXPECT_EQ(latexOf(p, "2*3"), "2 \\cdot 3");
    EXPECT_EQ(latexOf(p, "ln x"), "\\ln\\left(x\\right)");
}

TEST(PlotterTicks, stepsNumbersAndPi) {
    Lib t("ticks.mjs");
    EXPECT_DOUBLE_EQ(t.call("niceStep", "[10, 300, 20]").toNumber(), 1);
    EXPECT_DOUBLE_EQ(t.call("niceStep", "[10, 100, 20]").toNumber(), 2);
    EXPECT_DOUBLE_EQ(t.call("niceStep", "[10, 40, 20]").toNumber(), 5);
    EXPECT_DOUBLE_EQ(t.call("niceStep", "[1, 300, 30]").toNumber(), 0.1);
    EXPECT_DOUBLE_EQ(t.call("niceStep", "[1000, 300, 30]").toNumber(), 100);
    EXPECT_EQ(t.call("ticks", "[-1, 1, 0.1]").property("length").toInt(), 21);
    EXPECT_DOUBLE_EQ(t.eval("m.ticks(-1, 1, 0.1)[13].v").toNumber(), 0.3);  // (k · step: no 0.30000000000000004)
    EXPECT_EQ(t.call("piLabel", "[1, 1, 2]").toString(), "\\frac{\\pi}{2}");
    EXPECT_EQ(t.call("piLabel", "[2, 1, 2]").toString(), "\\pi");
    EXPECT_EQ(t.call("piLabel", "[3, 1, 2]").toString(), "\\frac{3\\pi}{2}");
    EXPECT_EQ(t.call("piLabel", "[4, 1, 2]").toString(), "2\\pi");
    EXPECT_EQ(t.call("piLabel", "[-1, 1, 4]").toString(), "-\\frac{\\pi}{4}");
    EXPECT_EQ(t.call("numberLabel", "[0.5, 0.5, true]").toString(), "0{,}5");
    EXPECT_EQ(t.call("numberLabel", "[0.5, 0.5, false]").toString(), "0.5");
    EXPECT_EQ(t.call("numberLabel", "[-2, 1, false]").toString(), "-2");
    EXPECT_EQ(t.call("numberLabel", "[0.30000000000000004, 0.1, true]").toString(), "0{,}3");
    EXPECT_EQ(t.call("numberLabel", "[2000000, 1000000, false]").toString(), "2 \\cdot 10^{6}");
    EXPECT_EQ(t.eval("m.piStep(4 * Math.PI, 400, 30).den").toInt(), 2);  // (π/2 for −2π … 2π on 400 points)
}

TEST(PlotterSample, polesAndJumpsBreakTheCurveAndItIsCutAtTheBorder) {
    Lib s("sample.mjs");
    const QString view = "{x0: -5, x1: 5, y0: -5, y1: 5, left: 100, top: 100, width: 200, height: 200}";
    // 1/x: two pieces, none crossing x = 0 (page x 200), every point in the box
    QJSValue lines = s.eval("m.sampleFunction(function (x) { return 1 / x }, " + view + ")");
    ASSERT_EQ(lines.property("length").toInt(), 2);
    for (int i = 0; i < 2; ++i) {
        const QJSValue l = lines.property(i);
        const int n = l.property("length").toInt();
        bool left = l.property(0).toNumber() < 200;
        for (int k = 0; k < n; k += 2) {
            const double x = l.property(k).toNumber(), y = l.property(k + 1).toNumber();
            EXPECT_EQ(x < 200, left);
            EXPECT_GE(x, 100 - 1e-9);
            EXPECT_LE(x, 300 + 1e-9);
            EXPECT_GE(y, 100 - 1e-9);
            EXPECT_LE(y, 300 + 1e-9);
        }
        // (it leaves the box at its top or bottom: the last or first point is on the border)
        const double yEnd = std::min(std::abs(l.property(1).toNumber() - 100), std::abs(l.property(1).toNumber() - 300));
        const double yEnd2 = std::min(std::abs(l.property(n - 1).toNumber() - 100), std::abs(l.property(n - 1).toNumber() - 300));
        EXPECT_LT(std::min(yEnd, yEnd2), 1e-6);
    }
    // tan x on [-5, 5]: poles at ±π/2 and ±3π/2: five pieces, no spike (no step longer than half the box)
    lines = s.eval("m.sampleFunction(Math.tan, " + view + ")");
    EXPECT_EQ(lines.property("length").toInt(), 5);
    for (int i = 0; i < lines.property("length").toInt(); ++i) {
        const QJSValue l = lines.property(i);
        for (int k = 2; k + 1 < l.property("length").toInt(); k += 2) {
            EXPECT_LT(std::abs(l.property(k + 1).toNumber() - l.property(k - 1).toNumber()), 100);
        }
    }
    // floor x: a step per unit, no vertical lines
    lines = s.eval("m.sampleFunction(Math.floor, " + view + ")");
    EXPECT_EQ(lines.property("length").toInt(), 10);
    // sqrt x begins where it is defined, at x = 0 (page x 200)
    lines = s.eval("m.sampleFunction(Math.sqrt, " + view + ")");
    ASSERT_EQ(lines.property("length").toInt(), 1);
    EXPECT_NEAR(lines.property(0).property(0).toNumber(), 200, 0.01);
    // x² on y in [0, 4]: cut where it leaves through the top (y on the border, x interpolated: ±2)
    lines = s.eval("m.sampleFunction(function (x) { return x * x }, {x0: -5, x1: 5, y0: 0, y1: 4, left: 0, top: 0, "
                   "width: 100, height: 100})");
    ASSERT_EQ(lines.property("length").toInt(), 1);
    const QJSValue l = lines.property(0);
    const int n = l.property("length").toInt();
    EXPECT_NEAR(l.property(1).toNumber(), 0, 1e-9);
    EXPECT_NEAR(l.property(0).toNumber(), 30, 1e-3);  // (x = -2)
    EXPECT_NEAR(l.property(n - 2).toNumber(), 70, 1e-3);
    // Adaptive: more points where it bends (a parabola's vertex) than on a straight line
    EXPECT_LT(s.eval("m.sampleFunction(function (x) { return x }, " + view + ")[0].length").toInt(), 6);
    // A wild one stays fast
    QElapsedTimer timer;
    timer.start();
    s.eval("m.sampleFunction(function (x) { return Math.sin(1 / x) }, " + view + ")");
    EXPECT_LT(timer.elapsed(), 1500);
    // Curves: a circle x = cos t, y = sin t is one closed line
    lines = s.eval("m.sampleCurve(Math.cos, Math.sin, 0, 2 * Math.PI, " + view + ")");
    ASSERT_EQ(lines.property("length").toInt(), 1);
    const QJSValue c = lines.property(0);
    const int cn = c.property("length").toInt();
    EXPECT_NEAR(c.property(0).toNumber(), c.property(cn - 2).toNumber(), 1e-6);
    EXPECT_NEAR(c.property(1).toNumber(), c.property(cn - 1).toNumber(), 1e-6);
    // Roots and extrema
    EXPECT_NEAR(s.eval("m.roots(function (x) { return x * x - 2 }, -5, 5)[1]").toNumber(), std::sqrt(2.0), 1e-9);
    EXPECT_EQ(s.eval("m.roots(function (x) { return 1 / x }, -5, 5).length").toInt(), 0);  // (a pole is no root)
    EXPECT_NEAR(s.eval("m.extrema(function (x) { return (x - 1) * (x - 1) }, -5, 5)[0].x").toNumber(), 1, 1e-6);
}

TEST(PlotterDraw, theExactScaleIsOneCentimetrePerUnit) {
    Lib d("plot.mjs");
    d.js.globalObject().setProperty("S", d.js.importModule(PLOTTER + "/lib/spec.mjs"));
    const QJSValue r = d.eval(
            "(function () { const s = S.defaultSpec(false, []); s.exact = true; s.unitMm = 10; s.xMin = '-5'; "
            "s.xMax = '5'; s.yAuto = false; s.yMin = '-3'; s.yMax = '4'; s.grid = true;"
            "const size = m.exactSize(s, m.ranges(s, m.analyse(s)));"
            "const out = m.draw(s, {x: 50, y: 60, width: size.width, height: size.height});"
            "const grid = out.shapes.filter(function (sh) { return sh.type === 'stroke' && sh.color === m.GRID_COLOR"
            " && sh.points[0] === sh.points[2] });"
            "return {w: size.width, h: size.height, xs: grid.map(function (g) { return g.points[0] }) } })()");
    const double cm = 72 / 2.54;
    EXPECT_NEAR(r.property("w").toNumber(), 10 * cm, 1e-9);
    EXPECT_NEAR(r.property("h").toNumber(), 7 * cm, 1e-9);
    const QJSValue xs = r.property("xs");
    ASSERT_EQ(xs.property("length").toInt(), 11);  // (-5 … 5: a grid line per centimetre)
    for (int i = 1; i < 11; ++i) {
        EXPECT_NEAR(xs.property(i).toNumber() - xs.property(i - 1).toNumber(), cm, 1e-9);
    }
    EXPECT_NEAR(xs.property(0).toNumber(), 50, 1e-9);
}

// --- the plugin through the host --------------------------------------------------------------------------------------

namespace {
class PlotterFixture: public PluginFixture {
protected:
    void SetUp() override {
        PluginFixture::SetUp();
        // The bundled plugins' folder is the source tree's; the selection as a window would give it
        hostObject = std::make_unique<plugins::PluginHost>(
                QStringList{QStringLiteral(XQT_PLUGINS_SOURCE_DIR), tmp.filePath("user")}, [this] { return stored; },
                [this](const QString& s) { stored = s; });
        ops.add("selection.read", "read", false, [this](ops::Context& c, const QVariantMap&) -> QVariant {
            if (selected.isEmpty()) {
                return {};
            }
            QVariantList list;
            for (const QVariant& e: c.apply("element.list", {{"page", 0}}).toList()) {
                if (selected.contains(e.toMap().value("group").toInt())) {
                    list << e;
                }
            }
            return QVariantMap{{"page", 0}, {"elements", list}};
        });
        ops.add("selection.clear", "read", false, [this](ops::Context&, const QVariantMap&) -> QVariant {
            selected.clear();
            return true;
        });
    }
    /// The plot's live dialog after "plot": its first preview
    QVariantMap openPlot() {
        auto r = host().run("org.xournalqt.function-plotter", "plot", env());
        EXPECT_TRUE(r.ok) << r.error.toStdString();
        EXPECT_TRUE(ui.live);
        return ui.live ? ui.live->spec() : QVariantMap();
    }
    QVariantMap frame{{"page", 0}, {"x", 100}, {"y", 150}, {"width", 240}, {"height", 200}};
    QList<int> selected;  ///< the groups "selected"
};
}  // namespace

TEST_F(PlotterFixture, insertMakesOneGroupOfInkAndMathAsOneUndoStep) {
    const QVariantMap spec = openPlot();
    EXPECT_EQ(spec.value("title").toString(), "Plot a function");
    QVariantMap values = spec.value("values").toMap();
    values["f0_expr"] = "x^2 - 2x - 1";
    const QVariantMap preview = ui.live->change(values, frame, "");
    ASSERT_FALSE(preview.contains("error")) << preview.value("error").toString().toStdString();
    const QVariantList shapes = preview.value("shapes").toList();
    ASSERT_GT(shapes.size(), 20);
    EXPECT_TRUE(preview.value("errors").toMap().isEmpty());
    // The formula next to the curve, as LaTeX in the curve's color
    bool formula = false;
    for (const QVariant& v: shapes) {
        const QVariantMap s = v.toMap();
        if (s.value("type") == "markdown" && s.value("text").toString() == "$f(x) = x^{2} - 2x - 1$") {
            formula = true;
            EXPECT_EQ(s.value("color").toString(), values.value("f0_color").toString());
        }
    }
    EXPECT_TRUE(formula);
    EXPECT_EQ(elements(), 0u);  // (a preview changes nothing)

    // A typo: the message under the field, no curve
    QVariantMap typo = values;
    typo["f0_expr"] = "sni(x)";
    EXPECT_EQ(ui.live->change(typo, frame, "").value("errors").toMap().value("f0_expr").toString(),
              "unknown function \"sni\" - did you mean sin?");

    ASSERT_TRUE(ui.live->insert(values, frame));
    EXPECT_EQ(ui.asked, 1);  // (the permission, asked once)
    Layer* layer = session->getDocument()->getPage(0)->getSelectedLayer();
    const auto all = layer->getElementsView();
    ASSERT_EQ(elements(), static_cast<size_t>(all.size()));  // (all in one layer)
    const uint32_t group = (*all.begin())->getGroup();
    EXPECT_NE(group, 0u);
    size_t strokes = 0, texts = 0, withData = 0;
    for (const Element* e: all) {
        EXPECT_EQ(e->getGroup(), group);
        if (e->getType() == ELEMENT_STROKE) {
            ++strokes;
        } else if (e->getType() == ELEMENT_TEXT) {
            ++texts;
            EXPECT_TRUE(static_cast<const Text*>(e)->isMarkdown());
        }
        withData += !e->getData().empty() && e->getData().find("\"plot\"") != std::string::npos;
    }
    EXPECT_GT(strokes, 10u);
    EXPECT_GT(texts, 5u);
    EXPECT_GE(withData, 1u);
    UndoRedoHandler& undo = *session->getUndoRedoHandler();
    EXPECT_EQ(undo.undoDescription(), "Undo: Plot a function…");
    undo.undo();
    EXPECT_EQ(elements(), 0u);
    EXPECT_FALSE(undo.canUndo());
}

TEST_F(PlotterFixture, editPlotReplacesItAsOneUndoStep) {
    openPlot();
    QVariantMap values = ui.live->spec().value("values").toMap();
    ASSERT_TRUE(ui.live->insert(values, frame));
    const size_t first = elements();
    const uint32_t group = session->getDocument()->getPage(0)->getSelectedLayer()->getElementsView().front()->getGroup();

    // Selected, "Edit plot": the dialog again, with the plot's values and its frame where it is
    selected = {static_cast<int>(group)};
    ui.live.reset();
    auto r = host().run("org.xournalqt.function-plotter", "editPlot", env());
    ASSERT_TRUE(r.ok) << r.error.toStdString();
    ASSERT_TRUE(ui.live);
    EXPECT_EQ(ui.live->spec().value("title").toString(), "Edit plot");
    QVariantMap edit = ui.live->spec().value("values").toMap();
    EXPECT_EQ(edit.value("f0_expr").toString(), values.value("f0_expr").toString());
    const QVariantMap f = ui.live->spec().value("frame").toMap();
    EXPECT_NEAR(f.value("x").toDouble(), 100, 0.01);
    EXPECT_NEAR(f.value("y").toDouble(), 150, 0.01);
    // A second function: Update replaces the old plot
    const QVariantMap added = ui.live->change(edit, f, "addFunction");
    edit = added.value("values").toMap();
    edit["f1_expr"] = "sin(x)";
    EXPECT_TRUE(added.contains("fields"));
    ASSERT_TRUE(ui.live->insert(edit, {{"page", 0}, {"x", 100}, {"y", 150}, {"width", 240}, {"height", 200}}));
    const size_t second = elements();
    EXPECT_GT(second, first);
    bool sine = false;
    for (const Element* e: session->getDocument()->getPage(0)->getSelectedLayer()->getElementsView()) {
        if (e->getType() == ELEMENT_TEXT && static_cast<const Text*>(e)->getText().find("\\sin") != std::string::npos) {
            sine = true;
        }
    }
    EXPECT_TRUE(sine);
    UndoRedoHandler& undo = *session->getUndoRedoHandler();
    EXPECT_EQ(undo.undoDescription(), "Undo: Edit plot…");
    undo.undo();  // (the old plot back, whole)
    EXPECT_EQ(elements(), first);
}

TEST_F(PlotterFixture, aPlotWithAMistakeIsNotInsertedAndNothingChanges) {
    openPlot();
    QVariantMap values = ui.live->spec().value("values").toMap();
    values["f0_expr"] = "x^2 +";
    QString error;
    EXPECT_FALSE(ui.live->insert(values, frame, &error));
    EXPECT_TRUE(error.contains("mistake")) << error.toStdString();
    EXPECT_EQ(elements(), 0u);
    EXPECT_FALSE(session->getUndoRedoHandler()->canUndo());
}

TEST_F(PlotterFixture, parametersGetSlidersAndExactScaleFixesTheFrame) {
    openPlot();
    QVariantMap values = ui.live->spec().value("values").toMap();
    values["f0_expr"] = "a*x^2 + b";
    QVariantMap r = ui.live->change(values, frame, "");
    ASSERT_TRUE(r.contains("fields"));
    QStringList ids;
    for (const QVariant& f: r.value("fields").toList()) {
        ids << f.toMap().value("id").toString();
    }
    EXPECT_TRUE(ids.contains("p_a"));
    EXPECT_TRUE(ids.contains("p_b"));
    values = r.value("values").toMap();
    values["exact"] = true;
    values["unitMm"] = 5;
    values["yAuto"] = false;
    values["xMin"] = "-4";
    values["xMax"] = "4";
    values["yMin"] = "-2";
    values["yMax"] = "6";
    r = ui.live->change(values, frame, "");
    const QVariantMap f = r.value("frame").toMap();
    EXPECT_NEAR(f.value("width").toDouble(), 8 * 5 * 72 / 25.4, 1e-6);
    EXPECT_NEAR(f.value("height").toDouble(), 8 * 5 * 72 / 25.4, 1e-6);
    EXPECT_FALSE(f.value("resizable").toBool());
    EXPECT_EQ(r.value("message").toString(), "Printed at 100 %: 4 × 4 cm");
}
