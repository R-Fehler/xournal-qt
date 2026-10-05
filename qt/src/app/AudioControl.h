/*
 * xournal-qt: recording and playing in the window (qt/docs/audio.md, "In the app"), the QML object `app.audio`.
 *
 * One recording at a time, for the document of the tab it was started in: that tab's pen strokes and new texts are
 * tied to it (DocumentSession::setRecording), and it is a voice memo of the page that was shown when it started. It
 * ends with stop, when its tab closes, or when the device fails. Recordings go into the app's audio folder
 * (audio/AudioFiles.h) under upstream's names.
 *
 * Playing: a recording of the current document from a moment (the play tool on ink, a recording in the list), with a
 * lead-in (a setting, 2 s by default) so the words before the ink are heard too.
 *
 * Self-contained so the tool bar's owner can place its button anywhere (RecordButton.qml, RecordingPill.qml,
 * PlaybackPill.qml).
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <functional>
#include <memory>

#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantList>

namespace xqt {
class DocumentSession;
namespace audio {
class Recorder;
class Player;
}  // namespace audio

class AudioControl: public QObject {
    Q_OBJECT
    /// Recording is offered (a build with Qt Multimedia, or XQT_FAKE_AUDIO=1)
    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(bool recording READ recording NOTIFY recordingChanged)
    Q_PROPERTY(bool recordingPaused READ recordingPaused NOTIFY recordingChanged)
    /// The recording is for the document of the current tab
    Q_PROPERTY(bool recordingHere READ recordingHere NOTIFY recordingChanged)
    /// The title of the document the recording is for
    Q_PROPERTY(QString recordingTitle READ recordingTitle NOTIFY recordingChanged)
    Q_PROPERTY(qint64 recordedMs READ recordedMs NOTIFY recordedMsChanged)
    Q_PROPERTY(qreal level READ level NOTIFY levelChanged)
    /// The first seconds were silent (a muted or wrong microphone)
    Q_PROPERTY(bool silent READ silent NOTIFY levelChanged)

    Q_PROPERTY(bool playing READ playing NOTIFY playbackChanged)
    Q_PROPERTY(bool playPaused READ playPaused NOTIFY playbackChanged)
    Q_PROPERTY(QString playName READ playName NOTIFY playbackChanged)
    Q_PROPERTY(qint64 playDurationMs READ playDurationMs NOTIFY playbackChanged)
    Q_PROPERTY(qint64 playPositionMs READ playPositionMs NOTIFY playPositionChanged)
    /// The moments of the playing recording that have ink (ms), for ticks on the slider
    Q_PROPERTY(QVariantList playTicks READ playTicks NOTIFY playbackChanged)
    /// Played from the ink: this much earlier (ms; a setting)
    Q_PROPERTY(int leadInMs READ leadInMs WRITE setLeadInMs NOTIFY leadInChanged)

public:
    /// `current`: the document of the current tab (nullptr: none); `page`: its current page; `title`: its title.
    AudioControl(std::function<DocumentSession*()> current, std::function<QString(DocumentSession*)> title,
                 QObject* parent = nullptr);
    ~AudioControl() override;

    bool available() const;
    bool recording() const;
    bool recordingPaused() const;
    bool recordingHere() const;
    QString recordingTitle() const;
    qint64 recordedMs() const;
    qreal level() const;
    bool silent() const;
    bool playing() const;
    bool playPaused() const;
    QString playName() const { return played; }
    qint64 playDurationMs() const;
    qint64 playPositionMs() const;
    QVariantList playTicks() const { return ticks; }
    int leadInMs() const { return leadIn; }
    void setLeadInMs(int ms);

    /// Starts recording for the current document (a voice memo of its current page). False: see message().
    Q_INVOKABLE bool startRecording();
    Q_INVOKABLE void stopRecording();
    Q_INVOKABLE void pauseRecording();
    Q_INVOKABLE void resumeRecording();
    /// Start, or stop the one that runs
    Q_INVOKABLE void toggleRecording();

    /// Plays the recording `name` of the current document from `fromMs`.
    Q_INVOKABLE bool play(const QString& name, qint64 fromMs = 0);
    /// Plays what ink written at `ts` in `name` was written with: from the lead-in before it.
    Q_INVOKABLE bool playMoment(const QString& name, qint64 ts);
    Q_INVOKABLE void pausePlayback();
    Q_INVOKABLE void resumePlayback();
    Q_INVOKABLE void stopPlayback();
    Q_INVOKABLE void seek(qint64 ms);
    /// Forwards or back (ms)
    Q_INVOKABLE void skip(qint64 deltaMs);

    /// The recordings of the current document: [{ name, title, pages, elements, durationMs, found }]
    Q_INVOKABLE QVariantList recordings() const;
    /// Removes a recording from the current document (its ink's and memos' ties; undoable; the file stays)
    Q_INVOKABLE bool removeRecording(const QString& name);

    /// The current tab changed (recordingHere).
    void currentChanged();

Q_SIGNALS:
    void recordingChanged();
    void recordedMsChanged();
    void levelChanged();
    void playbackChanged();
    void playPositionChanged();
    void leadInChanged();
    /// Something to tell the user (a failure, where a recording went)
    void message(const QString& text);

private:
    void endRecording();
    std::function<DocumentSession*()> current;
    std::function<QString(DocumentSession*)> titleOf;
    std::unique_ptr<audio::Recorder> recorder;
    std::unique_ptr<audio::Player> player;
    QPointer<DocumentSession> recordingFor;
    QPointer<DocumentSession> playingFor;
    QString played;
    QVariantList ticks;
    int leadIn = 2000;
};

}  // namespace xqt
