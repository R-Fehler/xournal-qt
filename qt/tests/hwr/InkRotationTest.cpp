/*
 * xournal-qt: handwriting written at an angle (InkLayout.h, framesOf): a margin note written upwards or downwards, a
 * line at 35 degrees, laid out in its own frame and read upright; its words marked, copied and put into the PDF text
 * layer turned with the ink. Horizontal handwriting comes out as before (InkLayoutTest: the benchmark page).
 *
 * @license GNU GPLv2 or later
 */
#include <atomic>
#include <memory>
#include <mutex>

#include <QTemporaryDir>
#include <gtest/gtest.h>

#include "hwr/FakeRecognizer.h"
#include "hwr/InkCopy.h"
#include "hwr/InkLayout.h"
#include "hwr/InkRecognitionService.h"
#include "hwr/LineImage.h"
#include "session/AppContext.h"
#include "session/DocumentSearch.h"
#include "session/DocumentSession.h"
#include "session/DocumentTextIndex.h"
#include "session/HybridPdf.h"
#include "session/InkText.h"
#include "session/InkTextLayer.h"
#include "support/SearchHits.h"
#include "support/TestSupport.h"

#include "BenchmarkInk.h"

using namespace xqt;
using namespace xqt::hwr;
using xqt::test::waitFor;

namespace {
constexpr double HEIGHT = 10;   ///< of the letters
constexpr double LETTERS = 6;   ///< per word
constexpr double STEP = 60;     ///< from one word to the next (a word is 36 pt long: gaps of 24)

/// A word written upright at x: a zigzag that starts at its top-left (so the frame it is read in shows which way is up)
std::vector<QPointF> zigzag(double x) {
    std::vector<QPointF> pts;
    for (int i = 0; i <= 2 * LETTERS; ++i) {
        pts.emplace_back(x + i * HEIGHT * 0.3, i % 2 ? HEIGHT : 0);
    }
    return pts;
}

/// A line of `words` words written from `start` in the direction `angle` (degrees, clockwise on the page)
std::vector<InkStroke> lineAt(QPointF start, double angle, int words) {
    std::vector<InkStroke> out;
    for (int k = 0; k < words; ++k) {
        std::vector<QPointF> pts = zigzag(k * STEP);
        for (QPointF& p: pts) {
            p = start + ink::turned(p, angle);
        }
        out.push_back(InkStroke::of(std::move(pts), 1.0f));
    }
    return out;
}

/// `lines` horizontal lines of 4 words, 40 pt apart, from (100, 100)
std::vector<InkStroke> body(int lines) {
    std::vector<InkStroke> out;
    for (int l = 0; l < lines; ++l) {
        auto line = lineAt({100, 100.0 + 40 * l}, 0, 4);
        out.insert(out.end(), line.begin(), line.end());
    }
    return out;
}

void append(std::vector<InkStroke>& to, const std::vector<InkStroke>& more) {
    to.insert(to.end(), more.begin(), more.end());
}

/// Every stroke of the line is upright as the recogniser gets it: it starts at the top-left of its box
bool upright(const LineInput& in) {
    for (const InkStroke& s: in.strokes) {
        if (s.points.empty() || std::abs(s.points.front().x() - s.box.left()) > 1e-6 ||
            std::abs(s.points.front().y() - s.box.top()) > 1e-6) {
            return false;
        }
    }
    return in.size.width() > in.size.height();
}

/// The point lies in the word's turned box (a little tolerance)
bool inWord(const ink::Word& w, QPointF p) {
    const QPointF c = w.box.center();
    return w.box.adjusted(-1e-6, -1e-6, 1e-6, 1e-6).contains(c + ink::turned(p - c, -w.angle));
}
}  // namespace

