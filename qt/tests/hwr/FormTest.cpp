/*
 * xournal-qt: handwriting forms (qt/research/hwr/forms/DESIGN.md): the manifest attached to the form's PDF, strokes
 * mapped to boxes at any angle, the dataset of a filled form (`xournal-qt-cli hwr-form`, FormDataset.h) and the
 * benchmark (`hwr-bench`, FormBench.h) with scripted readings, and with the built-in model on real ink turned by the
 * boxes' angles (XQT_ONNXRUNTIME=<path of libonnxruntime.so.1>).
 *
 * @license GNU GPLv2 or later
 */
#include <algorithm>
#include <cmath>
#include <shared_mutex>

#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <gtest/gtest.h>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFEFStreamObjectHelper.hh>
#include <qpdf/QPDFEmbeddedFileDocumentHelper.hh>
#include <qpdf/QPDFFileSpecObjectHelper.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFWriter.hh>

#include "hwr/FakeRecognizer.h"
#include "hwr/FormBench.h"
#include "hwr/FormDataset.h"
#include "hwr/FormManifest.h"
#include "hwr/HandwritingSearch.h"
#include "hwr/InkLayout.h"
#include "hwr/LineDataset.h"
#include "hwr/Recognizer.h"
#include "model/Document.h"
#include "model/Layer.h"
#include "model/PageType.h"
#include "model/Stroke.h"
#include "model/XojPage.h"
#include "session/DocumentSession.h"
#include "session/InkText.h"
#ifdef XQT_HWR_ONNX
#include "hwr/HwrInfo.h"
#include "hwr/ModelInfo.h"
#endif

#include "config-test.h"

using namespace xqt;
using namespace xqt::hwr;

namespace {
constexpr double A4_W = 210 * PT_PER_MM, A4_H = 297 * PT_PER_MM;

/// The test form: one page, lines at 0, 90, -90 and 45 degrees, a drawing, a word left empty, a formula
const char* const MANIFEST = R"({
  "form": "xqt-hwr-test", "version": 1, "language": "en", "page_size_mm": [210, 297], "pages": 1,
  "items": [
    {"id": "1.1", "page": 1, "section": "B", "kind": "line", "text": "Kalman filter", "box_mm": [20, 20, 80, 14],
     "angle": 0, "x_height_mm": 3, "guides": true},
    {"id": "1.2", "page": 1, "section": "D", "kind": "line", "text": "margin note", "box_mm": [130, 40, 60, 14],
     "angle": 90, "x_height_mm": 3},
    {"id": "1.3", "page": 1, "section": "D", "kind": "line", "text": "upwards note", "box_mm": [20, 120, 60, 14],
     "angle": -90, "x_height_mm": 3},
    {"id": "1.4", "page": 1, "section": "D", "kind": "line", "text": "slanted words", "box_mm": [100, 150, 60, 14],
     "angle": 45, "x_height_mm": 3},
    {"id": "1.5", "page": 1, "section": "F", "kind": "drawing", "text": "", "box_mm": [20, 200, 60, 40], "angle": 0},
    {"id": "1.6", "page": 1, "section": "C", "kind": "word", "text": "Zebra", "box_mm": [100, 210, 40, 14],
     "angle": 0, "x_height_mm": 6, "search": ["Zebra"]},
    {"id": "1.7", "page": 1, "section": "G", "kind": "math", "text": "a+b", "latex": "a+b",
     "box_mm": [150, 250, 40, 14], "angle": 0, "x_height_mm": 3}
  ]
})";

/// The boxes' words as written (letter counts), by box id
const std::vector<std::pair<QString, std::vector<int>>> WRITTEN{
        {QStringLiteral("1.1"), {6, 6}}, {QStringLiteral("1.2"), {6, 4}}, {QStringLiteral("1.3"), {7, 4}},
        {QStringLiteral("1.4"), {7, 5}}, {QStringLiteral("1.7"), {3}}};

/// Upright strokes of a line of words (letters 5 x 8.5 pt, zigzags that differ by `seed`), centred on (0, 0)
std::vector<InkStroke> uprightLine(const std::vector<int>& words, int seed) {
    std::vector<InkStroke> out;
    double x = 0;
    for (const int letters: words) {
        for (int l = 0; l < letters; ++l) {
            const double wobble = 0.4 * ((seed * 7 + l * 3) % 5);
            out.push_back(InkStroke::of({{x, 8.5}, {x + 1.5, wobble}, {x + 3, 8.5}, {x + 5, 0.5 + wobble}}, 1.2f));
            x += 6.5;
        }
        x += 14 - 1.5;
    }
    const double width = x - 14 + 1.5;
    for (InkStroke& s: out) {
        for (QPointF& p: s.points) {
            p -= QPointF(width / 2, 4.25);
        }
        s = InkStroke::of(s.points, s.width);
    }
    return out;
}

