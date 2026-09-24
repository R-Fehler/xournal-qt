#include "SystemApps.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QUrl>

#ifdef XQT_HAVE_DBUS
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#endif

namespace xqt {

namespace {
SystemApps*& current() {
    static SystemApps* apps = nullptr;
    return apps;
}
}  // namespace

SystemApps& SystemApps::instance() {
    static SystemApps real;
    return current() ? *current() : real;
}

void SystemApps::setInstance(SystemApps* apps) { current() = apps; }

bool SystemApps::canShowInFileManager() {
#ifdef Q_OS_ANDROID
    return false;
#else
    return true;
#endif
}

bool SystemApps::openWithSystemApp(const QString& path) {
    return QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

bool SystemApps::showInFileManager(const QString& path) {
    if (!canShowInFileManager()) {
        return false;
    }
    const QFileInfo info(path);
    if (info.isDir()) {
        return QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    }
#if defined(Q_OS_WIN)
    return QProcess::startDetached(QStringLiteral("explorer"),
                                   {QStringLiteral("/select,") + QDir::toNativeSeparators(info.absoluteFilePath())});
#elif defined(Q_OS_MACOS)
    return QProcess::startDetached(QStringLiteral("open"), {QStringLiteral("-R"), info.absoluteFilePath()});
#else
    const QString folder = info.absolutePath();
#ifdef XQT_HAVE_DBUS
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (bus.isConnected()) {
        QDBusMessage call = QDBusMessage::createMethodCall(
                QStringLiteral("org.freedesktop.FileManager1"), QStringLiteral("/org/freedesktop/FileManager1"),
                QStringLiteral("org.freedesktop.FileManager1"), QStringLiteral("ShowItems"));
        call << QStringList{QUrl::fromLocalFile(info.absoluteFilePath()).toString()} << QString();
        // Not waited for: when no file manager answers, the folder is opened instead
        auto* watcher = new QDBusPendingCallWatcher(bus.asyncCall(call, 5000), QCoreApplication::instance());
        QObject::connect(watcher, &QDBusPendingCallWatcher::finished, watcher, [watcher, folder] {
            if (watcher->isError()) {
                QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
            }
            watcher->deleteLater();
        });
        return true;
    }
#endif
    return QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
#endif
}

bool SystemApps::startLibraryWindow(const QString& folder) {
    return QProcess::startDetached(QCoreApplication::applicationFilePath(), {folder});
}

}  // namespace xqt
