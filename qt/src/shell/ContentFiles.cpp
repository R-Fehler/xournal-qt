/*
 * xournal-qt: see ContentFiles.h.
 *
 * @license GNU GPLv2 or later
 */
#include "ContentFiles.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QMimeDatabase>

namespace xqt::ContentFiles {

QString sharedStoragePath(const QUrl& url, const StorageRoots& roots) {
    if (url.scheme() != QLatin1String("content")) {
        return {};
    }
    // The path is ".../tree/<tree id>" or ".../tree/<tree id>/document/<document id>" (or ".../document/<id>"), the
    // ids percent-encoded as one segment each ("primary%3ADocuments%2FUni"): the document id, where there is one, is
    // what was picked
    const QStringList segments = url.path(QUrl::FullyEncoded).split(QLatin1Char('/'), Qt::SkipEmptyParts);
    QString id;
    for (qsizetype i = 0; i + 1 < segments.size(); ++i) {
        if (segments[i] == QLatin1String("tree") || segments[i] == QLatin1String("document")) {
            id = QUrl::fromPercentEncoding(segments[i + 1].toUtf8());
        }
    }
    if (id.isEmpty()) {
        return {};
    }
    QString path;
    const QString authority = url.host();
    if (authority == QLatin1String("com.android.externalstorage.documents")) {
        const qsizetype colon = id.indexOf(QLatin1Char(':'));
        if (colon <= 0) {
            return {};
        }
        const QString volume = id.left(colon);
        const QString rel = id.mid(colon + 1);
        QString base;
        if (volume == QLatin1String("primary")) {
            base = roots.primary;
        } else if (volume == QLatin1String("home")) {
            base = roots.primary + QStringLiteral("/Documents");  // (the picker's "Documents")
        } else if (!volume.contains(QLatin1Char('/')) && volume != QLatin1String("..")) {
            base = roots.volumes + QLatin1Char('/') + volume;  // an SD card, a USB drive ("1234-ABCD")
        } else {
            return {};
        }
        path = rel.isEmpty() ? base : base + QLatin1Char('/') + rel;
    } else if (authority == QLatin1String("com.android.providers.downloads.documents")) {
        if (id == QLatin1String("downloads")) {
            path = roots.primary + QStringLiteral("/Download");
        } else if (id.startsWith(QLatin1String("raw:/"))) {
            path = id.mid(4);
        } else {
            return {};  // (numbers of the downloads database: no path)
        }
    } else {
        return {};
    }
    // No way out of the storage by "..", and only what is there
    const QString clean = QDir::cleanPath(path);
    if (path.split(QLatin1Char('/')).contains(QStringLiteral(".."))) {
        return {};
    }
    return QFileInfo::exists(clean) ? clean : QString();
}

namespace {

/// The extension of a file by its content, for a name without one (other apps may give "document" or a number).
QString extensionByContent(QFile& file) {
    const QMimeType mime = QMimeDatabase().mimeTypeForData(file.peek(4096));
    const QString name = mime.name();
    if (name == "application/pdf") {
        return ".pdf";
    }
    if (name == "application/gzip" || name == "application/x-gzip") {
        return ".xopp";  // (a .xopp is gzip-compressed XML)
    }
    if (name == "image/png" || name == "image/jpeg" || name == "image/webp" || name == "image/gif") {
        return "." + mime.preferredSuffix();
    }
    if (mime.inherits("text/plain")) {
        return ".md";
    }
    return {};
}

bool copyFile(QFile& in, const fs::path& target, std::string& error) {
    QFile out(QString::fromStdString(target.string()));
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        error = "Could not write \"" + target.string() + "\": " + out.errorString().toStdString();
        return false;
    }
    char buffer[1 << 16];
    for (;;) {
        const qint64 n = in.read(buffer, sizeof buffer);
        if (n < 0) {
            error = "Could not read \"" + target.filename().string() + "\": " + in.errorString().toStdString();
            return false;
        }
        if (n == 0) {
            break;
        }
        if (out.write(buffer, n) != n) {
            error = "Could not write \"" + target.string() + "\": " + out.errorString().toStdString();
            return false;
        }
    }
    return true;
}

/// A name not yet taken in `folder`: "name.ext", "name (2).ext", ...
fs::path freePath(const fs::path& folder, const std::string& name) {
    fs::path candidate = folder / name;
    const fs::path n(name);
    std::error_code ec;
    for (int i = 2; fs::exists(candidate, ec); ++i) {
        candidate = folder / (n.stem().string() + " (" + std::to_string(i) + ")" + n.extension().string());
    }
    return candidate;
}

fs::path copyTree(const QString& source, const QString& name, const fs::path& into, int depth, std::string& error) {
    if (depth > 32) {
        error = "\"" + name.toStdString() + "\" is nested too deep.";
        return {};
    }
    const fs::path dir = freePath(into, safeName(name));
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) {
        error = "Could not create \"" + dir.string() + "\": " + ec.message();
        return {};
    }
    QDirIterator it(source, QDir::AllEntries | QDir::NoDotAndDotDot);
    while (it.hasNext()) {
        const QString child = it.next();
        const QFileInfo info = it.fileInfo();
        if (info.fileName().startsWith('.')) {
            continue;  // hidden files and folders (caches, .git) stay behind
        }
        std::string childError;
        if (info.isDir()) {
            copyTree(child, info.fileName(), dir, depth + 1, childError);
        } else {
            QFile in(child);
            if (in.open(QIODevice::ReadOnly)) {
                copyFile(in, freePath(dir, safeName(info.fileName())), childError);
            } else {
                childError = "Could not read \"" + info.fileName().toStdString() + "\".";
            }
        }
        if (!childError.empty()) {
            error += (error.empty() ? "" : "\n") + childError;  // go on with the others
        }
    }
    return dir;
}

}  // namespace

bool isForeign(const QUrl& url) { return url.isValid() && !url.isLocalFile() && url.scheme() == "content"; }

QString sourceOf(const QUrl& url) { return url.isLocalFile() ? url.toLocalFile() : url.toString(); }

std::string safeName(const QString& name) {
    QString n = name.section('/', -1).section('\\', -1).trimmed();
    for (QChar& c: n) {
        if (c < QChar(32) || QStringLiteral(":*?\"<>|").contains(c)) {
            c = '_';
        }
    }
    while (n.startsWith('.')) {
        n.remove(0, 1);
    }
    if (n.isEmpty()) {
        n = QStringLiteral("Document");
    }
    return n.toStdString();
}

fs::path copyInto(const QString& source, const fs::path& into, std::string& error) {
    const QFileInfo info(source);
    if (info.isDir()) {
        return copyTree(source, info.fileName(), into, 0, error);
    }
    QFile in(source);
    if (!in.open(QIODevice::ReadOnly)) {
        error = "Could not read \"" + (info.fileName().isEmpty() ? source : info.fileName()).toStdString() + "\": " +
                in.errorString().toStdString();
        return {};
    }
    std::string name = safeName(info.fileName());
    if (fs::path(name).extension().empty()) {
        name += extensionByContent(in).toStdString();
    }
    const fs::path target = freePath(into, name);
    if (!copyFile(in, target, error)) {
        std::error_code ec;
        fs::remove(target, ec);
        return {};
    }
    return target;
}

}  // namespace xqt::ContentFiles