std::unique_ptr<Stroke> strokeOf(const InkStroke& s) {
    auto stroke = std::make_unique<Stroke>();
    stroke->setWidth(s.width);
    for (size_t i = 0; i < s.points.size(); ++i) {
        stroke->addPoint(i < s.widths.size() ? Point(s.points[i].x(), s.points[i].y(), s.widths[i])
                                             : Point(s.points[i].x(), s.points[i].y()));
    }
    return stroke;
}

/// Strokes placed into a box: turned by its angle, around its centre
std::vector<InkStroke> placed(const std::vector<InkStroke>& upright, const FormItem& item) {
    std::vector<InkStroke> out;
    for (const InkStroke& s: upright) {
        std::vector<QPointF> points;
        for (const QPointF& p: s.points) {
            points.push_back(item.centre() + ink::turned(p, item.angle));
        }
        out.push_back(InkStroke::of(std::move(points), s.width, s.widths));
    }
    return out;
}

const FormItem& itemById(const FormManifest& m, const QString& id) {
    return *std::find_if(m.items.begin(), m.items.end(), [&](const FormItem& i) { return i.id == id; });
}

/// The test form filled in on `page`: the boxes' lines, a drawing with a word-sized scribble, a stroke in no box
void fill(XojPage& page, const FormManifest& m) {
    Layer* layer = page.getSelectedLayer();
    int seed = 1;
    for (const auto& [id, words]: WRITTEN) {
        for (const InkStroke& s: placed(uprightLine(words, seed++), itemById(m, id))) {
            layer->addElement(strokeOf(s));
        }
    }
    const QRectF d = itemById(m, QStringLiteral("1.5")).box();
    std::vector<QPointF> frame{d.topLeft() + QPointF(5, 5), d.topRight() + QPointF(-5, 5),
                               d.bottomRight() + QPointF(-5, -5), d.bottomLeft() + QPointF(5, -5),
                               d.topLeft() + QPointF(5, 5)};
    layer->addElement(strokeOf(InkStroke::of(frame, 1.2f)));
    std::vector<QPointF> circle;
    for (int i = 0; i <= 32; ++i) {
        circle.emplace_back(d.center().x() + 30 * std::cos(i * M_PI / 16), d.center().y() + 30 * std::sin(i * M_PI / 16));
    }
    layer->addElement(strokeOf(InkStroke::of(circle, 1.2f)));
    for (const InkStroke& s: uprightLine({1}, 9)) {
        InkStroke t = s;
        for (QPointF& p: t.points) {
            p += d.topLeft() + QPointF(20, 20);
        }
        layer->addElement(strokeOf(InkStroke::of(t.points, t.width)));
    }
    layer->addElement(strokeOf(InkStroke::of({{A4_W - 30, A4_H - 30}, {A4_W - 20, A4_H - 25}}, 1.2f)));
}

std::unique_ptr<Document> filledForm(const FormManifest& m) {
    auto doc = std::make_unique<Document>(nullptr);
    auto page = std::make_shared<XojPage>(A4_W, A4_H);
    page->setBackgroundType(PageType(PageTypeFormat::Plain));
    fill(*page, m);
    doc->addPage(std::move(page));
    return doc;
}

std::vector<QJsonObject> entriesOf(const QDir& dir) {
    std::vector<QJsonObject> out;
    QFile f(dir.filePath(QStringLiteral("lines.jsonl")));
    if (!f.open(QIODevice::ReadOnly)) {
        return out;
    }
    for (const QByteArray& line: f.readAll().split('\n')) {
        if (!line.isEmpty()) {
            out.push_back(QJsonDocument::fromJson(line).object());
        }
    }
    return out;
}

/// A one-page A4 PDF with `json` attached as "<name>"
void writeFormPdf(const QString& path, const QByteArray& json, const std::string& name) {
    QPDF q;
    q.emptyPDF();
    QPDFObjectHandle contents = QPDFObjectHandle::newStream(&q, "");
    QPDFObjectHandle page = q.makeIndirectObject(QPDFObjectHandle::parse(
            "<< /Type /Page /MediaBox [0 0 " + std::to_string(A4_W) + " " + std::to_string(A4_H) + "] >>"));
    page.replaceKey("/Contents", contents);
    QPDFPageDocumentHelper(q).addPage(QPDFPageObjectHelper(page), false);
    QPDFEmbeddedFileDocumentHelper efdh(q);
    auto stream = QPDFEFStreamObjectHelper::createEFStream(q, json.toStdString());
    auto spec = QPDFFileSpecObjectHelper::createFileSpec(q, name, stream);
    efdh.replaceEmbeddedFile(name, spec);
    QPDFWriter w(q, path.toStdString().c_str());
    w.write();
}

