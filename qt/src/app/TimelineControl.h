/*
 * xournal-qt: the replay of a document's timeline in the window (qt/docs/timeline.md, "Replay"), the QML object
 * `app.timeline`; its play bar is TimelineBar.qml.
 *
 * Read-only: while it replays, the document's view is for reading (every tool scrolls, a tap on ink goes to the moment
 * it was written), the session refuses changes (DocumentSession::setReplaying: no undo, no paste, ...). Leaving brings
 * the whole document back as it is; it was never changed. Should the document change all the same (a page deleted from
 * the sidebar), the replay ends.
 *
 * The clock: a timer moves the bar's time on (at the chosen speed). Where a recording is on the bar, it is heard
 * (through app.audio) at 1×, and the clock follows what is heard; at other speeds it is silent.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <functional>
#include <memory>

namespace xqt {
class AudioControl;
class CanvasView;
class DocumentSession;
namespace timeline {
class Timeline;
}

class TimelineControl: public QObject {
    Q_OBJECT
    /// Replaying the current document
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)
    /// The bar's time (ms) and its length
    Q_PROPERTY(qint64 position READ position NOTIFY positionChanged)
    Q_PROPERTY(qint64 duration READ duration NOTIFY activeChanged)
    /// 0.5, 1, 2, 4 or 8 (recordings are heard at 1× only)
    Q_PROPERTY(qreal speed READ speed WRITE setSpeed NOTIFY speedChanged)
    /// The starts of the sessions: [{ at, label }] (the elements without a time: label "Without a time")
    Q_PROPERTY(QVariantList marks READ marks NOTIFY activeChanged)
    /// The recordings on the bar: [{ at, length, found, title }]
    Q_PROPERTY(QVariantList tracks READ tracks NOTIFY activeChanged)
    /// The clock time at the bar's time ("Sun 4 Oct 2026, 14:03"; "Written before the times were kept" in the prelude)
    Q_PROPERTY(QString timeText READ timeText NOTIFY positionChanged)
    /// "1:23 / 4:56"
    Q_PROPERTY(QString positionText READ positionText NOTIFY positionChanged)
    /// The play bar's short forms (qt/replay-polish): the bar's time "12:04" and the clock time of the moment
    /// "3 Oct, 14:20" (with the year when it is not this one; "Before the times were kept" in the prelude)
    Q_PROPERTY(QString elapsedText READ elapsedText NOTIFY positionChanged)
    Q_PROPERTY(QString momentText READ momentText NOTIFY positionChanged)
    /// A recording is heard now
    Q_PROPERTY(bool hearing READ hearing NOTIFY hearingChanged)
    /// How many elements are shown now, of all
    Q_PROPERTY(int shownCount READ shownCount NOTIFY positionChanged)
    Q_PROPERTY(int elementCount READ elementCount NOTIFY activeChanged)

public:
    TimelineControl(std::function<DocumentSession*()> session, std::function<CanvasView*()> canvas, AudioControl* audio,
                    QObject* parent = nullptr);
    ~TimelineControl() override;

    bool active() const { return line != nullptr; }
    bool playing() const { return clock.isActive(); }
    qint64 position() const { return at; }
    qint64 duration() const;
    qreal speed() const { return rate; }
    void setSpeed(qreal speed);
    QVariantList marks() const;
    QVariantList tracks() const;
    QString timeText() const;
    QString positionText() const;
    QString elapsedText() const;
    QString momentText() const;
    bool hearing() const { return heard; }
    int shownCount() const;
    int elementCount() const;

    /// The current document can be replayed (a document of notes, not a text file; not while it is recorded)
    bool canStart() const;
    /// Replays the current document from the start (paused). False: nothing to replay (message())
    Q_INVOKABLE bool start();
    /// ... from the moment `ms` of its recording `name` (the playback pill: where it is heard now)
    Q_INVOKABLE bool startAtRecording(const QString& name, qint64 ms);
    /// The whole document again, as it is
    Q_INVOKABLE void stop();
    Q_INVOKABLE void play();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void toggle();
    Q_INVOKABLE void seek(qint64 ms);
    Q_INVOKABLE void skip(qint64 deltaMs);
    /// The slider is being dragged (true) or let go (false): the pages' pictures catch up when it is let go
    Q_INVOKABLE void setScrubbing(bool on);
    /// To the start of the next or the previous session
    Q_INVOKABLE void nextMark();
    Q_INVOKABLE void previousMark();
    /// The next speed (0.5 → 1 → 2 → 4 → 8 → 0.5)
    Q_INVOKABLE void cycleSpeed();

    /// The current tab changed: a replay of another document ends
    void currentChanged();

Q_SIGNALS:
    void activeChanged();
    void playingChanged();
    void positionChanged();
    void speedChanged();
    void hearingChanged();
    void message(const QString& text);

private:
    bool begin(qint64 from);
    void tick();
    void moveTo(qint64 ms, bool fromClock);
    void syncAudio();
    void stopAudio();

    std::function<DocumentSession*()> currentSession;
    std::function<CanvasView*()> currentCanvas;
    QPointer<AudioControl> audio;
    std::shared_ptr<const timeline::Timeline> line;
    QPointer<DocumentSession> session;
    QPointer<CanvasView> view;
    std::vector<QMetaObject::Connection> watching;
    QTimer clock;
    QElapsedTimer sinceTick;
    qint64 at = 0;
    qreal rate = 1;
    bool heard = false;  ///< a recording is heard (we started it)
    QString silent;      ///< a recording that ended or could not be played: not again until a jump
    bool scrubbing = false;
};

}  // namespace xqt
