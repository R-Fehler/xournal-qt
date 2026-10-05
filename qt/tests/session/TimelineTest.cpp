/*
 * xournal-qt: the timeline of a document (qt/docs/timeline.md, "The timeline"): elements in the order they were made,
 * those without a time first, long pauses shortened and sessions marked, recordings placed by their start (their ink's
 * times, else their name), what is shown and heard at a moment.
 *
 * @license GNU GPLv2 or later
 */
#include <QDate>
#include <QDateTime>
#include <QTime>
#include <memory>
#include <optional>
#include <string>

#include <gtest/gtest.h>

#include "model/Document.h"
#include "model/DocumentHandler.h"
#include "model/Font.h"
#include "model/Layer.h"
#include "model/Point.h"
#include "model/Stroke.h"
#include "model/Text.h"
#include "model/XojPage.h"
#include "session/Timeline.h"

using namespace xqt;
using namespace xqt::timeline;

namespace {

constexpr int64_t T0 = 1791100800000;  // 2026-10-04 08:00:00 UTC
constexpr int64_t MIN = 60 * 1000;

DocumentHandler& handler() {
    static DocumentHandler h;
    return h;
}

/// A stroke `length` points long, made at `created`
Stroke* addStroke(Layer* layer, int64_t created, double length = 80, double y = 100) {
    auto s = std::make_unique<Stroke>();
    s->setToolType(StrokeTool::PEN);
    s->setWidth(2);
    for (int i = 0; i <= 8; ++i) {
        s->addPoint(Point(50 + length * i / 8, y));
    }
    s->setCreated(created);
    Stroke* raw = s.get();
    layer->addElement(std::move(s));
    return raw;
}

Text* addText(Layer* layer, int64_t created) {
    auto t = std::make_unique<Text>();
    t->setText("note");
    t->setFont(XojFont("Sans", 12));
    t->move(50, 200);
    t->setCreated(created);
    Text* raw = t.get();
    layer->addElement(std::move(t));
    return raw;
}

struct Doc {
    std::unique_ptr<Document> doc = std::make_unique<Document>(&handler());
    Layer* layer(size_t page) const { return doc->getPage(page)->getSelectedLayer(); }
    explicit Doc(size_t pages = 1) {
        for (size_t i = 0; i < pages; ++i) {
            doc->addPage(std::make_shared<XojPage>(400, 300));
        }
    }
};

Timeline build(const Doc& d, Timeline::LengthOf lengths = {}) { return Timeline::build(*d.doc, lengths); }

std::vector<const Element*> order(const Timeline& t) {
    std::vector<const Element*> out;
    for (const Event& e: t.events()) {
        out.push_back(e.element);
    }
    return out;
}

/// The name Xournal++ gives a recording started at this local time
std::string nameAt(const QDateTime& local) { return local.toString("yyyy-MM-dd_HH-mm-ss").toStdString() + ".ogg"; }

}  // namespace

// Elements come in the order they were made, wherever they are; those without a time first, in the document's order
TEST(TimelineTest, elementsInTheOrderTheyWereMadeThoseWithoutATimeFirst) {
    Doc d(2);
    Stroke* late = addStroke(d.layer(0), T0 + 5000);
    Stroke* old1 = addStroke(d.layer(0), 0);
    Stroke* early = addStroke(d.layer(1), T0);
    Text* middle = addText(d.layer(0), T0 + 2000);
    Stroke* old2 = addStroke(d.layer(1), 0);
    const Timeline t = build(d);
    EXPECT_EQ(order(t), (std::vector<const Element*>{old1, old2, early, middle, late}));
    EXPECT_EQ(t.events()[0].when, 0);
    EXPECT_EQ(t.events()[2].when, T0);
    EXPECT_EQ(t.events()[2].page, 1u);
    // The prelude: a moment each, then a pause, then the clock: 2 s and 3 s as they were
    EXPECT_EQ(t.events()[0].at, 0);
    EXPECT_EQ(t.events()[1].at, PRELUDE_STEP);
    const int64_t start = 2 * PRELUDE_STEP + PAUSE_ON_BAR;
    EXPECT_EQ(t.events()[2].at, start);
    EXPECT_EQ(t.events()[3].at, start + 2000);
    EXPECT_EQ(t.events()[4].at, start + 5000);
    ASSERT_EQ(t.marks().size(), 2u) << "the prelude and the first session";
    EXPECT_EQ(t.marks()[0].when, 0);
    EXPECT_EQ(t.marks()[1].at, start);
    EXPECT_EQ(t.marks()[1].when, T0);
    EXPECT_EQ(t.indexOf(middle), 3u);
    EXPECT_FALSE(t.wallTime(10).has_value()) << "the prelude has no time";
    EXPECT_EQ(t.wallTime(start + 2500), T0 + 2500);
}

