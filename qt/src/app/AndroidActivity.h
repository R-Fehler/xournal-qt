/*
 * xournal-qt: the bridge to the app's activity on Android (qt/packaging/android/src/org/xournalqt/app/
 * XournalActivity.java, see qt/docs/android.md).
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <functional>

#include <QStringList>

namespace xqt::android {

/// The entry the launcher's shortcut "Quick note" adds to the incoming files (XournalActivity.QUICK_NOTE)
inline constexpr const char* QUICK_NOTE = "xournal-qt:quick-note";

/// Files other apps hand over ("Open with", the share sheet): `receive` gets the ones waiting since the start now,
/// and later ones as they come (on the UI thread; content:// URIs, see AppController::receiveFiles). The launcher's
/// shortcut "Quick note" arrives among them as QUICK_NOTE.
void watchIncomingFiles(std::function<void(const QStringList&)> receive);

/// A stylus is attached (Android's input devices report a stylus source).
bool hasStylus();

/// A recording runs, paused or ended: the foreground service that keeps the microphone in the background, with its
/// notification (qt/docs/audio.md, "Android"): the time (`recordedMs` now; its clock runs on while not paused), the
/// document's `title`, and Pause/Resume and Stop. `labels`: the notification's texts, translated (recording, paused,
/// pause, resume, stop).
void setRecording(bool on, bool paused, qint64 recordedMs, const QString& title, const QStringList& labels);

/// The notification's buttons: `command` gets 1 (pause), 2 (resume) or 3 (stop), on the UI thread
/// (AudioControl::PlatformCommand).
void watchRecordingCommands(std::function<void(int command)> command);

/// The app's page in the system's settings (where the microphone is allowed after a refusal).
bool openAppSettings();

}  // namespace xqt::android
