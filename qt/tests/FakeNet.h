/*
 * xournal-qt: the network for tests (qt/docs/citations.md): answers what the test says, records what was asked
 * and when. Installed with NetFetch::setInstance, so no test touches the network.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <functional>
#include <vector>

#include <QElapsedTimer>
#include <QTimer>

#include "shell/NetFetch.h"

namespace xqt::test {

struct FakeNet: NetFetch {
    struct Call {
        QUrl url;
        qint64 atMs = 0;  ///< since the fake was made
    };
    std::vector<Call> calls;
    /// The answer to a request (default: 404)
    std::function<Reply(const QUrl&)> answer;
    QElapsedTimer clock;

    FakeNet() {
        clock.start();
        NetFetch::setInstance(this);
    }
    ~FakeNet() override { NetFetch::setInstance(nullptr); }

    void get(const QUrl& url, int, qint64, Done done) override {
        calls.push_back({url, clock.elapsed()});
        Reply r;
        if (answer) {
            r = answer(url);
        } else {
            r.status = 404;
            r.error = QStringLiteral("not found");
        }
        r.url = url;
        QTimer::singleShot(0, [r, done = std::move(done)] { done(r); });  // (later, as the network answers)
    }

    static Reply ok(const QByteArray& body) {
        Reply r;
        r.status = 200;
        r.body = body;
        return r;
    }
};

}  // namespace xqt::test
