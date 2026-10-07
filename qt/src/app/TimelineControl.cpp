#include "TimelineControl.h"

#include <QDateTime>
#include <QLocale>
#include <QVariantMap>
#include <algorithm>
#include <cmath>
#include <shared_mutex>

#include "audio/AudioFiles.h"
#include "audio/OggVorbis.h"
#include "canvas/CanvasView.h"
#include "canvas/TimelineReplay.h"
#include "model/Document.h"
#include "session/DocumentSession.h"
#include "session/ElementTimes.h"
#include "session/Timeline.h"

#include "AudioControl.h"

namespace xqt {

namespace {
constexpr int FRAME_MS = 16;
/// The clock follows what is heard when they are this far apart (ms)
constexpr qint64 AUDIO_DRIFT = 250;
const qreal SPEEDS[] = {0.5, 1, 2, 4, 8};

QString clockText(qint64 ms) {
    const qint64 s = std::max<qint64>(0, ms / 1000);
    if (s >= 3600) {
        return QStringLiteral("%1:%2:%3")
                .arg(s / 3600)
                .arg((s / 60) % 60, 2, 10, QLatin1Char('0'))
                .arg(s % 60, 2, 10, QLatin1Char('0'));
    }
    return QStringLiteral("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QLatin1Char('0'));
}

QString dateText(qint64 when) {
    const QDateTime t = QDateTime::fromMSecsSinceEpoch(when);
    return QLocale().toString(t, QStringLiteral("ddd d MMM yyyy, HH:mm"));
}
}  // namespace

TimelineControl::TimelineControl(std::function<DocumentSession*()> s, std::function<CanvasView*()> c, AudioControl* a,
                                 QObject* parent):
        QObject(parent), currentSession(std::move(s)), currentCanvas(std::move(c)), audio(a) {
    clock.setInterval(FRAME_MS);
    clock.setTimerType(Qt::PreciseTimer);
    connect(&clock, &QTimer::timeout, this, &TimelineControl::tick);
}

TimelineControl::~TimelineControl() { stop(); }

qint64 TimelineControl::duration() const { return line ? line->duration() : 0; }

void TimelineControl::setSpeed(qreal speed) {
    speed = std::clamp<qreal>(speed, 0.25, 16);
    if (speed == rate) {
        return;
    }
    rate = speed;
    Q_EMIT speedChanged();
    if (playing()) {
        syncAudio();  // (heard at 1× only)
    }
}

void TimelineControl::cycleSpeed() {
    const auto it = std::find(std::begin(SPEEDS), std::end(SPEEDS), rate);
    setSpeed(it == std::end(SPEEDS) || std::next(it) == std::end(SPEEDS) ? SPEEDS[0] : *std::next(it));
}

QVariantList TimelineControl::marks() const {
    QVariantList out;
    if (!line) {
        return out;
    }
    for (const auto& m: line->marks()) {
        QVariantMap v;
        v["at"] = static_cast<qint64>(m.at);
        v["label"] = m.when == 0 ? tr("Without a time") : dateText(m.when);
        out.append(v);
    }
    return out;
}

QVariantList TimelineControl::tracks() const {
    QVariantList out;
    if (!line) {
        return out;
    }
    for (const auto& t: line->tracks()) {
        QVariantMap v;
        v["at"] = static_cast<qint64>(t.at);
        v["length"] = static_cast<qint64>(t.length);
        v["found"] = t.found;
        v["title"] = QString::fromStdU16String(fs::path(t.name).stem().u16string());
        out.append(v);
    }
    return out;
}

QString TimelineControl::timeText() const {
    if (!line) {
        return {};
    }
    if (const auto when = line->wallTime(at)) {
        return dateText(*when);
    }
    return line->events().empty() ? QString() : tr("Written before the times were kept");
}

QString TimelineControl::positionText() const { return clockText(at) + QStringLiteral(" / ") + clockText(duration()); }

QString TimelineControl::elapsedText() const { return line ? clockText(at) : QString(); }

QString TimelineControl::momentText() const {
    if (!line) {
        return {};
    }
    if (const auto when = line->wallTime(at)) {
        const QDateTime t = QDateTime::fromMSecsSinceEpoch(*when);
        const bool thisYear = t.date().year() == QDateTime::fromMSecsSinceEpoch(timeline::now()).date().year();
        return QLocale().toString(t, thisYear ? QStringLiteral("d MMM, HH:mm") : QStringLiteral("d MMM yyyy, HH:mm"));
    }
    return line->events().empty() ? QString() : tr("Before the times were kept");
}

int TimelineControl::shownCount() const {
    const TimelineReplay* r = view ? view->replay() : nullptr;
    return r ? static_cast<int>(r->frame().shown) : 0;
}

int TimelineControl::elementCount() const { return line ? static_cast<int>(line->events().size()) : 0; }

bool TimelineControl::canStart() const {
    DocumentSession* s = currentSession();
    if (!s || !currentCanvas() || s->textFile() || s->isReadOnly()) {
        return false;
    }
    return !(audio && audio->recordingHere());
}

bool TimelineControl::start() { return begin(0); }

bool TimelineControl::startAtRecording(const QString& name, qint64 ms) {
    if (!begin(0)) {
        return false;
    }
    for (const auto& t: line->tracks()) {
        if (QString::fromStdString(t.name) == name) {
            moveTo(t.at + std::clamp<qint64>(ms, 0, t.length), false);
            play();
            break;
        }
    }
    return true;
}

bool TimelineControl::begin(qint64 from) {
    if (active()) {
        stop();
    }
    if (!canStart()) {
        if (audio && audio->recordingHere()) {
            Q_EMIT message(tr("Stop the recording to replay the document."));
        }
        return false;
    }
    DocumentSession* s = currentSession();
    CanvasView* v = currentCanvas();
    if (audio) {
        audio->stopPlayback();  // (the replay plays what it needs)
    }
    v->endTextEditing();  // (a text being typed is finished first: it is part of the document then)
    const fs::path documentFile = s->getFilePath();
    auto built = std::make_shared<timeline::Timeline>();
    {
        Document* doc = s->getDocument();
        std::shared_lock lock(*doc);
        *built = timeline::Timeline::build(*doc, [&documentFile](const std::string& name) -> std::optional<int64_t> {
            const fs::path file = audio::find(name, documentFile);
            if (file.empty()) {
                return std::nullopt;
            }
            const int64_t ms = audio::durationMsOf(file);
            return ms > 0 ? std::optional<int64_t>(ms) : std::nullopt;
        });
    }
    if (built->empty()) {
        Q_EMIT message(tr("Nothing to replay: the document is empty."));
        return false;
    }
    line = built;
    session = s;
    view = v;
    at = std::clamp<qint64>(from, 0, line->duration());
    s->setReplaying(true);
    v->startReplay(line, at);
    // Should the document change all the same, the replay ends (its elements are what it shows)
    const auto changed = [this] {
        if (active()) {
            stop();
            Q_EMIT message(tr("The document changed: the replay ended."));
        }
    };
    watching.push_back(connect(s, &DocumentSession::pageRevisionsChanged, this, changed));
    watching.push_back(connect(s, &DocumentSession::undoRedoStateChanged, this, changed));
    watching.push_back(connect(s, &QObject::destroyed, this, [this] { stop(); }));
    watching.push_back(connect(v, &QObject::destroyed, this, [this] { stop(); }));
    watching.push_back(connect(v, &CanvasView::replayTapped, this, [this](qint64 t) {
        // A tap on ink: from the moment it was written (with the lead-in of the audio's setting where it is heard)
        const qint64 leadIn = audio && line->heardAt(t) ? audio->leadInMs() : 0;
        moveTo(std::max<qint64>(0, t - leadIn), false);
        if (view) {
            view->settleReplay();
        }
    }));
    Q_EMIT activeChanged();
    Q_EMIT positionChanged();
    return true;
}

void TimelineControl::stop() {
    if (!line) {
        return;
    }
    const bool wasPlaying = playing();
    clock.stop();
    stopAudio();
    for (auto& c: watching) {
        disconnect(c);
    }
    watching.clear();
    if (view) {
        view->endReplay();
    }
    if (session) {
        session->setReplaying(false);
    }
    line.reset();
    view = nullptr;
    session = nullptr;
    at = 0;
    scrubbing = false;
    if (wasPlaying) {
        Q_EMIT playingChanged();
    }
    Q_EMIT activeChanged();
    Q_EMIT positionChanged();
}

void TimelineControl::play() {
    if (!line || playing()) {
        return;
    }
    if (at >= line->duration()) {
        moveTo(0, false);  // (at the end: from the start again)
    }
    sinceTick.start();
    clock.start();
    syncAudio();
    Q_EMIT playingChanged();
}

void TimelineControl::pause() {
    if (!playing()) {
        return;
    }
    clock.stop();
    stopAudio();
    if (view) {
        view->settleReplay();
    }
    Q_EMIT playingChanged();
}

void TimelineControl::toggle() { playing() ? pause() : play(); }

void TimelineControl::seek(qint64 ms) {
    moveTo(ms, false);
    if (!scrubbing && !playing() && view) {
        view->settleReplay();
    }
}

void TimelineControl::skip(qint64 deltaMs) { seek(at + deltaMs); }

void TimelineControl::setScrubbing(bool on) {
    scrubbing = on;
    if (!on && view) {
        view->settleReplay();
    }
}

void TimelineControl::nextMark() {
    if (!line) {
        return;
    }
    for (const auto& m: line->marks()) {
        if (m.at > at) {
            seek(m.at);
            return;
        }
    }
    seek(line->duration());
}

void TimelineControl::previousMark() {
    if (!line) {
        return;
    }
    qint64 target = 0;
    for (const auto& m: line->marks()) {
        if (m.at < at - 1500) {  // (just after a mark: the one before it)
            target = m.at;
        }
    }
    seek(target);
}

void TimelineControl::currentChanged() {
    if (active() && (session != currentSession() || view != currentCanvas())) {
        stop();
    }
}

void TimelineControl::moveTo(qint64 ms, bool fromClock) {
    if (!line) {
        return;
    }
    const qint64 to = std::clamp<qint64>(ms, 0, line->duration());
    if (to == at && fromClock) {
        return;
    }
    at = to;
    if (view) {
        view->seekReplay(at);
    }
    if (!fromClock) {
        silent.clear();
        if (playing()) {
            syncAudio();  // (a jump: what is heard jumps too)
        }
    }
    Q_EMIT positionChanged();
}

void TimelineControl::tick() {
    if (!line) {
        clock.stop();
        return;
    }
    const qint64 elapsed = sinceTick.restart();
    qint64 next = at + static_cast<qint64>(std::llround(static_cast<double>(elapsed) * rate));
    // Where a recording is heard, the clock follows it
    if (heard && audio && audio->playing() && !audio->playPaused()) {
        if (const auto h = line->heardAt(at)) {
            const qint64 heardAt = line->tracks()[h->track].at + audio->playPositionMs();
            if (std::abs(heardAt - next) > AUDIO_DRIFT) {
                next = heardAt;
            }
        }
    }
    moveTo(next, true);
    if (at >= line->duration()) {
        pause();  // (the end)
        return;
    }
    syncAudio();
}

void TimelineControl::syncAudio() {
    if (!line || !audio) {
        return;
    }
    const auto h = playing() && rate == 1 ? line->heardAt(at) : std::nullopt;
    if (!h) {
        stopAudio();
        return;
    }
    const auto& track = line->tracks()[h->track];
    const QString name = QString::fromStdString(track.name);
    if (name == silent) {
        return;
    }
    if (heard && audio->playName() == name) {
        if (!audio->playing()) {
            silent = name;  // (it ended: its ink may go on a little longer)
            stopAudio();
            return;
        }
        if (!audio->playPaused() && std::abs(audio->playPositionMs() - h->position) > AUDIO_DRIFT) {
            audio->seek(h->position);
        }
        if (!audio->playPaused()) {
            return;
        }
    }
    if (audio->play(name, h->position)) {
        if (!heard) {
            heard = true;
            Q_EMIT hearingChanged();
        }
    } else {
        silent = name;  // (not again and again: the message said why)
        stopAudio();
    }
}

void TimelineControl::stopAudio() {
    if (!heard) {
        return;
    }
    heard = false;
    if (audio) {
        audio->stopPlayback();
    }
    Q_EMIT hearingChanged();
}

}  // namespace xqt
