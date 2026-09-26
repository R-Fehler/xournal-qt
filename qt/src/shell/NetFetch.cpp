/*
 * xournal-qt: the app's own network requests (see NetFetch.h).
 *
 * @license GNU GPLv2 or later
 */
#include "NetFetch.h"

#include <QCoreApplication>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>

namespace xqt {

namespace {
NetFetch* current = nullptr;
int gap = 3000;  // arXiv: "no more than one request every three seconds"
/// When the last request to arXiv went out (for the whole program: every window's queue)
QElapsedTimer& lastRequest() {
    static QElapsedTimer t;
    return t;
}
}  // namespace

QByteArray NetFetch::userAgent() {
    const QString version = QCoreApplication::applicationVersion();
    return QStringLiteral("xournal-qt/%1 (+https://github.com/R-Fehler/xournal-qt)")
            .arg(version.isEmpty() ? QStringLiteral("dev") : version)
            .toUtf8();
}

NetFetch& NetFetch::instance() {
    if (current) {
        return *current;
    }
    static QtNetFetch real;
    return real;
}

void NetFetch::setInstance(NetFetch* fetch) { current = fetch; }

QtNetFetch::QtNetFetch() = default;

QtNetFetch::~QtNetFetch() = default;  // (the manager is the application's child)

QNetworkAccessManager* QtNetFetch::manager() {
    if (!nam) {
        nam = new QNetworkAccessManager(QCoreApplication::instance());
    }
    return nam;
}

void QtNetFetch::get(const QUrl& url, int timeoutMs, qint64 maxBytes, Done done) {
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, userAgent());
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(timeoutMs);  // (no data for that long: aborted, OperationCanceledError)
    QNetworkReply* reply = manager()->get(request);
    QObject::connect(reply, &QNetworkReply::downloadProgress, reply, [reply, maxBytes](qint64 received, qint64 total) {
        if (received > maxBytes || total > maxBytes) {
            reply->setProperty("xqtTooBig", true);
            reply->abort();
        }
    });
    QObject::connect(reply, &QNetworkReply::finished, reply, [reply, url, timeoutMs, done = std::move(done)] {
        Reply r;
        r.url = url;
        r.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->property("xqtTooBig").toBool()) {
            r.error = QCoreApplication::translate("NetFetch", "The answer is too big.");
        } else if (reply->error() == QNetworkReply::OperationCanceledError ||
                   reply->error() == QNetworkReply::TimeoutError) {
            r.error = QCoreApplication::translate("NetFetch", "%1 did not answer within %2 s.")
                              .arg(url.host())
                              .arg(timeoutMs / 1000);
        } else if (reply->error() != QNetworkReply::NoError) {
            r.error = r.status > 0 ? QCoreApplication::translate("NetFetch", "%1 answered with error %2.")
                                             .arg(url.host())
                                             .arg(r.status)
                                   : reply->errorString();
        } else {
            r.body = reply->readAll();
        }
        reply->deleteLater();
        done(r);
    });
}

ArxivQueue::ArxivQueue(QObject* parent): QObject(parent) {
    timer.setSingleShot(true);
    connect(&timer, &QTimer::timeout, this, &ArxivQueue::next);
}

int ArxivQueue::interval() { return gap; }

void ArxivQueue::setInterval(int ms) { gap = ms; }

void ArxivQueue::get(const QUrl& url, int timeoutMs, qint64 maxBytes, NetFetch::Done done) {
    pending.push_back({url, timeoutMs, maxBytes, std::move(done)});
    Q_EMIT waitingChanged();
    if (!timer.isActive()) {
        next();
    }
}

void ArxivQueue::next() {
    if (pending.empty()) {
        return;
    }
    QElapsedTimer& last = lastRequest();
    if (last.isValid() && last.elapsed() < gap) {
        timer.start(static_cast<int>(gap - last.elapsed()));
        return;
    }
    Request r = std::move(pending.front());
    pending.pop_front();
    last.start();
    Q_EMIT waitingChanged();
    NetFetch::instance().get(r.url, r.timeoutMs, r.maxBytes, std::move(r.done));
    if (!pending.empty()) {
        timer.start(gap);
    }
}

}  // namespace xqt