// At 0 and 15 degrees the page's rules lay the line out (a slope is fine); at 35, 90 and -90 (written upwards) it is
// laid out in its own frame: one line, its words, and the angle it was written in
TEST(InkRotationTest, aLineAtAnAngleIsOneLineInItsOwnFrame) {
    for (const double angle: {0.0, 15.0, 35.0, -35.0, 90.0, -90.0}) {
        SCOPED_TRACE(angle);
        // (at 15 degrees the page's rules take 3 words as one line: the 4th is too far below the 1st, as before)
        const size_t words = angle == 15 ? 3 : 4;
        const auto strokes = lineAt({300, 400}, angle, static_cast<int>(words));
        const hwr::Layout l = layout(strokes);
        ASSERT_EQ(l.lines.size(), 1u);
        const InkLine& line = l.lines[0];
        if (angle == 15) {
            EXPECT_EQ(line.angle, 0);  // (its words: by the page's rules, as before)
            continue;
        }
        EXPECT_EQ(line.words.size(), words);
        EXPECT_EQ(line.strokes.size(), words);
        EXPECT_TRUE(l.drawings.empty());
        if (std::abs(angle) < 20) {
            EXPECT_EQ(line.angle, 0);
        } else if (std::abs(angle) == 90) {
            EXPECT_EQ(line.angle, angle);  // (exactly)
        } else {
            EXPECT_NEAR(line.angle, angle, 0.5);
        }
        // Read upright: the recogniser's strokes start at their top-left, the line is wider than high
        const LineInput in = LineInput::of(strokes, l, line);
        EXPECT_TRUE(upright(in)) << in.size.width() << " x " << in.size.height();
        if (line.angle != 0) {
            EXPECT_NEAR(in.size.width(), 3 * STEP + 2 * LETTERS * HEIGHT * 0.3, 0.5);
            EXPECT_NEAR(in.size.height(), HEIGHT, 0.5);
            // Its origin is where the line starts on the page (the top-left of its first word, upright)
            EXPECT_NEAR(line.origin().x(), 300, 0.5);
            EXPECT_NEAR(line.origin().y(), 400, 0.5);
        }
        // The picture the recogniser gets is a line: much wider than high
        int w = 0, h = 0;
        const auto pieces = piecesOf(in);
        ASSERT_EQ(pieces.size(), 1u);
        greyOf(in, pieces[0], w, h);
        EXPECT_GT(w, 4 * h);
    }
}

// Two lines written upwards beside each other are two lines; a note written downwards and one upwards are apart
TEST(InkRotationTest, parallelLinesAtAnAngleAreLinesOfTheirOwn) {
    std::vector<InkStroke> s = lineAt({100, 600}, -90, 4);
    append(s, lineAt({130, 600}, -90, 3));
    append(s, lineAt({500, 100}, 90, 4));
    const hwr::Layout l = layout(s);
    ASSERT_EQ(l.lines.size(), 3u);
    int up = 0, down = 0;
    for (const InkLine& line: l.lines) {
        up += line.angle == -90 ? 1 : 0;
        down += line.angle == 90 ? 1 : 0;
    }
    EXPECT_EQ(up, 2);
    EXPECT_EQ(down, 1);
}

// A page of horizontal text with a note written upwards in its margin: the text is laid out exactly as without the
// note (the same lines, words and hashes), the note as a line of its own
TEST(InkRotationTest, aMixedPageGivesBoth) {
    const auto text = body(5);
    std::vector<InkStroke> page = text;
    append(page, lineAt({40, 330}, -90, 3));
    const hwr::Layout alone = layout(text);
    const hwr::Layout l = layout(page);
    ASSERT_EQ(alone.lines.size(), 5u);
    ASSERT_EQ(l.lines.size(), 6u);
    std::vector<const InkLine*> level;
    const InkLine* note = nullptr;
    for (const InkLine& line: l.lines) {
        (line.angle == 0 ? level.push_back(&line) : void(note = &line));
    }
    ASSERT_EQ(level.size(), 5u);
    for (size_t i = 0; i < 5; ++i) {
        EXPECT_EQ(level[i]->hash, alone.lines[i].hash);
        EXPECT_EQ(level[i]->strokes, alone.lines[i].strokes);
        ASSERT_EQ(level[i]->words.size(), alone.lines[i].words.size());
        for (size_t k = 0; k < level[i]->words.size(); ++k) {
            EXPECT_EQ(level[i]->words[k].box, alone.lines[i].words[k].box);
        }
    }
    ASSERT_TRUE(note);
    EXPECT_EQ(note->angle, -90);
    EXPECT_EQ(note->words.size(), 3u);
    EXPECT_EQ(note->strokes, (std::vector<uint32_t>{20, 21, 22}));
}