/// A .xopp on that PDF with these strokes on its page (plain XML: the loader reads it unpacked too)
void writeXopp(const QString& path, const QString& pdf, const std::vector<InkStroke>& strokes) {
    QString xml = QStringLiteral("<?xml version=\"1.0\" standalone=\"no\"?>\n<xournal creator=\"test\" fileversion=\"4\">\n"
                                 "<page width=\"%1\" height=\"%2\"><background type=\"pdf\" domain=\"absolute\" "
                                 "filename=\"%3\" pageno=\"1\"/><layer>\n")
                          .arg(A4_W, 0, 'f', 2)
                          .arg(A4_H, 0, 'f', 2)
                          .arg(pdf);
    for (const InkStroke& s: strokes) {
        QStringList coords;
        for (const QPointF& p: s.points) {
            coords << QString::number(p.x(), 'f', 3) << QString::number(p.y(), 'f', 3);
        }
        xml += QStringLiteral("<stroke tool=\"pen\" color=\"#000000ff\" width=\"1.2\">%1</stroke>\n")
                       .arg(coords.join(u' '));
    }
    xml += QStringLiteral("</layer></page>\n</xournal>\n");
    QFile f(path);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write(xml.toUtf8());
}
}  // namespace

// The manifest: its fields, the default search words, boxes in points turned around their centre
TEST(FormTest, theManifestIsRead) {
    const FormManifest m = FormManifest::parse(MANIFEST);
    ASSERT_TRUE(m.valid()) << m.error.toStdString();
    EXPECT_EQ(m.form, QStringLiteral("xqt-hwr-test"));
    EXPECT_EQ(m.items.size(), 7u);
    const FormItem& k = itemById(m, QStringLiteral("1.1"));
    EXPECT_EQ(k.search, (QStringList{QStringLiteral("Kalman"), QStringLiteral("filter")}));
    EXPECT_NEAR(k.box().left(), 20 * 72 / 25.4, 1e-9);
    EXPECT_TRUE(itemById(m, QStringLiteral("1.5")).search.isEmpty());
    EXPECT_EQ(itemById(m, QStringLiteral("1.6")).search, QStringList{QStringLiteral("Zebra")});
    EXPECT_TRUE(itemById(m, QStringLiteral("1.7")).search.isEmpty()) << "no word of 3 letters";
    EXPECT_FALSE(itemById(m, QStringLiteral("1.5")).textual());
    EXPECT_TRUE(itemById(m, QStringLiteral("1.7")).textual());
    // A box at 90 degrees: tall on the page
    const FormItem& down = itemById(m, QStringLiteral("1.2"));
    const QPointF c = down.centre();
    EXPECT_TRUE(down.contains(c + QPointF(0, 70)));
    EXPECT_FALSE(down.contains(c + QPointF(70, 0)));
    EXPECT_EQ(searchWords(QStringLiteral("a dumb test, a Dumb one")),
              (QStringList{QStringLiteral("dumb"), QStringLiteral("test"), QStringLiteral("one")}));
    EXPECT_FALSE(FormManifest::parse("{}").valid());
    EXPECT_FALSE(FormManifest::parse("not json").valid());
}

// Each stroke goes to the box holding most of its points (in the box's turned frame); strokes in no box stay out
TEST(FormTest, strokesGoToTheBoxHoldingMostOfTheirPoints) {
    const FormManifest m = FormManifest::parse(MANIFEST);
    const auto items = m.itemsOn(1);
    auto doc = filledForm(m);
    const std::vector<InkStroke> strokes = strokesOf(*doc->getPage(0));
    const std::vector<int> owner = assign(strokes, items);
    std::map<QString, int> count;
    int none = 0;
    for (const int o: owner) {
        o < 0 ? ++none : ++count[items[static_cast<size_t>(o)]->id];
    }
    EXPECT_EQ(count[QStringLiteral("1.1")], 12);
    EXPECT_EQ(count[QStringLiteral("1.2")], 10);
    EXPECT_EQ(count[QStringLiteral("1.3")], 11);
    EXPECT_EQ(count[QStringLiteral("1.4")], 12);
    EXPECT_EQ(count[QStringLiteral("1.5")], 3);
    EXPECT_EQ(count[QStringLiteral("1.6")], 0);
    EXPECT_EQ(count[QStringLiteral("1.7")], 3);
    EXPECT_EQ(none, 1);
    // Half in a box is not most of it
    const FormItem& k = itemById(m, QStringLiteral("1.1"));
    const InkStroke across = InkStroke::of({k.box().topRight() + QPointF(-1, 5), k.box().topRight() + QPointF(1, 5),
                                            k.box().topRight() + QPointF(20, 5), k.box().topRight() + QPointF(30, 5)},
                                           1);
    EXPECT_EQ(assign({across}, items), std::vector<int>{-1});
}

