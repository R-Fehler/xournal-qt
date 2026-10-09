#include "AudioControl.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QSettings>
#include <QTimer>
#include <QUrl>
#include <QtCore/qtcore-config.h>
#if QT_CONFIG(permissions)
#include <QPermissions>
#endif

#include "audio/AudioDevice.h"
#include "audio/AudioFiles.h"
#include "audio/DocumentAudio.h"
#include "audio/OggVorbis.h"
#include "audio/Player.h"
#include "audio/Recorder.h"
#include "model/Document.h"
#include "session/DocumentSession.h"

namespace xqt {

namespace {
constexpr const char* LEAD_IN_KEY = "audio/leadInMs";

QString qstr(const std::string& s) { return QString::fromStdString(s); }

std::function<void(const AudioControl::PlatformState&)>& platformHook() {
    static std::function<void(const AudioControl::PlatformState&)> hook;
    return hook;
}
AudioControl::PermissionAccess& permissionAccess() {
    static AudioControl::PermissionAccess access;
    return access;
}
std::function<bool()>& settingsOpener() {
    static std::function<bool()> open;
    return open;
}

/// The system's page for the microphone permission, where a URL opens it
bool openSettingsByUrl() {
#if defined(Q_OS_MACOS)
    return QDesktopServices::openUrl(
            QUrl(QStringLiteral("x-apple.systempreferences:com.apple.preference.security?Privacy_Microphone")));
#elif defined(Q_OS_WIN)
    return QDesktopServices::openUrl(QUrl(QStringLiteral("ms-settings:privacy-microphone")));
#else
    return false;
#endif
}

/// XQT_FAKE_MIC_PERMISSION: "denied", "ask-deny" or "ask-grant" (empty: none)
QString fakePermission() { return qEnvironmentVariable("XQT_FAKE_MIC_PERMISSION"); }

AudioControl::Permission checkMicrophone() {
    if (permissionAccess().check) {
        return permissionAccess().check();
    }
    if (const QString fake = fakePermission(); !fake.isEmpty()) {
        return fake == QLatin1String("denied")       ? AudioControl::Permission::Denied
               : fake.startsWith(QLatin1String("ask")) ? AudioControl::Permission::Undetermined
                                                       : AudioControl::Permission::Granted;
    }
#if QT_CONFIG(permissions)
    // Android and macOS ask the user once; Linux and Windows report it granted
    if (audio::backend() == audio::Backend::Qt) {
        switch (QCoreApplication::instance()->checkPermission(QMicrophonePermission())) {
            case Qt::PermissionStatus::Undetermined:
                return AudioControl::Permission::Undetermined;
            case Qt::PermissionStatus::Denied:
                return AudioControl::Permission::Denied;
            case Qt::PermissionStatus::Granted:
                break;
        }
    }
#endif
    return AudioControl::Permission::Granted;
}

void requestMicrophone(QObject* context, std::function<void(bool)> answer) {
    if (permissionAccess().request) {
        permissionAccess().request(context, std::move(answer));
        return;
    }
    if (const QString fake = fakePermission(); !fake.isEmpty()) {
        QTimer::singleShot(0, context, [answer, granted = fake == QLatin1String("ask-grant")] { answer(granted); });
        return;
    }
#if QT_CONFIG(permissions)
    QCoreApplication::instance()->requestPermission(QMicrophonePermission(), context, [answer](const QPermission& p) {
        answer(p.status() == Qt::PermissionStatus::Granted);
    });
#else
    QTimer::singleShot(0, context, [answer] { answer(true); });
#endif
}
std::string utf8(const QString& s) { return s.toStdString(); }

QString pagesText(const std::vector<size_t>& pages) {
    QStringList parts;
    for (size_t i = 0; i < pages.size(); ++i) {
        size_t j = i;
        while (j + 1 < pages.size() && pages[j + 1] == pages[j] + 1) {
            ++j;
        }
        parts << (j == i ? QString::number(pages[i] + 1) : QStringLiteral("%1–%2").arg(pages[i] + 1).arg(pages[j] + 1));
        i = j;
    }
    return parts.join(QStringLiteral(", "));
}
/// A document's title in the recording's notification: its file's name
QString titleOf(const DocumentSession* s) {
    return s && s->hasFilePath() ? QString::fromStdU16String(s->getFilePath().filename().u16string())
                                 : QCoreApplication::translate("AudioControl", "Untitled");
}
}  // namespace

AudioControl::AudioControl(const WindowContext& w, QObject* parent):
        QObject(parent),
        window(w),
        recorder(std::make_unique<audio::Recorder>()),
        player(std::make_unique<audio::Player>()) {
    leadIn = QSettings().value(LEAD_IN_KEY, 2000).toInt();
    connect(recorder.get(), &audio::Recorder::stateChanged, this, &AudioControl::recordingChanged);
    connect(recorder.get(), &audio::Recorder::positionChanged, this, &AudioControl::recordedMsChanged);
    connect(recorder.get(), &audio::Recorder::levelChanged, this, &AudioControl::levelChanged);
    connect(recorder.get(), &audio::Recorder::silentChanged, this, &AudioControl::levelChanged);
    connect(recorder.get(), &audio::Recorder::failed, this, [this](const QString& error) {
        endRecording();
        Q_EMIT message(tr("The recording stopped: %1").arg(error));
    });
    connect(player.get(), &audio::Player::stateChanged, this, &AudioControl::playbackChanged);
    connect(player.get(), &audio::Player::positionChanged, this, &AudioControl::playPositionChanged);
    connect(player.get(), &audio::Player::failed, this,
            [this](const QString& error) { Q_EMIT message(tr("Playing stopped: %1").arg(error)); });
    // The platform hears of every start, pause, resume and end (after the slots above have run)
    connect(this, &AudioControl::recordingChanged, this, &AudioControl::syncPlatform, Qt::QueuedConnection);
}

AudioControl::~AudioControl() {
    if (recorder->isRecording()) {
        stopRecording();
        syncPlatform();  // (now: the queued call would not come any more)
    }
}

bool AudioControl::available() const { return audio::available(); }
bool AudioControl::recording() const { return recorder->isRecording(); }
bool AudioControl::recordingPaused() const { return recorder->state() == audio::Recorder::State::Paused; }
bool AudioControl::recordingHere() const { return recording() && recordingFor && recordingFor == current(); }
QString AudioControl::recordingTitle() const { return recording() && recordingFor ? titleOf(recordingFor) : QString(); }
qint64 AudioControl::recordedMs() const { return recorder->positionMs(); }
qreal AudioControl::level() const { return recorder->level(); }
bool AudioControl::silent() const { return recorder->silent(); }
bool AudioControl::playing() const { return player->state() != audio::Player::State::Stopped; }
bool AudioControl::playPaused() const { return player->state() == audio::Player::State::Paused; }
qint64 AudioControl::playDurationMs() const { return player->durationMs(); }
qint64 AudioControl::playPositionMs() const { return player->positionMs(); }

void AudioControl::setLeadInMs(int ms) {
    ms = std::clamp(ms, 0, 30000);
    if (ms == leadIn) {
        return;
    }
    leadIn = ms;
    QSettings().setValue(LEAD_IN_KEY, ms);
    Q_EMIT leadInChanged();
}

void AudioControl::setPlatformHook(std::function<void(const PlatformState&)> hook) { platformHook() = std::move(hook); }
void AudioControl::setPermissionAccess(PermissionAccess access) { permissionAccess() = std::move(access); }
void AudioControl::setSettingsOpener(std::function<bool()> open) { settingsOpener() = std::move(open); }

QString AudioControl::microphoneSettingsPath() const {
#if defined(Q_OS_MACOS)
    return tr("System Settings → Privacy & Security → Microphone");
#elif defined(Q_OS_WIN)
    return tr("Settings → Privacy & security → Microphone");
#elif defined(Q_OS_ANDROID)
    return tr("Settings → Apps → Xournal Qt → Permissions → Microphone");
#else
    return tr("the system's privacy settings");
#endif
}

bool AudioControl::canOpenMicrophoneSettings() const {
#if defined(Q_OS_MACOS) || defined(Q_OS_WIN)
    return true;
#else
    return bool(settingsOpener());
#endif
}

bool AudioControl::openMicrophoneSettings() {
    setDenied(false);
    return settingsOpener() ? settingsOpener()() : openSettingsByUrl();
}

void AudioControl::dismissMicrophoneNotice() { setDenied(false); }

void AudioControl::setDenied(bool on) {
    if (denied != on) {
        denied = on;
        Q_EMIT microphoneDeniedChanged();
    }
}

bool AudioControl::microphoneAllowed() {
    switch (checkMicrophone()) {
        case Permission::Granted:
            return true;
        case Permission::Denied:
            setDenied(true);
            return false;
        case Permission::Undetermined:
            break;
    }
    // The system asks the user (once); recording starts when the answer is yes
    if (!asking) {
        asking = true;
        requestMicrophone(this, [this](bool granted) {
            asking = false;
            if (granted) {
                startRecording();
            } else {
                setDenied(true);
            }
        });
    }
    return false;
}

void AudioControl::syncPlatform() {
    PlatformState now;
    now.recording = recording();
    now.paused = recordingPaused();
    if (now.recording) {
        now.recordedMs = recordedMs();
        now.title = recordingTitle();
    }
    // (the time alone is no news: the notification's clock runs by itself)
    const bool changed = now.recording != told.recording || now.paused != told.paused ||
                         (now.recording && now.title != told.title);
    told = now;
    if (changed && platformHook()) {
        platformHook()(now);
    }
}

void AudioControl::platformCommand(PlatformCommand command) {
    switch (command) {
        case PlatformCommand::Pause:
            pauseRecording();
            break;
        case PlatformCommand::Resume:
            resumeRecording();
            break;
        case PlatformCommand::Stop:
            stopRecording();
            break;
    }
}

bool AudioControl::startRecording() {
    DocumentSession* s = current();
    if (!s || recorder->isRecording()) {
        return false;
    }
    if (!microphoneAllowed()) {
        return false;
    }
    setDenied(false);
    stopPlayback();  // (the speaker would be recorded)
    // Into the document's sidecar ("name.audio" next to its .xopp), else (not saved yet, a PDF with notes) the app's
    // audio folder: the first save takes it into the sidecar (qt/docs/features/audio.md, "Storage")
    const fs::path folder = audio::recordingFolderFor(s->getFilePath());
    const std::string name = audio::newRecordingName(QDateTime::currentDateTime(), [&](const std::string& n) {
        std::error_code ec;
        return fs::exists(folder / n, ec);
    });
    if (!recorder->start(folder / name)) {
        Q_EMIT message(tr("Recording cannot start: %1").arg(recorder->error()));
        return false;
    }
    recordingFor = s;
    recordingFile = folder / name;
    audio::setBusy(recordingFile, true);  // (a save leaves it where it is until it is done: endRecording)
    audio::Recorder* r = recorder.get();
    s->setRecording(name, [r] { return static_cast<size_t>(r->positionMs()); });
    s->addVoiceMemo(s->getCurrentPageNo(), name);
    connect(s, &QObject::destroyed, this, [this] {
        // Its tab closed: the recording ends with it (the session is gone: nothing more to tell it)
        recordingFor = nullptr;
        if (recorder->isRecording()) {
            recorder->stop();
            Q_EMIT message(tr("The recording ended with its document."));
        }
        audio::setBusy(recordingFile, false);
        recordingFile.clear();
        Q_EMIT recordingChanged();
    });
    Q_EMIT recordingChanged();
    return true;
}

void AudioControl::endRecording() {
    audio::setBusy(recordingFile, false);
    if (recordingFor) {
        recordingFor->setRecording({}, {});
        disconnect(recordingFor, &QObject::destroyed, this, nullptr);
        // Saved as a .xopp while it recorded (the save left it): into the sidecar now
        const fs::path doc = recordingFor->getFilePath();
        std::error_code ec;
        if (audio::keepsSidecar(doc) && fs::is_regular_file(recordingFile, ec) &&
            !fs::equivalent(recordingFile.parent_path(), audio::sidecarOf(doc), ec)) {
            audio::moveInto(recordingFile, audio::sidecarOf(doc));
        }
    }
    recordingFile.clear();
    recordingFor = nullptr;
    Q_EMIT recordingChanged();
}

void AudioControl::stopRecording() {
    if (!recorder->isRecording()) {
        return;
    }
    recorder->stop();
    if (!recorder->error().isEmpty()) {
        Q_EMIT message(tr("The recording may be incomplete: %1").arg(recorder->error()));
    }
    endRecording();
}

void AudioControl::pauseRecording() { recorder->pause(); }
void AudioControl::resumeRecording() { recorder->resume(); }

void AudioControl::toggleRecording() {
    if (recorder->isRecording()) {
        stopRecording();
    } else {
        startRecording();
    }
}

bool AudioControl::play(const QString& name, qint64 fromMs) {
    DocumentSession* s = current();
    if (!s || name.isEmpty()) {
        return false;
    }
    if (recorder->isRecording()) {
        Q_EMIT message(tr("Stop the recording to play one."));
        return false;
    }
    const fs::path file = audio::find(utf8(name), s->getFilePath());
    if (file.empty()) {
        Q_EMIT message(tr("The recording %1 is not here (looked next to the document and in the audio folder).")
                               .arg(QString::fromStdU16String(fs::path(utf8(name)).filename().u16string())));
        return false;
    }
    if (!player->play(file, fromMs)) {
        Q_EMIT message(player->error());
        return false;
    }
    playingFor = s;
    played = name;
    ticks.clear();
    {
        std::shared_lock lock(*s->getDocument());
        for (const auto& m: audio::momentsOf(*s->getDocument(), utf8(name))) {
            ticks.append(static_cast<qint64>(m.ts));
        }
    }
    Q_EMIT playbackChanged();
    return true;
}

bool AudioControl::playMoment(const QString& name, qint64 ts) { return play(name, std::max<qint64>(0, ts - leadIn)); }

void AudioControl::pausePlayback() { player->pause(); }
void AudioControl::resumePlayback() { player->resume(); }

void AudioControl::stopPlayback() {
    if (playing()) {
        player->stop();
        Q_EMIT playbackChanged();
    }
}

void AudioControl::seek(qint64 ms) { player->seek(ms); }

void AudioControl::skip(qint64 deltaMs) { player->seek(player->positionMs() + deltaMs); }

QVariantList AudioControl::recordings() const {
    QVariantList out;
    DocumentSession* s = current();
    if (!s) {
        return out;
    }
    std::vector<audio::Recording> recs;
    {
        std::shared_lock lock(*s->getDocument());
        recs = audio::recordingsOf(*s->getDocument());
    }
    for (const auto& r: recs) {
        const fs::path file = audio::find(r.name, s->getFilePath());
        QVariantMap m;
        m["name"] = qstr(r.name);
        m["title"] = QString::fromStdU16String(fs::path(r.name).stem().u16string());
        m["pages"] = pagesText(r.pages);
        m["firstPage"] = r.pages.empty() ? 0 : static_cast<int>(r.pages.front());
        m["elements"] = static_cast<int>(r.elements);
        m["durationMs"] = file.empty() ? qint64(-1) : static_cast<qint64>(audio::durationMsOf(file));
        m["found"] = !file.empty();
        out.append(m);
    }
    return out;
}

bool AudioControl::removeRecording(const QString& name) {
    DocumentSession* s = current();
    if (!s) {
        return false;
    }
    if (played == name) {
        stopPlayback();
    }
    return s->removeRecording(utf8(name)) > 0;
}

void AudioControl::currentChanged() {
    if (playing() && playingFor != current()) {
        stopPlayback();  // (another document: its recording is not this one's)
    }
    Q_EMIT recordingChanged();
}

}  // namespace xqt