// A long pause is shortened; one longer than SESSION_GAP starts a session (a mark)
TEST(TimelineTest, longPausesAreShortenedAndSessionsMarked) {
    Doc d;
    addStroke(d.layer(0), T0);
    addStroke(d.layer(0), T0 + 5000);            // (5 s: as it was)
    addStroke(d.layer(0), T0 + 5000 + 3 * MIN);  // (3 min: shortened)
    Stroke* nextDay = addStroke(d.layer(0), T0 + 24 * 60 * MIN);
    const Timeline t = build(d);
    ASSERT_EQ(t.events().size(), 4u);
    EXPECT_EQ(t.events()[0].at, 0);
    EXPECT_EQ(t.events()[1].at, 5000);
    EXPECT_EQ(t.events()[2].at, 5000 + PAUSE_ON_BAR);
    EXPECT_EQ(t.events()[3].at, 5000 + 2 * PAUSE_ON_BAR);
    ASSERT_EQ(t.marks().size(), 2u);
    EXPECT_EQ(t.marks()[0].at, 0);
    EXPECT_EQ(t.marks()[1].at, 5000 + 2 * PAUSE_ON_BAR);
    EXPECT_EQ(t.marks()[1].when, nextDay->getCreated());
    EXPECT_EQ(t.wallTime(5000 + 2 * PAUSE_ON_BAR + 100), T0 + 24 * 60 * MIN + 100);
    EXPECT_EQ(t.barOf(T0 + 5000 + 3 * MIN), 5000 + PAUSE_ON_BAR);
    EXPECT_EQ(t.barOf(T0 + 6 * MIN), 5000 + 2 * PAUSE_ON_BAR) << "in a shortened pause: where it ends at most";
    EXPECT_GT(t.duration(), t.events().back().at);
}

// A stroke is drawn on over a while (its length at handwriting speed, until the next element at most); a text appears
// at once
TEST(TimelineTest, aStrokeIsDrawnOnOverAWhile) {
    Doc d;
    Stroke* a = addStroke(d.layer(0), T0, 160);          // 1 s at handwriting speed
    Stroke* b = addStroke(d.layer(0), T0 + 4000, 1600);  // 10 s, but the text comes after 2 s
    addText(d.layer(0), T0 + 6000);
    const Timeline t = build(d);
    EXPECT_EQ(t.events()[0].length, 1000);
    EXPECT_EQ(t.events()[1].length, 2000);
    EXPECT_EQ(t.events()[2].length, 0);
    EXPECT_EQ(t.frameAt(-1), (Frame{0, std::nullopt, 0}));
    EXPECT_EQ(t.frameAt(0), (Frame{0, 0u, 0}));
    EXPECT_EQ(t.frameAt(250), (Frame{0, 0u, 0.25}));
    EXPECT_EQ(t.frameAt(1000), (Frame{1, std::nullopt, 0})) << a;
    EXPECT_EQ(t.frameAt(3999), (Frame{1, std::nullopt, 0}));
    EXPECT_EQ(t.frameAt(5000), (Frame{1, 1u, 0.5})) << b;
    EXPECT_EQ(t.frameAt(6000), (Frame{3, std::nullopt, 0}));
    EXPECT_EQ(t.frameAt(t.duration()), (Frame{3, std::nullopt, 0}));
}

// A recording made with xournal-qt: placed where its ink says (made at xqt-created, ts into it), so the ink comes when
// it is heard, also after the recorder was paused (the pause is not in the file)
TEST(TimelineTest, aRecordingIsPlacedByItsInk) {
    Doc d;
    const std::string name = "2026-10-04_10-00-00.ogg";  // (its name's time is not needed)
    Stroke* first = addStroke(d.layer(0), T0 + 3000);
    first->setAudioFilename(name);
    first->setTimestamp(3000);  // started at T0
    Stroke* afterPause = addStroke(d.layer(0), T0 + 2 * MIN + 10000);
    afterPause->setAudioFilename(name);
    afterPause->setTimestamp(10000);                     // paused for 2 min before
    Stroke* without = addStroke(d.layer(0), T0 + 5000);  // (the highlighter: no recording)
    const Timeline t = build(d, [](const std::string& n) -> std::optional<int64_t> {
        return n == "2026-10-04_10-00-00.ogg" ? std::optional<int64_t>(20000) : std::nullopt;
    });
    ASSERT_EQ(t.tracks().size(), 1u);
    const Track& track = t.tracks()[0];
    EXPECT_EQ(track.name, name);
    EXPECT_EQ(track.start, T0);
    EXPECT_EQ(track.length, 20000);
    EXPECT_TRUE(track.found);
    EXPECT_EQ(track.at, 0);
    EXPECT_EQ(order(t), (std::vector<const Element*>{first, without, afterPause}));
    EXPECT_EQ(t.events()[0].at, 3000);
    EXPECT_EQ(t.events()[2].at, 10000) << "the ink of the recording goes with what is heard";
    const auto heard = t.heardAt(10000);
    ASSERT_TRUE(heard);
    EXPECT_EQ(heard->track, 0u);
    EXPECT_EQ(heard->position, 10000);
    EXPECT_FALSE(t.heardAt(20000).has_value()) << "it ended";
    EXPECT_EQ(t.duration(), 20000 + 500);
}

