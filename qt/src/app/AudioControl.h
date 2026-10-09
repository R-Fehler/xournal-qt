/*
 * xournal-qt: recording and playing in the window (qt/docs/features/audio.md, "In the app"), the QML object
 * `app.audio`.
 *
 * One recording at a time, for the document of the tab it was started in: that tab's pen strokes and new texts are
 * tied to it (DocumentSession::setRecording), and it is a voice memo of the page that was shown when it started. It
 * ends with stop, when its tab closes, or when the device fails. Recordings go into the document's sidecar
 * ("name.audio" next to its .xopp), or the app's audio folder while it is not saved (audio/AudioFiles.h), under
 * upstream's names.
 *
 * Playing: a recording of the current document from a moment (the play tool on ink, a recording in the list), with a
 * lead-in (a setting, 2 s by default) so the words before the ink are heard too.
 *
 * Self-contained so the tool bar's owner can place its button anywhere (RecordButton.qml, RecordingPill.qml,
 * PlaybackPill.qml).
 *
 * The platforms (qt/docs/features/audio.md, "Platforms"): the microphone permission is asked before the first recording
 * (Qt's QMicrophonePermission: macOS and Android ask the user; Linux and Windows grant it), and refused it opens
 * MicrophoneDialog.qml with a way to the system's settings. A running recording is reported to the platform's hook
 * (Android: the foreground service and its notification), whose Pause, Resume and Stop come back through
 * platformCommand().
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

#include "WindowContext.h"
#include "filesystem.h"

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

    /// The system refused the microphone when a recording was to start (MicrophoneDialog.qml shows it until closed)
    Q_PROPERTY(bool microphoneDenied READ microphoneDenied NOTIFY microphoneDeniedChanged)
    /// Where the microphone is allowed on this system, e.g. "System Settings → Privacy & Security → Microphone"
    Q_PROPERTY(QString microphoneSettingsPath READ microphoneSettingsPath CONSTANT)
    /// openMicrophoneSettings() can show that page (macOS, Windows, Android; read again whenever microphoneDenied
    /// changes, since main.cpp sets Android's opener after this object exists)
    Q_PROPERTY(bool canOpenMicrophoneSettings READ canOpenMicrophoneSettings NOTIFY microphoneDeniedChanged)

public:
    /// The microphone permission as the system reports it (Qt's Qt::PermissionStatus, also where Qt has none)
    enum class Permission { Granted, Denied, Undetermined };
    /// How the microphone permission is checked and asked for. The default: Qt's QMicrophonePermission for the
    /// Qt Multimedia backend (granted for the fakes, and where Qt has no permission API); XQT_FAKE_MIC_PERMISSION=
    /// denied | ask-deny | ask-grant pretends one (to try the refused path on Linux).
    struct PermissionAccess {
        std::function<Permission()> check;
        /// Shows the system's question; `answer(granted)` later, on the UI thread
        std::function<void(QObject* context, std::function<void(bool granted)> answer)> request;
    };
    /// Tests: another way of checking and asking (an empty one: the default again).
    static void setPermissionAccess(PermissionAccess access);
    /// Opens the system's page where the app may be allowed the microphone; false if there is none. The default:
    /// macOS's and Windows's privacy pages by URL; Android's is set by main.cpp (the app's page, by JNI).
    static void setSettingsOpener(std::function<bool()> open);

    /// A running recording as the platform shows it (Android: the foreground service's notification)
    struct PlatformState {
        bool recording = false;
        bool paused = false;
        qint64 recordedMs = 0;  ///< at this moment (the notification's clock runs on from it while not paused)
        QString title;          ///< the document's
        bool operator==(const PlatformState&) const = default;
    };
    /// What the platform's controls ask for (the notification's buttons)
    enum class PlatformCommand { Pause = 1, Resume = 2, Stop = 3 };

    /// One per window: it records and plays for the window's current document.
    explicit AudioControl(const WindowContext& window, QObject* parent = nullptr);
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
    bool microphoneDenied() const { return denied; }
    QString microphoneSettingsPath() const;
    bool canOpenMicrophoneSettings() const;

    /// Starts recording for the current document (a voice memo of its current page). False: see message().
    Q_INVOKABLE bool startRecording();
    Q_INVOKABLE void stopRecording();
    Q_INVOKABLE void pauseRecording();
    Q_INVOKABLE void resumeRecording();
    /// Start, or stop the one that runs
    Q_INVOKABLE void toggleRecording();
    /// The system's page for the microphone permission (and the notice closes)
    Q_INVOKABLE bool openMicrophoneSettings();
    Q_INVOKABLE void dismissMicrophoneNotice();

    /// Plays the recording `name` of the current document from `fromMs`.
    Q_INVOKABLE bool play(const QString& name, qint64 fromMs = 0);
    /// Plays what ink written at `ts` in `name` was written with: from the lead-in before it.
    bool playMoment(const QString& name, qint64 ts);
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

    /// The platform's part of a running recording (Android: the foreground service that keeps the microphone in the
    /// background, with its notification; main.cpp sets it). Called when a recording starts, pauses, resumes and
    /// ends.
    static void setPlatformHook(std::function<void(const PlatformState&)> hook);
    /// The platform's controls of the recording (Android: the notification's Pause, Resume and Stop).
    void platformCommand(PlatformCommand command);

Q_SIGNALS:
    void recordingChanged();
    void recordedMsChanged();
    void levelChanged();
    void playbackChanged();
    void playPositionChanged();
    void leadInChanged();
    void microphoneDeniedChanged();
    /// Something to tell the user (a failure, where a recording went)
    void message(const QString& text);

private:
    void endRecording();
    /// Starts once the microphone is allowed (false: refused, or asked and the answer is to come)
    bool microphoneAllowed();
    void setDenied(bool on);
    /// Tells the platform's hook what changed since it was told last
    void syncPlatform();
    PlatformState told;
    bool denied = false;
    bool asking = false;
    /// The document of the window's current tab (nullptr: none)
    DocumentSession* current() const { return window.session(); }
    WindowContext window;
    std::unique_ptr<audio::Recorder> recorder;
    std::unique_ptr<audio::Player> player;
    QPointer<DocumentSession> recordingFor;
    fs::path recordingFile;  ///< the file being recorded
    QPointer<DocumentSession> playingFor;
    QString played;
    QVariantList ticks;
    int leadIn = 2000;
};

}  // namespace xqt