// A line at an angle moved with the lasso keeps its hash (and so what was read in it); turned with the lasso within
// its range of angles too
TEST(InkRotationTest, aMovedOrTurnedLineAtAnAngleKeepsItsHash) {
    for (const double angle: {35.0, 90.0, -90.0}) {
        SCOPED_TRACE(angle);
        const auto strokes = lineAt({300, 400}, angle, 4);
        const quint64 before = layout(strokes).lines.at(0).hash;
        auto moved = strokes;
        for (InkStroke& s: moved) {
            for (QPointF& p: s.points) {
                p += QPointF(50.3, -120.7);
            }
            s = InkStroke::of(s.points, s.width, s.widths);
        }
        const hwr::Layout after = layout(moved);
        ASSERT_EQ(after.lines.size(), 1u);
        EXPECT_EQ(after.lines[0].hash, before);
        EXPECT_NEAR(after.lines[0].origin().x(), 350.3, 0.5);
    }
    // Turned by 10 degrees with the lasso (around a point of its own): 35 becomes 45, read the same
    const auto strokes = lineAt({300, 400}, 35, 4);
    const quint64 before = layout(strokes).lines.at(0).hash;
    auto turnedLine = strokes;
    for (InkStroke& s: turnedLine) {
        s = turned(s, 10);
    }
    const hwr::Layout after = layout(turnedLine);
    ASSERT_EQ(after.lines.size(), 1u);
    EXPECT_NEAR(after.lines[0].angle, 45, 0.5);
    EXPECT_EQ(after.lines[0].hash, before);
}

// A column of words written one below the other (a list) is not taken for a line written downwards
TEST(InkRotationTest, aListIsNotALineWrittenDownwards) {
    std::vector<InkStroke> s;
    for (int i = 0; i < 8; ++i) {
        append(s, lineAt({100, 100.0 + 20 * i}, 0, 1));
    }
    const hwr::Layout l = layout(s);
    ASSERT_EQ(l.lines.size(), 8u);
    for (const InkLine& line: l.lines) {
        EXPECT_EQ(line.angle, 0);
    }
}

// Real handwriting (the benchmark's lines that stand alone) turned up, down and to a free angle is one line in its own
// frame, with all the strokes the level line has, as many words as level, and nothing left on the page: the T's bar
// (a straight stroke), a "Th" written in one stroke (its box turned is big), a word after a gap wider than a letter
// and the dots after it belong to the line
TEST(InkRotationTest, realHandwritingAtAnAngleIsOneLine) {
    const auto lines = test::benchmarkLines();
    ASSERT_GE(lines.size(), 4u);
    for (size_t k = 0; k < lines.size(); ++k) {
        const hwr::Layout level = layout(lines[k]);
        ASSERT_EQ(level.lines.size(), 1u);
        for (const double angle: {90.0, -90.0, 45.0, -45.0, 30.0}) {
            SCOPED_TRACE(QStringLiteral("line %1 at %2").arg(k).arg(angle).toStdString());
            const hwr::Layout l = layout(test::turnedAround(lines[k], angle));
            ASSERT_EQ(l.lines.size(), 1u);
            if (std::abs(angle) == 90) {
                EXPECT_EQ(l.lines[0].angle, angle);
            } else {
                EXPECT_NEAR(l.lines[0].angle, angle, 3);
            }
            EXPECT_EQ(l.lines[0].strokes, level.lines[0].strokes);
            EXPECT_EQ(l.drawings, level.drawings);
        }
    }
}

// The words of real lines written one below the other (lists, close or wide) are level lines, none at an angle
TEST(InkRotationTest, aListOfRealWordsIsNotALineWrittenDownwards) {
    for (const auto& line: test::benchmarkLines()) {
        for (const double pitch: {1.2, 1.6}) {
            SCOPED_TRACE(pitch);
            const hwr::Layout l = layout(test::listOf(line, pitch));
            EXPECT_GE(l.lines.size(), 8u);
            for (const InkLine& x: l.lines) {
                EXPECT_EQ(x.angle, 0);
            }
        }
    }
}

