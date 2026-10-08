/*
 * xournal-qt: handwriting forms (qt/research/hwr/forms/DESIGN.md): the manifest attached to the form's PDF, strokes
 * mapped to boxes at any angle, the dataset of a filled form (`xournal-qt-cli hwr-form`, FormDataset.h).
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

#include "hwr/FormDataset.h"
#include "hwr/FormManifest.h"
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
