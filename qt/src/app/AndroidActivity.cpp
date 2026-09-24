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

}  // namespace xqt::android
