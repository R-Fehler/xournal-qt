/*
 * xournal-qt: how well the built-in model reads handwriting at an angle, measured against the same ink level
 * (qt/docs/features/handwriting-search.md, "Handwriting at an angle"). Not part of the routine run: set
 * XQT_HWR_ROTATION_BENCH to a folder (the table and the line pictures go there) and XQT_ONNXRUNTIME to
 * libonnxruntime.so.1.
 *
 * The lines of the benchmark page that stand alone (BenchmarkInk.h) are read level, then turned around their middle
 * by 90, -90, 45, -45, 30, 15, 135, -135 and 180 degrees, each line alone and all of them together as a page, and laid
 * out and read again. Per angle: the lines found (per line), the character error rate of what was read against the
 * level reading, and the share of the words the search finds (word boxes with one of the line's six words among their
 * candidates, of 12 per line; the level reading finds about a third: the model reads "teat", "writtar"). Then the
 * words of each line written one below the other (lists): no line at an angle may come out of them. Last a page of
 * the level lines with a note of three words written upwards in its margin. XQT_HWR_ROTATION_ONLY="<line>,<angle>"
 * reads that line at that angle only (its pictures go to the folder: the layout's debugging).
 *
 * @license GNU GPLv2 or later
 */
#include <algorithm>
#include <cstdio>
#include <set>

#include <QDir>
#include <QImage>
#include <QStringList>
#include <gtest/gtest.h>

#include "hwr/HandwritingSearch.h"
#include "hwr/HwrInfo.h"
#include "hwr/InkLayout.h"
#include "hwr/LineImage.h"
#include "hwr/ModelInfo.h"
#include "session/InkText.h"
#include "session/Vocabulary.h"

#include "BenchmarkInk.h"

using namespace xqt;
using namespace xqt::hwr;

namespace {
/// What the benchmark's lines say: "This is a dumb test, written many times..." (each line twice); a word box is found
/// by the search when one of its candidates is one of these words
const std::vector<QString> TRUTH{QStringLiteral("this"), QStringLiteral("dumb"), QStringLiteral("test"),
                                 QStringLiteral("written"), QStringLiteral("many"), QStringLiteral("times")};
constexpr int TRUTH_PER_LINE = 12;

struct Reading {
    QString text;  ///< the best readings of its lines, a space between them
    size_t lines = 0;
    std::vector<size_t> wordsPerLine;
    int found = 0;  ///< word boxes with one of TRUTH among their candidates
};

int distance(const QString& a, const QString& b) {
    std::vector<int> row(static_cast<size_t>(b.size()) + 1);
    for (size_t j = 0; j < row.size(); ++j) {
        row[j] = static_cast<int>(j);
    }
    for (qsizetype i = 1; i <= a.size(); ++i) {
        int diagonal = row[0];
        row[0] = static_cast<int>(i);
        for (qsizetype j = 1; j <= b.size(); ++j) {
            const int up = row[static_cast<size_t>(j)];
            row[static_cast<size_t>(j)] = std::min({up + 1, row[static_cast<size_t>(j - 1)] + 1,
                                                    diagonal + (a[i - 1] == b[j - 1] ? 0 : 1)});
            diagonal = up;
        }
    }
    return row.back();
}

void writePicture(const LineInput& in, const QString& path) {
    const auto pieces = piecesOf(in, 1000);
    if (pieces.empty()) {
        return;
    }
    int w = 0, h = 0;
    const auto grey = greyOf(in, pieces[0], w, h);
    QImage img(w, h, QImage::Format_Grayscale8);
    for (int y = 0; y < h; ++y) {
        std::copy_n(grey.data() + static_cast<size_t>(y) * static_cast<size_t>(w), w, img.scanLine(y));
    }
    img.save(path);
}

Reading read(Recognizer& rec, const std::vector<InkStroke>& strokes, const QString& pictures = QString()) {
    static const std::set<words::Id> truth = [] {
        std::set<words::Id> ids;
        for (const QString& w: TRUTH) {
            ids.insert(words::idOf(w));
        }
        return ids;
    }();
    Reading r;
    const hwr::Layout l = layout(strokes);
    // (in the order of writing: the same order whichever way the page is turned)
    std::vector<const InkLine*> lines;
    for (const InkLine& line: l.lines) {
        lines.push_back(&line);
    }
    std::sort(lines.begin(), lines.end(), [](const InkLine* a, const InkLine* b) { return a->strokes.front() < b->strokes.front(); });
    QStringList texts;
    for (const InkLine* line: lines) {
        const LineInput in = LineInput::of(strokes, l, *line);
        if (!pictures.isEmpty()) {
            writePicture(in, pictures + QStringLiteral("-l%1-a%2.png").arg(r.lines).arg(line->angle));
        }
        ++r.lines;
        r.wordsPerLine.push_back(line->words.size());
        const auto result = rec.recognizeLine(in, {});
        if (!result) {
            continue;
        }
        QStringList words;
        for (const ink::Word& w: result->words) {
            words << w.text;
            r.found += std::any_of(w.candidates.begin(), w.candidates.end(),
                                   [](const ink::Candidate& c) { return truth.count(c.word) > 0; })
                               ? 1
                               : 0;
        }
        texts << words.join(u' ');
    }
    r.text = texts.join(u' ');
    return r;
}

/// The means over lines (or one page): lines found per line, the character error rate against the level reading,
/// the share of the words the search finds
struct Score {
    double lines = 0, cer = 0, found = 0;
    int n = 0;
    void add(const Reading& level, const Reading& turned, int words) {
        lines += static_cast<double>(turned.lines) / static_cast<double>(std::max<size_t>(1, level.lines));
        cer += static_cast<double>(distance(level.text, turned.text)) / std::max<qsizetype>(1, level.text.size());
        found += static_cast<double>(turned.found) / words;
        ++n;
    }
    QString row() const {
        return QStringLiteral("%1 | %2 | %3").arg(lines / n, 0, 'f', 2).arg(cer / n, 0, 'f', 3).arg(found / n, 0, 'f', 2);
    }
};

QString counts(const Reading& r) {
    QStringList w;
    for (const size_t n: r.wordsPerLine) {
        w << QString::number(n);
    }
    return w.join(u'/');
}
}  // namespace