// The dataset: a line per box with strokes, drawn upright as the recognisers see it, with the box's text and fields
TEST(FormTest, aFilledFormBecomesADataset) {
    QTemporaryDir tmp;
    const FormManifest m = FormManifest::parse(MANIFEST);
    auto doc = filledForm(m);
    FormExport job;
    job.document = QStringLiteral("filled.xopp");
    job.out = tmp.filePath(QStringLiteral("set"));
    job.writer = QStringLiteral("w07");
    const FormExportResult r = exportForm(job, *doc, m);
    ASSERT_TRUE(r.ok) << r.error.toStdString();
    EXPECT_EQ(r.lines, 5);
    EXPECT_EQ(r.math, 1);
    EXPECT_EQ(r.empty, 1);
    EXPECT_EQ(r.left, 1);
    EXPECT_EQ(r.outside, 1);
    EXPECT_TRUE(r.warnings.isEmpty()) << r.warnings.join(u'\n').toStdString();
    const QDir out(job.out);
    QFile ds(out.filePath(QStringLiteral("dataset.json")));
    ASSERT_TRUE(ds.open(QIODevice::ReadOnly));
    const QJsonObject d = QJsonDocument::fromJson(ds.readAll()).object();
    EXPECT_EQ(d.value(QStringLiteral("kind")).toString(), QStringLiteral("ink"));
    EXPECT_EQ(d.value(QStringLiteral("languages")).toArray(), QJsonArray{QStringLiteral("en")});
    EXPECT_TRUE(d.value(QStringLiteral("noncommercial")).toBool());
    const auto entries = entriesOf(out);
    ASSERT_EQ(entries.size(), 5u);
    std::map<QString, QJsonObject> byBox;
    for (const QJsonObject& e: entries) {
        byBox[e.value(QStringLiteral("box_id")).toString()] = e;
        EXPECT_EQ(e.value(QStringLiteral("writer")).toString(), QStringLiteral("w07"));
        EXPECT_EQ(e.value(QStringLiteral("form")).toString(), QStringLiteral("xqt-hwr-test"));
        EXPECT_EQ(e.value(QStringLiteral("page")).toInt(), 1);
        EXPECT_EQ(e.value(QStringLiteral("lang")).toString(), QStringLiteral("en"));
    }
    const QJsonObject down = byBox[QStringLiteral("1.2")];
    EXPECT_EQ(down.value(QStringLiteral("id")).toString(), QStringLiteral("w07-xqt_hwr_test-1_2"));
    EXPECT_EQ(down.value(QStringLiteral("text")).toString(), QStringLiteral("margin note"));
    EXPECT_EQ(down.value(QStringLiteral("angle")).toDouble(), 90);
    EXPECT_EQ(down.value(QStringLiteral("kind")).toString(), QStringLiteral("line"));
    EXPECT_FALSE(down.contains(QStringLiteral("math")));
    EXPECT_TRUE(byBox[QStringLiteral("1.7")].value(QStringLiteral("math")).toBool());
    EXPECT_EQ(byBox[QStringLiteral("1.7")].value(QStringLiteral("text")).toString(), QStringLiteral("a+b"));
    EXPECT_FALSE(byBox.count(QStringLiteral("1.5"))) << "a drawing is no training data";
    EXPECT_FALSE(byBox.count(QStringLiteral("1.6"))) << "an empty box";
    // Upright: the ink of the line written downwards is as wide as the line, relative to its top-left
    for (const QString& id: {QStringLiteral("1.1"), QStringLiteral("1.2"), QStringLiteral("1.3"), QStringLiteral("1.4")}) {
        QFile s(out.filePath(byBox[id].value(QStringLiteral("strokes")).toString()));
        ASSERT_TRUE(s.open(QIODevice::ReadOnly));
        const QJsonObject ink = QJsonDocument::fromJson(s.readAll()).object();
        EXPECT_GT(ink.value(QStringLiteral("width")).toDouble(), 60) << id.toStdString();
        EXPECT_LT(ink.value(QStringLiteral("height")).toDouble(), 10) << id.toStdString();
        double minX = 1e9, minY = 1e9;
        for (const QJsonValue& st: ink.value(QStringLiteral("strokes")).toArray()) {
            for (const QJsonValue& p: st.toObject().value(QStringLiteral("points")).toArray()) {
                minX = std::min(minX, p.toArray().at(0).toDouble());
                minY = std::min(minY, p.toArray().at(1).toDouble());
            }
        }
        EXPECT_NEAR(minX, 0, 0.02) << id.toStdString();
        EXPECT_NEAR(minY, 0, 0.02) << id.toStdString();
    }
    // The picture of the turned line is the picture of the same line written level, as LineImage draws it
    LineInput level;
    QRectF box;
    for (const InkStroke& s: uprightLine({6, 4}, 2)) {
        box = level.strokes.empty() ? s.box : box.united(s.box);
        level.strokes.push_back(s);
    }
    InkWordBox all{QRectF(QPointF(0, 0), box.size()), {}};
    for (uint32_t i = 0; i < level.strokes.size(); ++i) {
        level.strokes[i] = InkStroke::of(
                [&] {
                    auto pts = level.strokes[i].points;
                    for (QPointF& p: pts) {
                        p -= box.topLeft();
                    }
                    return pts;
                }(),
                level.strokes[i].width);
        all.strokes.push_back(i);
    }
    level.words.push_back(all);
    level.size = box.size();
    const QImage expected = lineImage(level);
    const QImage image(out.filePath(down.value(QStringLiteral("image")).toString()));
    ASSERT_EQ(image.size(), expected.size());
    EXPECT_EQ(image.height(), 128);
    int differ = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            differ += std::abs(qGray(image.pixel(x, y)) - qGray(expected.pixel(x, y))) > 8 ? 1 : 0;
        }
    }
    EXPECT_LT(differ, image.width() * image.height() / 200);
}

