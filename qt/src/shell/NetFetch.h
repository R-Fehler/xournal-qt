/*
 * xournal-qt: the app's own network requests (qt/docs/citations.md): arXiv's search and its PDFs. Opt-in (Settings:
 * `networkAccess`), and every address is shown before it is fetched.
 *
 * NetFetch is the one way out: tests replace it (setInstance) so that no test touches the network. The real one uses
 * QNetworkAccessManager, which works asynchronously (nothing waits on the UI thread), with a timeout per request, a
 * user agent that names the app, and a size limit.
 *
 * ArxivQueue keeps to arXiv's API rules: at most one request every 3 seconds (a queue; a request that has to wait
 * says so).
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <deque>
#include <functional>

#include <QByteArray>
#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QUrl>

class QNetworkAccessManager;

namespace xqt {

class NetFetch {
public:
    struct Reply {
        QUrl url;
        int status = 0;    ///< HTTP status (0: no answer)
        QByteArray body;
        QString error;     ///< "" when it worked (status 200)
    };
    using Done = std::function<void(const Reply&)>;

    virtual ~NetFetch() = default;
    /// GET `url`; `done` is called later on the calling (UI) thread. `timeoutMs`: no answer, or no progress, for that
    /// long is an error. `maxBytes`: a bigger answer is an error.
    virtual void get(const QUrl& url, int timeoutMs, qint64 maxBytes, Done done) = 0;

    /// "xournal-qt/<version> (+https://github.com/R-Fehler/xournal-qt)"
    static QByteArray userAgent();

    /// The one in use (the real one unless a test set its own).
    static NetFetch& instance();
    /// Use another one (tests: a fake); nullptr: the real one again. The caller keeps ownership.
    static void setInstance(NetFetch* fetch);
};

/// The real one: QNetworkAccessManager.
class QtNetFetch final: public NetFetch {
public:
    QtNetFetch();
    ~QtNetFetch() override;
    void get(const QUrl& url, int timeoutMs, qint64 maxBytes, Done done) override;

private:
    QNetworkAccessManager* manager();
    QNetworkAccessManager* nam = nullptr;
};

/// Requests to arXiv, one every `interval` (3 s by arXiv's rules; tests shorten it), in order.
class ArxivQueue final: public QObject {
    Q_OBJECT
public:
    explicit ArxivQueue(QObject* parent = nullptr);
    /// Queue a request (done as NetFetch::get).
    void get(const QUrl& url, int timeoutMs, qint64 maxBytes, NetFetch::Done done);
    /// A request waits for its turn.
    bool waiting() const { return !pending.empty(); }
    /// The time between two requests (ms); for the whole program.
    static int interval();
    static void setInterval(int ms);

Q_SIGNALS:
    void waitingChanged();

private:
    struct Request {
        QUrl url;
        int timeoutMs;
        qint64 maxBytes;
        NetFetch::Done done;
    };
    void next();
    std::deque<Request> pending;
    QTimer timer;
};

}  // namespace xqt
