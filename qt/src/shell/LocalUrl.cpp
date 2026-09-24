#include "LocalUrl.h"

namespace xqt {

namespace {
bool isDriveLetter(const QString& s) { return s.size() == 1 && s.at(0).isLetter(); }
}  // namespace

QString localPathOf(const QUrl& url) {
    if (url.isEmpty()) {
        return {};
    }
    const QString scheme = url.scheme();
    // "file://C:/x": the drive letter taken for a host (QUrl lowercases hosts; drive letters are shown upper case)
    if (scheme == QLatin1String("file") && isDriveLetter(url.host()) && url.port() == -1) {
        return url.host().toUpper() + QLatin1Char(':') + url.path();
    }
    if (url.isLocalFile()) {
        return url.toLocalFile();
    }
    // "C:/x" (or "C:\x") where a URL was expected: the drive letter taken for a scheme
    if (isDriveLetter(scheme)) {
        return scheme.toUpper() + QLatin1Char(':') + url.path();
    }
    return url.toString(QUrl::PreferLocalFile);
}

}  // namespace xqt