// The manifest attached to the form's PDF: found through the .xopp's background, the dataset made from the file
TEST(FormTest, theManifestComesFromTheFormsPdf) {
    QTemporaryDir tmp;
    const QString pdf = tmp.filePath(QStringLiteral("xqt-hwr-test.pdf"));
    writeFormPdf(pdf, MANIFEST, "xqt-hwr-test.manifest.json");
    QString name;
    EXPECT_EQ(embeddedManifest(fs::path(pdf.toStdString()), &name), QByteArray(MANIFEST));
    EXPECT_EQ(name, QStringLiteral("xqt-hwr-test.manifest.json"));
    // A PDF without one, and a file that is not a PDF
    const QString plain = tmp.filePath(QStringLiteral("plain.pdf"));
    writeFormPdf(plain, "{}", "notes.txt");
    EXPECT_TRUE(embeddedManifest(fs::path(plain.toStdString())).isEmpty());
    EXPECT_TRUE(embeddedManifest(fs::path(tmp.filePath(QStringLiteral("missing.pdf")).toStdString())).isEmpty());

    const FormManifest m = FormManifest::parse(MANIFEST);
    std::vector<InkStroke> strokes;
    int seed = 1;
    for (const auto& [id, words]: WRITTEN) {
        for (const InkStroke& s: placed(uprightLine(words, seed++), itemById(m, id))) {
            strokes.push_back(s);
        }
    }
    const QString xopp = tmp.filePath(QStringLiteral("filled.xopp"));
    writeXopp(xopp, pdf, strokes);
    FormExport job;
    job.document = xopp;
    job.out = tmp.filePath(QStringLiteral("set"));
    FormExportResult r = exportForm(job);
    ASSERT_TRUE(r.ok) << r.error.toStdString();
    EXPECT_EQ(r.form, QStringLiteral("xqt-hwr-test"));
    EXPECT_EQ(r.lines, 5);
    EXPECT_EQ(r.empty, 1);
    EXPECT_EQ(r.outside, 0);
    EXPECT_EQ(entriesOf(QDir(job.out)).size(), 5u);
    // On a PDF without a manifest: an error, unless one is given
    const QString other = tmp.filePath(QStringLiteral("other.xopp"));
    writeXopp(other, plain, strokes);
    job.document = other;
    r = exportForm(job);
    EXPECT_FALSE(r.ok);
    EXPECT_TRUE(r.error.contains(QStringLiteral("--manifest"))) << r.error.toStdString();
    const QString file = tmp.filePath(QStringLiteral("form.manifest.json"));
    {
        QFile f(file);
        ASSERT_TRUE(f.open(QIODevice::WriteOnly));
        f.write(MANIFEST);
    }
    job.manifest = file;
    r = exportForm(job);
    ASSERT_TRUE(r.ok) << r.error.toStdString();
    EXPECT_EQ(r.lines, 5);
}

