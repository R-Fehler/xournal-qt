#include "AudioControl.h"

#include <QDateTime>
#include <QSettings>

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
}  // namespace

AudioControl::AudioControl(std::function<DocumentSession*()> c, std::function<QString(DocumentSession*)> t,
                           QObject* parent):
        QObject(parent),
        current(std::move(c)),
        titleOf(std::move(t)),
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
}

AudioControl::~AudioControl() {
    if (recorder->isRecording()) {
        stopRecording();
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

bool AudioControl::startRecording() {
    DocumentSession* s = current();
    if (!s || recorder->isRecording()) {
        return false;
    }
    stopPlayback();  // (the speaker would be recorded)
    const fs::path folder = audio::appFolder();
    const std::string name = audio::newRecordingName(QDateTime::currentDateTime(), [&](const std::string& n) {
        std::error_code ec;
        return fs::exists(folder / n, ec);
    });
    if (!recorder->start(folder / name)) {
        Q_EMIT message(tr("Recording cannot start: %1").arg(recorder->error()));
        return false;
    }
    recordingFor = s;
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
        Q_EMIT recordingChanged();
    });
    Q_EMIT recordingChanged();
    return true;
}

void AudioControl::endRecording() {
    if (recordingFor) {
        recordingFor->setRecording({}, {});
        disconnect(recordingFor, &QObject::destroyed, this, nullptr);
    }
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