// A file of Xournal++ with a recording (no xqt-created): placed by the time in the recording's name (local time)
TEST(TimelineTest, aXournalppRecordingIsPlacedByItsName) {
    Doc d;
    const QDateTime started(QDate(2026, 10, 4), QTime(14, 3, 22), Qt::LocalTime);
    const std::string name = nameAt(started);
    Stroke* a = addStroke(d.layer(0), 0);
    a->setAudioFilename(name);
    a->setTimestamp(4000);
    Stroke* b = addStroke(d.layer(0), 0);
    b->setAudioFilename(name);
    b->setTimestamp(1000);
    Stroke* old = addStroke(d.layer(0), 0);
    d.doc->getPage(0)->setAudioMemos("2026-10-05_09-00-00.ogg");  // (a voice memo without ink, its file is nowhere)
    const Timeline t = build(d);
    EXPECT_EQ(order(t), (std::vector<const Element*>{old, b, a}));
    EXPECT_EQ(t.events()[1].when, started.toMSecsSinceEpoch() + 1000);
    ASSERT_EQ(t.tracks().size(), 1u) << "a memo whose file is nowhere has no length: not on the bar";
    EXPECT_FALSE(t.tracks()[0].found);
    EXPECT_EQ(t.tracks()[0].length, 5000) << "the span of its ink";
    EXPECT_FALSE(t.heardAt(t.events()[2].at).has_value()) << "nothing to hear";
    // A recording whose start nobody knows: its ink has no time
    Doc e;
    Stroke* c = addStroke(e.layer(0), 0);
    c->setAudioFilename("lecture.ogg");
    c->setTimestamp(4000);
    const Timeline u = build(e);
    ASSERT_EQ(u.events().size(), 1u);
    EXPECT_EQ(u.events()[0].when, 0);
    EXPECT_TRUE(u.tracks().empty());
}

// The time in a recording's name (local time), also with "-2" or a folder; other names have none
TEST(TimelineTest, theTimeInARecordingsName) {
    const QDateTime t(QDate(2026, 10, 4), QTime(14, 3, 22), Qt::LocalTime);
    EXPECT_EQ(startFromName("2026-10-04_14-03-22.ogg"), t.toMSecsSinceEpoch());
    EXPECT_EQ(startFromName("2026-10-04_14-03-22-2.ogg"), t.toMSecsSinceEpoch());
    EXPECT_EQ(startFromName("/home/me/notes.audio/2026-10-04_14-03-22.ogg"), t.toMSecsSinceEpoch());
    EXPECT_FALSE(startFromName("lecture.ogg"));
    EXPECT_FALSE(startFromName("2026-13-04_14-03-22.ogg"));
}

// Elements of hidden layers are not on it; an empty document has an empty timeline
TEST(TimelineTest, hiddenLayersAndEmptyDocuments) {
    Doc d(2);
    EXPECT_TRUE(build(d).empty());
    EXPECT_EQ(build(d).duration(), 0);
    EXPECT_EQ(build(d).frameAt(100), (Frame{0, std::nullopt, 0}));
    addStroke(d.layer(0), T0);
    addStroke(d.layer(1), T0 + 100);
    d.layer(1)->setVisible(false);
    const Timeline t = build(d);
    EXPECT_EQ(t.events().size(), 1u);
}

// Many elements without a time: the prelude stays short
TEST(TimelineTest, aLongPreludeStaysShort) {
    Doc d;
    for (int i = 0; i < 1000; ++i) {
        addStroke(d.layer(0), 0, 10, i * 0.2);
    }
    const Timeline t = build(d);
    EXPECT_LE(t.events().back().at, PRELUDE_MAX);
    EXPECT_EQ(t.frameAt(t.duration()).shown, 1000u);
}