// Through the worker and the scripted recogniser: every line it gets is upright, and the words come back over the ink
TEST(InkRotationTest, theRecogniserReadsUprightAndTheWordsLieOverTheInk) {
    InkRecognitionService service;
    auto fake = std::make_shared<FakeRecognizer>();
    std::mutex mtx;
    std::vector<bool> seen;
    fake->setScript([&](const LineInput& line, size_t word) {
        if (word == 0) {
            std::lock_guard lock(mtx);
            seen.push_back(upright(line));
        }
        return FakeRecognizer::Readings{{QStringLiteral("w%1").arg(word), 1.0f}};
    });
    service.setRecognizer(fake);
    service.setActivityPause(0);
    std::vector<InkStroke> page = body(2);
    append(page, lineAt({40, 330}, -90, 3));
    append(page, lineAt({560, 100}, 90, 3));
    append(page, lineAt({150, 300}, 35, 4));
    InkRecognitionService::Job job;
    job.strokes = page;
    PageResult got;
    bool done = false;
    QObject owner;
    service.submit(&owner, std::move(job), [&](PageResult r) {
        got = std::move(r);
        done = true;
    });
    ASSERT_TRUE(waitFor([&] { return done; }, 10000));
    ASSERT_EQ(got.lines.size(), 5u);
    {
        std::lock_guard lock(mtx);
        // (two lines read: a line is known by its strokes upright, so the lines of 4 words, the horizontal ones and the
        // one at 35 degrees, are one line to the cache, and so are the notes of 3 words written up and down)
        ASSERT_EQ(seen.size(), 2u);
        for (const bool b: seen) {
            EXPECT_TRUE(b);
        }
    }
    int turnedLines = 0;
    for (const LineRef& l: got.lines) {
        turnedLines += l.angle != 0 ? 1 : 0;
    }
    EXPECT_EQ(turnedLines, 3);
    // Each word's turned box holds the points of its stroke, and nothing of the others
    ASSERT_TRUE(got.text);
    ASSERT_EQ(got.text->words.size(), 2u * 4 + 3 + 3 + 4);
    for (const ink::Word& w: got.text->words) {
        int holding = 0;
        for (const InkStroke& s: page) {
            bool all = true;
            for (const QPointF& p: s.points) {
                all = all && inWord(w, p);
            }
            holding += all ? 1 : 0;
        }
        EXPECT_EQ(holding, 1) << w.text.toStdString() << " at " << w.angle;
    }
}

// The search marks a word at an angle with its turned box; the box around it covers the quad
TEST(InkRotationTest, aHitAtAnAngleIsMarkedTurned) {
    auto line = std::make_shared<ink::LineResult>();
    for (int i = 0; i < 2; ++i) {
        ink::Word w;
        w.box = QRectF(i * STEP, 0, 36, HEIGHT);
        w.conf = 0.9f;
        w.text = i == 0 ? QStringLiteral("margin") : QStringLiteral("note");
        w.candidates.push_back(ink::candidate(w.text, 1.0f));
        line->words.push_back(std::move(w));
    }
    // Written upwards from (40, 330)
    const auto page = ink::PageText::assemble({{QPointF(40, 330), line, -90}});
    ASSERT_EQ(page->words.size(), 2u);
    EXPECT_EQ(page->words[0].angle, -90);
    EXPECT_EQ(ink::boundsOf(page->words[0]), QRectF(QPointF(40, 294), QPointF(50, 330)));
    const auto hits = ink::find(*page, {{QStringLiteral("margin note"), textmatch::Anywhere}});
    ASSERT_EQ(hits.size(), 1u);
    EXPECT_TRUE(ink::atAnAngle(*page, hits[0]));
    const auto quads = ink::quadsOf(*page, hits[0]);
    ASSERT_EQ(quads.size(), 1u);
    // From the line's start upwards: 96 pt long, 10 high, 2 pt more around
    const QRectF bounds = quads[0].boundingRect();
    EXPECT_NEAR(bounds.left(), 38, 1e-9);
    EXPECT_NEAR(bounds.right(), 52, 1e-9);
    EXPECT_NEAR(bounds.top(), 330 - 96 - 2, 1e-9);
    EXPECT_NEAR(bounds.bottom(), 332, 1e-9);
    EXPECT_EQ(ink::rectsOf(*page, hits[0]), std::vector<QRectF>{bounds});
    // Horizontal handwriting: no quads to draw, the rects as before
    const auto level = ink::PageText::assemble({{QPointF(40, 330), line}});
    const auto plain = ink::find(*level, {{QStringLiteral("note"), textmatch::Anywhere}});
    ASSERT_EQ(plain.size(), 1u);
    EXPECT_FALSE(ink::atAnAngle(*level, plain[0]));
    EXPECT_EQ(ink::rectsOf(*level, plain[0]), std::vector<QRectF>{QRectF(100 - 2, 328, 40, 14)});
}

