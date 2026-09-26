#include "MarkdownImages.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMimeData>
#include <QObject>
#include <QSaveFile>
#include <QUrl>

#include "session/DocumentImages.h"
#include "session/DocumentSession.h"

#include "MdImages.h"

namespace xqt::MarkdownImages {

namespace {
QString qpath(const fs::path& p) {
    const std::u8string s = p.u8string();
    return QString::fromUtf8(reinterpret_cast<const char*>(s.data()), static_cast<qsizetype>(s.size()));
}
fs::path pathOf(const QString& s) {
    const QByteArray u = s.toUtf8();
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(u.constData()), static_cast<size_t>(u.size())));
}
bool taken(const fs::path& p) {
    std::error_code ec;
    return fs::exists(p, ec);
}
}  // namespace

std::optional<Place> placeOf(const DocumentSession& session) {
    const md::images::Root* root = session.imageRootOf();
    if (!root || root->assetsDir.empty() || root->assetsName.empty()) {
        return std::nullopt;
    }
    return Place{pathOf(QString::fromStdString(root->assetsDir)), root->assetsName + "/"};
}

std::string newName(const fs::path& folder, const std::string& extension, const QDateTime& now) {
    const std::string stem = "image-" + now.toString(QStringLiteral("yyyy-MM-dd-HHmmss")).toStdString();
    std::string name = stem + "." + extension;
    for (int n = 2; taken(folder / pathOf(QString::fromStdString(name))); ++n) {
        name = stem + "-" + std::to_string(n) + "." + extension;
    }
    return name;
}

std::string freeName(const fs::path& folder, const std::string& given) {
    QString name = QFileInfo(QString::fromStdString(given)).fileName();  // (no folders)
    name.replace(QLatin1Char('\\'), QLatin1Char('_'));
    while (name.startsWith(QLatin1Char('.'))) {
        name.remove(0, 1);  // (not hidden)
    }
    if (name.isEmpty()) {
        name = QStringLiteral("image");
    }
    const QFileInfo info(name);
    const QString base = info.completeBaseName();
    const QString suffix = info.suffix().isEmpty() ? QString() : "." + info.suffix();
    QString candidate = name;
    for (int n = 2; taken(folder / pathOf(candidate)); ++n) {
        candidate = QStringLiteral("%1 (%2)%3").arg(base).arg(n).arg(suffix);
    }
    return candidate.toStdString();
}

std::string linkFor(const Place& place, const std::string& name) {
    return place.prefix + DocumentImages::linkEncoded(name);
}

bool isPictureName(const QString& name) {
    static const QStringList kinds = {"png", "jpg", "jpeg", "gif", "webp", "svg", "bmp"};
    return kinds.contains(QFileInfo(name).suffix().toLower());
}

std::string markdownFor(const std::string& link, const std::string& alt) {
    std::string escaped;
    for (const char c: alt) {
        if (c == '[' || c == ']' || c == '\\') {
            escaped += '\\';
        }
        escaped += c;
    }
    return "![" + escaped + "](" + link + ")";
}

std::optional<std::string> savePicture(const DocumentSession& session, const QImage& image, QString& error,
                                       const QDateTime& now) {
    const auto place = placeOf(session);
    if (!place) {
        error = QObject::tr("Pictures can be added once the document is saved.");
        return std::nullopt;
    }
    if (image.isNull()) {
        error = QObject::tr("The clipboard holds no picture that can be read.");
        return std::nullopt;
    }
    QDir().mkpath(qpath(place->folder));
    const std::string name = newName(place->folder, "png", now);
    QSaveFile file(qpath(place->folder / pathOf(QString::fromStdString(name))));
    if (!file.open(QIODevice::WriteOnly) || !image.save(&file, "PNG") || !file.commit()) {
        error = QObject::tr("The picture could not be saved in %1.").arg(qpath(place->folder));
        return std::nullopt;
    }
    md::images::changed();
    return linkFor(*place, name);
}

std::optional<std::string> addPictureFile(const DocumentSession& session, const QString& source, const QString& name,
                                          QString& error) {
    const auto place = placeOf(session);
    if (!place) {
        error = QObject::tr("Pictures can be added once the document is saved.");
        return std::nullopt;
    }
    // A file of the folder already: linked as it is
    const QFileInfo in(source);
    if (in.exists() && QFileInfo(in.absolutePath()) == QFileInfo(qpath(place->folder))) {
        return linkFor(*place, in.fileName().toStdString());
    }
    QFile from(source);
    if (!from.open(QIODevice::ReadOnly)) {
        error = QObject::tr("\"%1\" cannot be read.").arg(name);
        return std::nullopt;
    }
    QDir().mkpath(qpath(place->folder));
    const std::string target = freeName(place->folder, name.toStdString());
    QSaveFile to(qpath(place->folder / pathOf(QString::fromStdString(target))));
    if (!to.open(QIODevice::WriteOnly)) {
        error = QObject::tr("The picture could not be saved in %1.").arg(qpath(place->folder));
        return std::nullopt;
    }
    while (!from.atEnd()) {
        const QByteArray chunk = from.read(1 << 20);
        if (chunk.isEmpty() || to.write(chunk) != chunk.size()) {
            to.cancelWriting();
            error = QObject::tr("\"%1\" could not be copied.").arg(name);
            return std::nullopt;
        }
    }
    if (!to.commit()) {
        error = QObject::tr("The picture could not be saved in %1.").arg(qpath(place->folder));
        return std::nullopt;
    }
    md::images::changed();
    return linkFor(*place, target);
}

std::optional<std::string> pastedPictures(const DocumentSession& session, const QMimeData* mime, QString& error) {
    if (!mime) {
        return std::nullopt;
    }
    // Copied picture files (a file manager's copy)
    QList<QUrl> files;
    for (const QUrl& url: mime->urls()) {
        if (url.isLocalFile() && isPictureName(url.fileName())) {
            files.push_back(url);
        }
    }
    const bool onlyPictureFiles = !files.isEmpty() && files.size() == mime->urls().size();
    const QString text = mime->text().trimmed();
    const bool textIsNoText = text.isEmpty() || (!text.contains(QLatin1Char('\n')) && QUrl(text).isValid() &&
                                                 !QUrl(text).scheme().isEmpty());
    if (!onlyPictureFiles && !(mime->hasImage() && textIsNoText)) {
        return std::nullopt;
    }
    if (!placeOf(session)) {
        return std::nullopt;  // (nowhere to keep them: the text, if any, as before)
    }
    std::string out;
    if (onlyPictureFiles) {
        for (const QUrl& url: files) {
            const auto link = addPictureFile(session, url.toLocalFile(), url.fileName(), error);
            if (!link) {
                return std::string();
            }
            out += (out.empty() ? "" : "\n\n") + markdownFor(*link, QFileInfo(url.fileName()).completeBaseName().toStdString());
        }
        return out;
    }
    const auto link = savePicture(session, qvariant_cast<QImage>(mime->imageData()), error);
    if (!link) {
        return std::string();
    }
    return markdownFor(*link);
}

}  // namespace xqt::MarkdownImages