// The benchmark with scripted readings: the layout's lines per box and their angles, CER, words found, search recall,
// text in the drawing, false hits, per group
TEST(FormTest, theBenchmarkComparesThePipelineWithTheManifest) {
    const FormManifest m = FormManifest::parse(MANIFEST);
    auto doc = filledForm(m);
    const std::vector<InkStroke> strokes = strokesOf(*doc->getPage(0));
    const hwr::Layout layout = hwr::layout(strokes);
    // The readings: each line reads its box's text ("Kalmar" for "Kalman"), the scribble in the drawing "words"
    auto fake = std::make_shared<FakeRecognizer>();
    const std::map<QString, QStringList> readings{
            {QStringLiteral("1.1"), {QStringLiteral("Kalmar"), QStringLiteral("filter")}},
            {QStringLiteral("1.2"), {QStringLiteral("margin"), QStringLiteral("note")}},
            {QStringLiteral("1.3"), {QStringLiteral("upwards"), QStringLiteral("note")}},
            {QStringLiteral("1.4"), {QStringLiteral("slanted"), QStringLiteral("words")}},
            {QStringLiteral("1.5"), {QStringLiteral("words")}},
            {QStringLiteral("1.7"), {QStringLiteral("a+b")}}};
    int matched = 0;
    for (const InkLine& line: layout.lines) {
        for (const auto& [id, words]: readings) {
            if (!itemById(m, id).contains(line.box.center())) {
                continue;
            }
            EXPECT_EQ(line.words.size(), static_cast<size_t>(words.size())) << id.toStdString();
            std::vector<FakeRecognizer::Readings> script;
            for (const QString& w: words) {
                script.push_back({{w, 1.0f}});
            }
            fake->setLine(line.hash, script);
            ++matched;
        }
    }
    ASSERT_EQ(matched, 6) << "a line per box";
    const BenchReport report = runBench(*doc, m, {{QStringLiteral("fake"), fake, QString()}}, QStringLiteral("t.xopp"));
    ASSERT_TRUE(report.ok) << report.error.toStdString();
    const QJsonObject groups = report.json.value(QStringLiteral("groups")).toObject();
    const QJsonObject all = groups.value(QStringLiteral("all")).toObject();
    EXPECT_EQ(all.value(QStringLiteral("boxes")).toInt(), 7);
    EXPECT_EQ(all.value(QStringLiteral("written")).toInt(), 6);
    EXPECT_EQ(all.value(QStringLiteral("empty")).toInt(), 1);
    EXPECT_EQ(all.value(QStringLiteral("text_boxes")).toInt(), 5);
    EXPECT_EQ(all.value(QStringLiteral("one_line")).toDouble(), 1.0);
    EXPECT_EQ(all.value(QStringLiteral("angle_ok")).toDouble(), 1.0);
    const QJsonObject fakeAll = all.value(QStringLiteral("models")).toObject().value(QStringLiteral("fake")).toObject();
    // CER: one letter wrong of 13 + 11 + 12 + 13 + 3 letters
    EXPECT_NEAR(fakeAll.value(QStringLiteral("cer")).toDouble(), 1.0 / 52, 1e-4);
    EXPECT_EQ(fakeAll.value(QStringLiteral("terms")).toInt(), 8);
    EXPECT_EQ(fakeAll.value(QStringLiteral("words_found")).toDouble(), 1.0) << "Kalmar: Kalman with a typo";
    EXPECT_EQ(fakeAll.value(QStringLiteral("search_words")).toInt(), 8);
    EXPECT_EQ(fakeAll.value(QStringLiteral("search_recall")).toDouble(), 1.0);
    EXPECT_EQ(fakeAll.value(QStringLiteral("text_in_drawings")).toInt(), 1);
    EXPECT_EQ(fakeAll.value(QStringLiteral("drawing_boxes")).toInt(), 1);
    // The page's search words: kalman filter margin note upwards slanted words (zebra: its box is empty, still a try)
    // "words" hits the drawing: 1 of the 8 tried there
    const QJsonObject drawing = groups.value(QStringLiteral("kind:drawing")).toObject();
    const QJsonObject fakeDrawing = drawing.value(QStringLiteral("models")).toObject().value(QStringLiteral("fake")).toObject();
    EXPECT_NEAR(fakeDrawing.value(QStringLiteral("false_hits")).toDouble(), 1.0 / 8, 1e-4);
    EXPECT_EQ(fakeDrawing.value(QStringLiteral("false_tries")).toInt(), 8);
    const QJsonObject level = groups.value(QStringLiteral("section:B")).toObject();
    EXPECT_NEAR(level.value(QStringLiteral("models")).toObject().value(QStringLiteral("fake")).toObject()
                        .value(QStringLiteral("cer")).toDouble(),
                1.0 / 13, 1e-4);
    EXPECT_TRUE(groups.contains(QStringLiteral("angle:90")));
    EXPECT_TRUE(groups.contains(QStringLiteral("angle:-90")));
    EXPECT_TRUE(groups.contains(QStringLiteral("angle:45")));
    EXPECT_TRUE(groups.contains(QStringLiteral("size:3mm")));
    EXPECT_TRUE(groups.contains(QStringLiteral("kind:math")));
    // Per box
    for (const QJsonValue& v: report.json.value(QStringLiteral("boxes")).toArray()) {
        const QJsonObject b = v.toObject();
        if (b.value(QStringLiteral("id")).toString() == QLatin1String("1.4")) {
            EXPECT_EQ(b.value(QStringLiteral("lines")).toInt(), 1);
            EXPECT_NEAR(b.value(QStringLiteral("line_angles")).toArray().at(0).toDouble(), 45, ANGLE_TOLERANCE);
            EXPECT_EQ(b.value(QStringLiteral("read")).toObject().value(QStringLiteral("fake")).toObject()
                              .value(QStringLiteral("text")).toString(),
                      QStringLiteral("slanted words"));
        }
        if (b.value(QStringLiteral("id")).toString() == QLatin1String("1.6")) {
            EXPECT_FALSE(b.value(QStringLiteral("written")).toBool());
        }
    }
    // Two models side by side, and the Markdown
    const BenchReport two = runBench(*doc, m, {{QStringLiteral("a"), fake, QString()}, {QStringLiteral("b"), std::make_shared<FakeRecognizer>(), QString()}},
                                     QStringLiteral("t.xopp"));
    ASSERT_TRUE(two.ok);
    const QJsonObject twoAll = two.json.value(QStringLiteral("groups")).toObject().value(QStringLiteral("all")).toObject()
                                       .value(QStringLiteral("models")).toObject();
    EXPECT_EQ(twoAll.value(QStringLiteral("a")).toObject().value(QStringLiteral("words_found")).toDouble(), 1.0);
    EXPECT_EQ(twoAll.value(QStringLiteral("b")).toObject().value(QStringLiteral("words_found")).toDouble(), 0.0);
    EXPECT_TRUE(two.markdown.contains(QStringLiteral("CER (a)"))) << two.markdown.toStdString();
    EXPECT_TRUE(two.markdown.contains(QStringLiteral("found (b)")));
    EXPECT_TRUE(two.markdown.contains(QStringLiteral("## By angle")));
    EXPECT_EQ(editsBetween(QStringLiteral("kitten"), QStringLiteral("sitting")), 3);
}