// Copy as text: a note at an angle is one line, a paragraph of its own, placed by where it starts on the page; a
// sweep along it takes its words
TEST(InkRotationTest, aNoteAtAnAngleIsCopiedAsALineOfItsOwn) {
    auto level = std::make_shared<ink::LineResult>();
    auto note = std::make_shared<ink::LineResult>();
    const char* body[] = {"one", "two", "three"};
    const char* margin[] = {"see", "page", "four"};
    for (int i = 0; i < 3; ++i) {
        for (auto [result, text]: {std::pair{level.get(), body[i]}, std::pair{note.get(), margin[i]}}) {
            ink::Word w;
            w.box = QRectF(i * STEP, 0, 36, HEIGHT);
            w.conf = 0.9f;
            w.text = QString::fromUtf8(text);
            w.candidates.push_back(ink::candidate(w.text, 1.0f));
            result->words.push_back(std::move(w));
        }
    }
    // Two lines of text at y 100 and 300; the note written upwards in the margin, from y 280 up to y 124
    const auto page = ink::PageText::assemble(
            {{QPointF(100, 100), level}, {QPointF(100, 300), level}, {QPointF(40, 280), note, -90}});
    const CopiedText t = inReadingOrder(allWords(*page));
    EXPECT_EQ(t.text, QStringLiteral("one two three\n\nsee page four\n\none two three"));
    EXPECT_EQ(t.words, 9);
    // A stroke along the note, beside the text: its words, in the order they were written
    const auto swept = sweptWords(*page, {{45, 285}, {45, 120}}, 2);
    EXPECT_EQ(inReadingOrder(swept).text, QStringLiteral("see page four"));
    // A tap on "page" (its middle: 40 + 5, 280 - 60 - 18)
    const auto tapped = sweptWords(*page, {{45, 202}}, 2);
    ASSERT_EQ(tapped.size(), 1u);
    EXPECT_EQ(tapped[0].text, QStringLiteral("page"));
    EXPECT_TRUE(sweptWords(*page, {{70, 202}}, 2).empty()) << "beside its turned box";
}

namespace {
class InkRotationPdfTest: public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(tmp.isValid());
        app = std::make_unique<AppContext>(fs::path(XQT_BUILD_RESOURCE_DIR),
                                           fs::path(tmp.filePath("settings.xml").toStdString()), 1);
        session = std::make_unique<DocumentSession>(*app);
    }
    QTemporaryDir tmp;
    std::unique_ptr<AppContext> app;
    std::unique_ptr<DocumentSession> session;
};
}  // namespace

// The PDF text layer turns a word at an angle with its text matrix: poppler finds it, and its glyphs lie over the ink
TEST_F(InkRotationPdfTest, theTextLayerIsTurnedWithTheInk) {
    for (const double angle: {-90.0, 90.0, 35.0}) {
        SCOPED_TRACE(angle);
        auto line = std::make_shared<ink::LineResult>();
        ink::Word w;
        w.box = QRectF(0, 0, 80, 14);
        w.conf = 0.9f;
        w.text = QStringLiteral("Kalman");
        w.candidates.push_back(ink::candidate(w.text, 1.0f));
        line->words.push_back(w);
        const auto page = ink::PageText::assemble({{QPointF(200, 400), line, angle}});
        const auto words = InkTextLayer::wordsOf(*page);
        ASSERT_EQ(words.size(), 1u);
        EXPECT_EQ(words[0].angle, angle);
        if (angle == -90) {
            // (written upwards: the text's x axis points up the PDF page)
            EXPECT_NE(InkTextLayer::contentOf(words, 842, "").find(" 0 1 -1 0 "), std::string::npos)
                    << InkTextLayer::contentOf(words, 842, "");
        }
        session->search().textIndex().setInk(0, page);
        const fs::path out = fs::path(tmp.filePath(QStringLiteral("turned%1.pdf").arg(angle)).toStdString());
        ASSERT_TRUE(session->saveAsHybrid(out).ok);
        const PdfPageLayout layout = PdfLayoutReader(out).layout(0);
        // (at a free angle poppler, as Okular and Evince show it, takes the letters one by one: they are found, the
        // word only by its letters with spaces; at 90 degrees it reads the word)
        const QString found = std::abs(angle) == 90 ? QStringLiteral("Kalman") : QStringLiteral("K a l m a n");
        const qsizetype at = layout.text.indexOf(found);
        ASSERT_GE(at, 0) << layout.text.toStdString();
        // The glyphs' boxes lie over the ink's turned box (poppler's boxes are upright around them)
        const QRectF ink = ink::boundsOf(page->words[0]);
        const auto rects = layout.rects(at, at + found.size());
        ASSERT_FALSE(rects.empty());
        QRectF glyphs;
        for (const QRectF& r: rects) {
            glyphs = glyphs.isNull() ? r : glyphs.united(r);
        }
        // (poppler's boxes of letters at a free angle are rough)
        const double near = std::abs(angle) == 90 ? 3 : 8;
        EXPECT_NEAR(glyphs.center().x(), ink.center().x(), near);
        EXPECT_NEAR(glyphs.center().y(), ink.center().y(), near);
        EXPECT_GT(glyphs.intersected(ink).width() * glyphs.intersected(ink).height(),
                  0.7 * ink.width() * ink.height() * (std::abs(angle) == 90 ? 1 : 0.5));
    }
}
