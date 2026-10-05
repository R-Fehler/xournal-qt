/*
 * xournal-qt: a document's handwriting as a line dataset (LineDataset.h, `xournal-qt-cli hwr-lines`), in the format
 * the training reads (qt/research/hwr/train/FORMATS.md, §1).
 *
 * @license GNU GPLv2 or later
 */
#include <set>
#include <shared_mutex>

#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include "hwr/InkLayout.h"
#include "hwr/LineDataset.h"
#include "model/Document.h"
#include "model/Element.h"
#include "model/Layer.h"
#include "model/Stroke.h"
#include "model/XojPage.h"
#include "session/DocumentSession.h"

#include "config-test.h"

using namespace xqt;
using namespace xqt::hwr;

namespace {
const QString BENCHMARK = [] {
    const std::u8string path = GET_TESTFILE(u8"benchmark/handwritten-text.xopp");
    return QString::fromUtf8(reinterpret_cast<const char*>(path.data()), static_cast<qsizetype>(path.size()));
}();

/// The lines the search's layout finds in the document (what the export must give)
int linesOf(const QString& file) {
    auto r = DocumentSession::loadFile(fs::path(file.toStdString()));
    if (!r.document) {
        return -1;
    }
    std::shared_lock lock(*r.document);
    int n = 0;
    for (size_t p = 0; p < r.document->getPageCount(); ++p) {
        const auto strokes = strokesOf(*r.document->getPage(p));
        n += strokes.empty() ? 0 : static_cast<int>(layout(strokes).lines.size());
    }
    return n;
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

QString writeText(const QTemporaryDir& dir, const QStringList& lines) {
    const QString path = dir.filePath(QStringLiteral("transcripts.txt"));
    QFile f(path);
    f.open(QIODevice::WriteOnly);
    f.write(lines.join(u'\n').toUtf8() + '\n');
    return path;
}
}  // namespace

// The benchmark page: one entry, one picture and one file of ink per line the layout finds, in reading order
TEST(LineDatasetTest, theBenchmarkBecomesADataset) {
    QTemporaryDir tmp;
    const int expected = linesOf(BENCHMARK);
    ASSERT_GE(expected, 10);
    LineExport job;
    job.document = BENCHMARK;
    job.out = tmp.filePath(QStringLiteral("set"));
    job.language = QStringLiteral("en");
    job.writer = QStringLiteral("w01");
    const LineExportResult r = exportLines(job);
    ASSERT_TRUE(r.ok) << r.error.toStdString();
    EXPECT_EQ(r.lines, expected);
    EXPECT_TRUE(r.warnings.isEmpty());
    const QDir out(job.out);
    // dataset.json
    QFile ds(out.filePath(QStringLiteral("dataset.json")));
    ASSERT_TRUE(ds.open(QIODevice::ReadOnly));
    const QJsonObject d = QJsonDocument::fromJson(ds.readAll()).object();
    EXPECT_EQ(d.value(QStringLiteral("kind")).toString(), QStringLiteral("ink"));
    EXPECT_EQ(d.value(QStringLiteral("version")).toInt(), 1);
    EXPECT_EQ(d.value(QStringLiteral("languages")).toArray(), QJsonArray{QStringLiteral("en")});
    EXPECT_EQ(d.value(QStringLiteral("licence")).toString(), QStringLiteral("private"));
    EXPECT_TRUE(d.value(QStringLiteral("noncommercial")).toBool());
    EXPECT_EQ(d.value(QStringLiteral("source")).toString(), QStringLiteral("handwritten-text.xopp"));
    EXPECT_FALSE(d.value(QStringLiteral("name")).toString().isEmpty());
    // lines.jsonl
    const auto entries = entriesOf(out);
    ASSERT_EQ(static_cast<int>(entries.size()), expected);
    std::set<QString> ids;
    int lastPage = 0, lastLine = 0;
    for (const QJsonObject& e: entries) {
        const QString id = e.value(QStringLiteral("id")).toString();
        EXPECT_TRUE(ids.insert(id).second) << "unique ids";
        EXPECT_TRUE(id.startsWith(QStringLiteral("w01-handwritten_text-p01-l"))) << id.toStdString();
        EXPECT_EQ(e.value(QStringLiteral("writer")).toString(), QStringLiteral("w01"));
        EXPECT_EQ(e.value(QStringLiteral("lang")).toString(), QStringLiteral("en"));
        EXPECT_EQ(e.value(QStringLiteral("text")).toString(), QString());
        const int page = e.value(QStringLiteral("page")).toInt(), line = e.value(QStringLiteral("line")).toInt();
        EXPECT_TRUE(page > lastPage || (page == lastPage && line > lastLine)) << "reading order";
        lastPage = page;
        lastLine = line;
        // The picture: 128 px high, grayscale, ink dark on white
        const QImage image(out.filePath(e.value(QStringLiteral("image")).toString()));
        ASSERT_FALSE(image.isNull()) << e.value(QStringLiteral("image")).toString().toStdString();
        EXPECT_EQ(image.height(), 128);
        EXPECT_TRUE(image.isGrayscale());
        EXPECT_EQ(qGray(image.pixel(0, 0)), 255) << "white margin";
        // The ink, relative to the line
        QFile s(out.filePath(e.value(QStringLiteral("strokes")).toString()));
        ASSERT_TRUE(s.open(QIODevice::ReadOnly));
        const QJsonObject ink = QJsonDocument::fromJson(s.readAll()).object();
        EXPECT_GT(ink.value(QStringLiteral("width")).toDouble(), 0);
        EXPECT_GT(ink.value(QStringLiteral("height")).toDouble(), 0);
        const QJsonArray strokes = ink.value(QStringLiteral("strokes")).toArray();
        ASSERT_FALSE(strokes.isEmpty());
        const QJsonArray point = strokes.at(0).toObject().value(QStringLiteral("points")).toArray().at(0).toArray();
        ASSERT_EQ(point.size(), 3);
        EXPECT_GE(point.at(0).toDouble(), -50);
        EXPECT_LE(point.at(0).toDouble(), ink.value(QStringLiteral("width")).toDouble() + 50);
        EXPECT_GT(point.at(2).toDouble(), 0) << "pressure";
    }
    EXPECT_EQ(QDir(out.filePath(QStringLiteral("images"))).entryList(QDir::Files).size(), expected);
    EXPECT_EQ(QDir(out.filePath(QStringLiteral("strokes"))).entryList(QDir::Files).size(), expected);
}

// Transcripts: one line of text per line of ink, in reading order; NFC; empty and "#" lines skipped; a different number
// is matched as far as it goes and warned about
TEST(LineDatasetTest, transcriptsAlignWithTheLines) {
    QTemporaryDir tmp;
    const int n = linesOf(BENCHMARK);
    ASSERT_GT(n, 2);
    QStringList texts{QStringLiteral("# the benchmark page"), QString()};
    for (int i = 0; i < n; ++i) {
        texts << QStringLiteral("line %1").arg(i + 1);
    }
    texts[2] = QStringLiteral("Verst") + QChar(u'a') + QChar(0x0308) + QStringLiteral("rkung");  // (decomposed)
    LineExport job;
    job.document = BENCHMARK;
    job.out = tmp.filePath(QStringLiteral("set"));
    job.texts = writeText(tmp, texts);
    job.language = QStringLiteral("de");
    LineExportResult r = exportLines(job);
    ASSERT_TRUE(r.ok) << r.error.toStdString();
    EXPECT_EQ(r.transcripts, n);
    EXPECT_TRUE(r.warnings.isEmpty());
    auto entries = entriesOf(QDir(job.out));
    ASSERT_EQ(static_cast<int>(entries.size()), n);
    EXPECT_EQ(entries[0].value(QStringLiteral("text")).toString(), QStringLiteral("Verstärkung"));
    EXPECT_EQ(entries[0].value(QStringLiteral("text")).toString().size(), 11) << "NFC: ä is one letter";
    for (int i = 1; i < n; ++i) {
        EXPECT_EQ(entries[static_cast<size_t>(i)].value(QStringLiteral("text")).toString(),
                  QStringLiteral("line %1").arg(i + 1));
    }
    EXPECT_EQ(entries[0].value(QStringLiteral("lang")).toString(), QStringLiteral("de"));
    // One text too few: matched as far as it goes, the last line without text, and a warning
    texts.removeLast();
    job.texts = writeText(tmp, texts);
    r = exportLines(job);
    ASSERT_TRUE(r.ok);
    ASSERT_EQ(r.warnings.size(), 1);
    EXPECT_TRUE(r.warnings[0].contains(QStringLiteral("%1 lines of ink, %2 lines of text").arg(n).arg(n - 1)));
    entries = entriesOf(QDir(job.out));
    ASSERT_EQ(static_cast<int>(entries.size()), n);
    EXPECT_EQ(entries.back().value(QStringLiteral("text")).toString(), QString());
}

TEST(LineDatasetTest, aDocumentThatCannotBeReadIsAnError) {
    QTemporaryDir tmp;
    LineExport job;
    job.document = tmp.filePath(QStringLiteral("missing.xopp"));
    job.out = tmp.filePath(QStringLiteral("set"));
    const LineExportResult r = exportLines(job);
    EXPECT_FALSE(r.ok);
    EXPECT_FALSE(r.error.isEmpty());
    job.document = BENCHMARK;
    job.texts = tmp.filePath(QStringLiteral("missing.txt"));
    EXPECT_FALSE(exportLines(job).ok);
}

// The "Handwriting sample" pages (qt/research/hwr/sample): written under every prompt, they give one line per
// sentence, with the sentences of the language's file as their texts
TEST(LineDatasetTest, theHandwritingSampleGivesASentencePerLine) {
    const QString dir = QStringLiteral(XQT_HWR_TEST_DATA "/../../../research/hwr/sample/");
    for (const char* lang: {"en", "de"}) {
        QTemporaryDir tmp;
        auto loaded = DocumentSession::loadFile(
                fs::path((dir + QStringLiteral("handwriting-sample-%1.xopp").arg(QLatin1String(lang))).toStdString()));
        ASSERT_TRUE(loaded.document) << loaded.error;
        Document& doc = *loaded.document;
        int prompts = 0;
        for (size_t p = 0; p < doc.getPageCount(); ++p) {
            Layer* layer = doc.getPage(p)->getSelectedLayer();
            std::vector<double> ys;
            for (const auto& e: layer->getElementsView()) {
                if (e->getType() == ELEMENT_TEXT && e->getBoundingBox().y > 60) {
                    ys.push_back(e->getBoundingBox().y);
                }
            }
            // A sentence of eight words, a little under each prompt (each written a little differently)
            for (const double y: ys) {
                ++prompts;
                for (int w = 0; w < 8; ++w) {
                    auto s = std::make_unique<Stroke>();
                    s->setWidth(1.0);
                    for (int i = 0; i <= 8; ++i) {
                        s->addPoint(Point(50 + 60 * w + i * 4.0, y + 28 + (i % 2 ? 14 : 0) + (i == 1 ? 0.1 * prompts : 0)));
                    }
                    layer->addElement(std::move(s));
                }
            }
        }
        LineExport job;
        job.document = dir + QStringLiteral("handwriting-sample-%1.xopp").arg(QLatin1String(lang));
        job.out = tmp.filePath(QStringLiteral("set"));
        job.texts = dir + QStringLiteral("sentences-%1.txt").arg(QLatin1String(lang));
        job.language = QLatin1String(lang);
        const LineExportResult r = exportLines(job, doc);
        ASSERT_TRUE(r.ok) << r.error.toStdString();
        EXPECT_EQ(r.lines, prompts) << lang;
        EXPECT_EQ(r.transcripts, prompts) << lang;
        EXPECT_TRUE(r.warnings.isEmpty()) << r.warnings.join(u' ').toStdString();
        QFile f(job.texts);
        ASSERT_TRUE(f.open(QIODevice::ReadOnly));
        const QStringList sentences = transcriptsOf(f.readAll());
        const auto entries = entriesOf(QDir(job.out));
        ASSERT_EQ(entries.size(), static_cast<size_t>(sentences.size()));
        for (size_t i = 0; i < entries.size(); ++i) {
            EXPECT_EQ(entries[i].value(QStringLiteral("text")).toString(), sentences[static_cast<qsizetype>(i)]);
        }
    }
}
