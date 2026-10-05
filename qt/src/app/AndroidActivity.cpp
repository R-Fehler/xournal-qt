/*
 * xournal-qt: see AndroidActivity.h.
 *
 * @license GNU GPLv2 or later
 */
#include "AndroidActivity.h"

#include <QCoreApplication>
#include <QJniEnvironment>
#include <QJniObject>

namespace xqt::android {

namespace {

constexpr const char* ACTIVITY = "org/xournalqt/app/XournalActivity";

std::function<void(const QStringList&)>& receiver() {
    static std::function<void(const QStringList&)> r;
    return r;
}

QStringList takeIncomingFiles() {
    QStringList files;
    const QJniObject array = QJniObject::callStaticObjectMethod(ACTIVITY, "takeIncomingFiles", "()[Ljava/lang/String;");
    if (!array.isValid()) {
        return files;
    }
    QJniEnvironment env;
    auto* strings = static_cast<jobjectArray>(array.object());
    const jsize n = env->GetArrayLength(strings);
    for (jsize i = 0; i < n; ++i) {
        files << QJniObject::fromLocalRef(env->GetObjectArrayElement(strings, i)).toString();
    }
    return files;
}

void deliver() {
    const QStringList files = takeIncomingFiles();
    if (!files.isEmpty() && receiver()) {
        receiver()(files);
    }
}

std::function<void(int)>& recordingCommand() {
    static std::function<void(int)> c;
    return c;
}

/// Java (the notification's buttons, through RecordingService on the Android UI thread)
void JNICALL recordingCommandArrived(JNIEnv*, jclass, jint command) {
    QMetaObject::invokeMethod(QCoreApplication::instance(), [command] {
        if (recordingCommand()) {
            recordingCommand()(static_cast<int>(command));
        }
    }, Qt::QueuedConnection);
}

/// Java (the Android UI thread): new files are waiting.
void JNICALL incomingFilesArrived(JNIEnv*, jclass) {
    QMetaObject::invokeMethod(QCoreApplication::instance(), &deliver, Qt::QueuedConnection);
}

}  // namespace

void watchIncomingFiles(std::function<void(const QStringList&)> receive) {
    receiver() = std::move(receive);
    QJniEnvironment env;
    const JNINativeMethod methods[] = {{"incomingFilesArrived", "()V", reinterpret_cast<void*>(&incomingFilesArrived)}};
    if (!env.registerNativeMethods(ACTIVITY, methods, 1)) {
        qWarning("xournal-qt: files from other apps cannot be received (no %s)", ACTIVITY);
        return;
    }
    deliver();
}

bool hasStylus() { return QJniObject::callStaticMethod<jboolean>(ACTIVITY, "hasStylus", "()Z"); }

void setRecording(bool on, bool paused, qint64 recordedMs, const QString& title, const QStringList& labels) {
    QJniEnvironment env;
    jobjectArray texts =
            env->NewObjectArray(static_cast<jsize>(labels.size()), env.findClass("java/lang/String"), nullptr);
    for (qsizetype i = 0; i < labels.size(); ++i) {
        env->SetObjectArrayElement(texts, static_cast<jsize>(i), QJniObject::fromString(labels[i]).object<jstring>());
    }
    QJniObject::callStaticMethod<void>(ACTIVITY, "setRecording", "(ZZJLjava/lang/String;[Ljava/lang/String;)V",
                                       jboolean(on), jboolean(paused), jlong(recordedMs),
                                       QJniObject::fromString(title).object<jstring>(), texts);
    env->DeleteLocalRef(texts);
}

void watchRecordingCommands(std::function<void(int)> command) {
    recordingCommand() = std::move(command);
    QJniEnvironment env;
    const JNINativeMethod methods[] = {{"recordingCommand", "(I)V", reinterpret_cast<void*>(&recordingCommandArrived)}};
    if (!env.registerNativeMethods(ACTIVITY, methods, 1)) {
        qWarning("xournal-qt: the recording's notification cannot pause or stop it (no %s)", ACTIVITY);
    }
}

bool openAppSettings() { return QJniObject::callStaticMethod<jboolean>(ACTIVITY, "openAppSettings", "()Z"); }

}  // namespace xqt::android