#ifdef XQT_HWR_ONNX
// The built-in model on real ink: the benchmark's line at y 550-575 (the sentence "This is a dumb test, written many
// times" written twice, as the level reading shows) copied into boxes at several angles, read through the whole
// pipeline. A rotation benchmark: it prints the report. The level line is one line; at ±90 degrees a line is found at
// the box's angle (2026-10: a piece of the line is still taken as level there); all three are read
TEST(FormTest, theBuiltInModelReadsRealInkInTurnedBoxes) {
    if (qEnvironmentVariableIsEmpty("XQT_ONNXRUNTIME")) {
        GTEST_SKIP() << "set XQT_ONNXRUNTIME to the path of libonnxruntime.so.1";
    }
    auto loaded = DocumentSession::loadFile(GET_TESTFILE(u8"benchmark/handwritten-text.xopp"));
    ASSERT_TRUE(loaded.document);
    std::vector<InkStroke> band;
    {
        std::shared_lock lock(*loaded.document);
        for (const InkStroke& s: strokesOf(*loaded.document->getPage(0))) {
            double y = 0;
            for (const QPointF& p: s.points) {
                y += p.y();
            }
            y /= static_cast<double>(s.points.size());
            if (y >= 550 && y <= 575) {
                band.push_back(s);
            }
        }
    }
    ASSERT_FALSE(band.empty());
    QRectF bounds;
    for (const InkStroke& s: band) {
        bounds = bounds.isNull() ? s.box : bounds.united(s.box);
    }
    for (InkStroke& s: band) {
        for (QPointF& p: s.points) {
            p -= bounds.center();
        }
        s = InkStroke::of(s.points, s.width, s.widths);
    }
    const QString TEXT = QStringLiteral("This is a dumb test, written many times. This is a dumb test, written many times.");
    const std::vector<double> angles{0, 15, 30, 45, -45, 90, -90, 180};
    QJsonArray items;
    const double wMm = (bounds.width() + 20) / PT_PER_MM, hMm = (bounds.height() + 20) / PT_PER_MM;
    for (size_t k = 0; k < angles.size(); ++k) {
        items.append(QJsonObject{{QStringLiteral("id"), QStringLiteral("1.%1").arg(k + 1)},
                                 {QStringLiteral("page"), static_cast<int>(k + 1)},
                                 {QStringLiteral("section"), QStringLiteral("D")},
                                 {QStringLiteral("kind"), QStringLiteral("line")},
                                 {QStringLiteral("text"), TEXT},
                                 {QStringLiteral("box_mm"), QJsonArray{105 - wMm / 2, 148 - hMm / 2, wMm, hMm}},
                                 {QStringLiteral("angle"), angles[k]},
                                 {QStringLiteral("x_height_mm"), 3}});
    }
    const QJsonObject json{{QStringLiteral("form"), QStringLiteral("xqt-hwr-rotation")},
                           {QStringLiteral("version"), 1},
                           {QStringLiteral("language"), QStringLiteral("en")},
                           {QStringLiteral("items"), items}};
    const FormManifest m = FormManifest::parse(QJsonDocument(json).toJson());
    ASSERT_TRUE(m.valid()) << m.error.toStdString();
    Document doc(nullptr);
    for (const FormItem& item: m.items) {
        auto page = std::make_shared<XojPage>(A4_W, A4_H);
        page->setBackgroundType(PageType(PageTypeFormat::Plain));
        for (const InkStroke& s: placed(band, item)) {
            page->getSelectedLayer()->addElement(strokeOf(s));
        }
        doc.addPage(std::move(page));
    }
    const auto bundled = HandwritingSearch::bundledModels(HandwritingSearch::bundledModelsDir(fs::path(XQT_BUILD_RESOURCE_DIR)));
    auto english = std::find_if(bundled.begin(), bundled.end(), [](const ModelInfo& i) { return i.reads(QStringLiteral("en")); });
    ASSERT_NE(english, bundled.end());
    BenchModel model{english->name, onnxRecognizerFor(english->folder), english->folder};
    QString why;
    ASSERT_TRUE(model.recognizer->ready(&why)) << why.toStdString();
    const BenchReport report = runBench(doc, m, {model}, QStringLiteral("rotation"));
    ASSERT_TRUE(report.ok) << report.error.toStdString();
    std::cout << report.markdown.toStdString();
    std::map<double, QJsonObject> byAngle;
    for (const QJsonValue& v: report.json.value(QStringLiteral("boxes")).toArray()) {
        const QJsonObject b = v.toObject();
        byAngle[b.value(QStringLiteral("angle")).toDouble()] = b;
        std::cout << "angle " << b.value(QStringLiteral("angle")).toDouble() << ": lines "
                  << b.value(QStringLiteral("lines")).toInt() << " at "
                  << QJsonDocument(b.value(QStringLiteral("line_angles")).toArray()).toJson(QJsonDocument::Compact).toStdString()
                  << ", read \""
                  << b.value(QStringLiteral("read")).toObject().value(model.name).toObject().value(QStringLiteral("text"))
                             .toString()
                             .toStdString()
                  << "\"\n";
    }
    EXPECT_EQ(byAngle[0].value(QStringLiteral("lines")).toInt(), 1);
    for (const double a: {0.0, 90.0, -90.0}) {
        const QJsonObject b = byAngle[a];
        const QJsonArray lineAngles = b.value(QStringLiteral("line_angles")).toArray();
        EXPECT_TRUE(std::any_of(lineAngles.begin(), lineAngles.end(),
                                [&](const QJsonValue& v) { return std::abs(v.toDouble() - a) <= ANGLE_TOLERANCE; }))
                << a;
        const QJsonObject read = b.value(QStringLiteral("read")).toObject().value(model.name).toObject();
        EXPECT_GE(read.value(QStringLiteral("found")).toInt(), read.value(QStringLiteral("terms")).toInt() / 2) << a;
    }
}
#endif