TEST(HwrRotationBenchmark, turnedHandwritingIsReadAsLevel) {
    const QString out = qEnvironmentVariable("XQT_HWR_ROTATION_BENCH");
    if (out.isEmpty() || qEnvironmentVariableIsEmpty("XQT_ONNXRUNTIME")) {
        GTEST_SKIP() << "set XQT_HWR_ROTATION_BENCH to a folder and XQT_ONNXRUNTIME to libonnxruntime.so.1";
    }
    QDir().mkpath(out);
    const auto models = HandwritingSearch::bundledModels(HandwritingSearch::bundledModelsDir(fs::path(XQT_BUILD_RESOURCE_DIR)));
    ASSERT_FALSE(models.empty());
    const auto rec = onnxRecognizerFor(models[0].folder);
    QString why;
    ASSERT_TRUE(rec->ready(&why)) << why.toStdString();

    const std::vector<std::vector<InkStroke>> lines = test::benchmarkLines();
    ASSERT_GE(lines.size(), 3u);

    // XQT_HWR_ROTATION_ONLY="<line>,<angle>": that line at that angle only (its pictures; the layout's debugging)
    const QStringList only = qEnvironmentVariable("XQT_HWR_ROTATION_ONLY").split(u',', Qt::SkipEmptyParts);
    if (only.size() == 2) {
        const auto k = static_cast<size_t>(only[0].toInt());
        ASSERT_LT(k, lines.size());
        const Reading r = read(*rec, test::turnedAround(lines[k], only[1].toDouble()), QDir(out).filePath(QStringLiteral("only")));
        std::printf("[%zu lines, words %s] %s\n", r.lines, counts(r).toUtf8().constData(), r.text.toUtf8().constData());
        return;
    }

    const QString file = QDir(out).filePath(QStringLiteral("readings.txt"));
    FILE* log = std::fopen(file.toLocal8Bit().constData(), "w");
    ASSERT_TRUE(log);
    auto say = [&](const QString& s) {
        std::fprintf(log, "%s\n", s.toUtf8().constData());
        std::printf("%s\n", s.toUtf8().constData());
    };
    // Lists: the words of each line written one below the other (left aligned, at 1.2 and 1.6 times the line's height),
    // the strokes of each word in their order: none of it may be taken for a line written downwards
    for (const double pitch: {1.2, 1.6}) {
        int turnedLines = 0;
        for (const auto& line: lines) {
            for (const InkLine& x: layout(test::listOf(line, pitch)).lines) {
                turnedLines += x.angle != 0 ? 1 : 0;
            }
        }
        say(QStringLiteral("lists at %1 times the line height: %2 lines at an angle in %3 lists").arg(pitch).arg(turnedLines).arg(lines.size()));
    }
    std::vector<Reading> levelReadings;
    for (size_t k = 0; k < lines.size(); ++k) {
        levelReadings.push_back(read(*rec, lines[k], QDir(out).filePath(QStringLiteral("line%1-level").arg(k))));
    }
    QStringList table{QStringLiteral("| angle | lines (each alone) | CER | words found | lines (page) | CER (page) | words found (page) |"),
                      QStringLiteral("| --- | --- | --- | --- | --- | --- | --- |")};
    // The lines together as a page (in their places), read level
    std::vector<InkStroke> together;
    for (const auto& l: lines) {
        together.insert(together.end(), l.begin(), l.end());
    }
    const Reading pageLevel = read(*rec, together);
    const int pageWords = TRUTH_PER_LINE * static_cast<int>(lines.size());
    for (const double angle: {0.0, 90.0, -90.0, 45.0, -45.0, 30.0, 15.0, 135.0, -135.0, 180.0}) {
        Score alone;
        for (size_t k = 0; k < lines.size(); ++k) {
            const Reading r = read(*rec, test::turnedAround(lines[k], angle),
                                   QDir(out).filePath(QStringLiteral("line%1-turned%2").arg(k).arg(angle)));
            say(QStringLiteral("line %1 at %2: [%3 lines, words %4] %5").arg(k).arg(angle).arg(r.lines).arg(counts(r), r.text));
            alone.add(levelReadings[k], r, TRUTH_PER_LINE);
        }
        Score whole;
        const Reading r = read(*rec, test::turnedAround(together, angle));
        whole.add(pageLevel, r, pageWords);
        table << QStringLiteral("| %1 | %2 | %3 of %4 | %5 |")
                         .arg(angle)
                         .arg(alone.row())
                         .arg(r.lines)
                         .arg(pageLevel.lines)
                         .arg(whole.row().section(QStringLiteral(" | "), 1));
    }
    // A mixed page: the other level lines, and three words of the first ("dumb test, written") turned upwards into the
    // left margin
    {
        std::vector<InkStroke> mixed;
        for (size_t k = 1; k < lines.size(); ++k) {
            mixed.insert(mixed.end(), lines[k].begin(), lines[k].end());
        }
        std::vector<InkStroke> words;
        {
            const hwr::Layout first = layout(lines[0]);
            ASSERT_GE(first.lines.at(0).words.size(), 6u);
            std::vector<uint32_t> which;
            for (size_t w = 3; w < 6; ++w) {
                const auto& ws = first.lines[0].words[w].strokes;
                which.insert(which.end(), ws.begin(), ws.end());
            }
            std::sort(which.begin(), which.end());
            for (const uint32_t i: which) {
                words.push_back(lines[0][i]);
            }
        }
        const Reading noteLevel = read(*rec, words);
        std::vector<InkStroke> note = test::turnedAround(words, -90);
        QRectF box, all;
        for (const InkStroke& s: note) {
            box = box.isNull() ? s.box : box.united(s.box);
        }
        for (const InkStroke& s: mixed) {
            all = all.isNull() ? s.box : all.united(s.box);
        }
        const Reading body = read(*rec, mixed);
        for (InkStroke& s: note) {
            for (QPointF& p: s.points) {
                p += QPointF(all.left() - 30 - box.right(), all.center().y() - box.center().y());
            }
            s = InkStroke::of(s.points, s.width, s.widths);
        }
        std::vector<InkStroke> withNote = mixed;
        withNote.insert(withNote.end(), note.begin(), note.end());
        const Reading both = read(*rec, withNote, QDir(out).filePath(QStringLiteral("mixed")));
        // (the note is written last: its reading comes last)
        const QString noteRead = both.text.startsWith(body.text) ? both.text.mid(body.text.size()).trimmed() : QString();
        say(QStringLiteral("mixed: note level \"%1\", in the margin \"%2\"; the page: %3 lines (%4 without the note), the "
                           "level lines read as without it: %5")
                    .arg(noteLevel.text, noteRead)
                    .arg(both.lines)
                    .arg(body.lines)
                    .arg(both.text.startsWith(body.text) ? QStringLiteral("yes") : QStringLiteral("no")));
        table << QStringLiteral("| note of 3 words at -90 on a page | %1 | %2 | %3 | %4 of %5 | | |")
                         .arg(both.lines - body.lines)
                         .arg(static_cast<double>(distance(noteLevel.text, noteRead)) / std::max<qsizetype>(1, noteLevel.text.size()), 0, 'f', 3)
                         .arg((both.found - body.found) / 3.0, 0, 'f', 2)
                         .arg(both.lines)
                         .arg(body.lines + 1);
    }
    say(QStringLiteral("%1 lines that stand alone, %2 of them on the page level").arg(lines.size()).arg(pageLevel.lines));
    say(table.join(u'\n'));
    std::fclose(log);
}
