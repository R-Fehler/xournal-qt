#include "SystemApps.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QMimeData>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
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

namespace {
/// The file manager shows these files, selected (all in one folder, or not: the first one's folder then).
bool showItems(const QStringList& paths) {
    if (paths.isEmpty()) {
        return false;
    }
    const QFileInfo info(paths.first());
#if defined(Q_OS_WIN)
    return QProcess::startDetached(QStringLiteral("explorer"),
                                   {QStringLiteral("/select,") + QDir::toNativeSeparators(info.absoluteFilePath())});
#elif defined(Q_OS_MACOS)
    QStringList args{QStringLiteral("-R")};
    for (const QString& p: paths) {
        args << QFileInfo(p).absoluteFilePath();
    }
    return QProcess::startDetached(QStringLiteral("open"), args);
#else
    const QString folder = info.absolutePath();
#ifdef XQT_HAVE_DBUS
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (bus.isConnected()) {
        QDBusMessage call = QDBusMessage::createMethodCall(
                QStringLiteral("org.freedesktop.FileManager1"), QStringLiteral("/org/freedesktop/FileManager1"),
                QStringLiteral("org.freedesktop.FileManager1"), QStringLiteral("ShowItems"));
        QStringList uris;
        for (const QString& p: paths) {
            uris << QUrl::fromLocalFile(QFileInfo(p).absoluteFilePath()).toString();
        }
        call << uris << QString();
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
}  // namespace

bool SystemApps::showInFileManager(const QString& path) {
    if (!canShowInFileManager()) {
        return false;
    }
    const QFileInfo info(path);
    if (info.isDir()) {
        return QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    }
    return showItems({path});
}

bool SystemApps::canShare() { return canShowInFileManager(); }

bool SystemApps::share(const QStringList& files) {
    if (!canShare() || files.isEmpty()) {
        return false;  // (Android, iOS: the share sheet, later)
    }
    return showItems(files);
}

bool SystemApps::copyToClipboard(const QStringList& files) {
    QClipboard* clipboard = QGuiApplication::clipboard();
    if (!clipboard || files.isEmpty()) {
        return false;
    }
    auto* data = new QMimeData;
    QList<QUrl> urls;
    QStringList paths;
    QByteArray gnome("copy");
    for (const QString& f: files) {
        const QString abs = QFileInfo(f).absoluteFilePath();
        urls << QUrl::fromLocalFile(abs);
        paths << abs;
        gnome += "\n" + QUrl::fromLocalFile(abs).toEncoded();
    }
    data->setUrls(urls);  // text/uri-list: file managers, browsers, most chat apps
    data->setData(QStringLiteral("x-special/gnome-copied-files"), gnome);
    if (files.size() == 1 && files.first().endsWith(QStringLiteral(".pdf"), Qt::CaseInsensitive)) {
        // The bytes too, for apps that take the data itself (not for a huge PDF)
        QFile pdf(files.first());
        if (pdf.size() <= maxPdfBytes && pdf.open(QIODevice::ReadOnly)) {
            data->setData(QStringLiteral("application/pdf"), pdf.readAll());
        }
    }
    data->setText(paths.join(QLatin1Char('\n')));
    clipboard->setMimeData(data);
    return true;
}

bool SystemApps::moveToTrash(const QString& path) { return QFile::moveToTrash(path); }

bool SystemApps::startLibraryWindow(const QString& folder) {
    return QProcess::startDetached(QCoreApplication::applicationFilePath(), {folder});
}

}  // namespace xqt
