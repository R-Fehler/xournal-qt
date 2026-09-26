/*
 * xournal-qt: arXiv (qt/docs/citations.md) - opt-in networking, one request every 3 s, the search and a paper
 * downloaded into the library by its title. The network is a fake: no test touches it.
 *
 * @license GNU GPLv2 or later
 */
#include <functional>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include "session/AppContext.h"
#include "shell/Citations.h"
#include "shell/Library.h"
#include "shell/LibraryModel.h"
#include "shell/NetFetch.h"
#include "shell/SettingsModel.h"

#include "../ArxivSamples.h"
#include "../CitationPdfs.h"
#include "../FakeNet.h"

using namespace xqt;

namespace {
void until(const std::function<bool()>& done, int ms = 5000) {
    QElapsedTimer t;
    t.start();
    while (!done() && t.elapsed() < ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
}

QByteArray fileBytes(const QString& path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

class ArxivTest: public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(tmp.isValid());
        root = fs::path(tmp.filePath("Library").toStdString());
        fs::create_directories(root / "Papers");
        app = std::make_unique<AppContext>(fs::path(XQT_BUILD_RESOURCE_DIR),
                                           fs::path(tmp.filePath("settings.xml").toStdString()), 1);
        settings = std::make_unique<SettingsModel>(*app);
        library.setLibrary(std::make_unique<Library>(root));
        citations = std::make_unique<Citations>(*app->getSettings(), &library);
        ArxivQueue::setInterval(0);  // (the spacing has a test of its own)
        // A real PDF as arXiv's answer to a PDF request
        const std::string pdf = tmp.filePath("answer.pdf").toStdString();
        test::makePaper(pdf, "Attention Is All You Need", "Attention Is All You Need", "Ashish Vaswani");
        pdfBytes = fileBytes(QString::fromStdString(pdf));
        net.answer = [this](const QUrl& url) {
            if (url.host() == "export.arxiv.org") {
                return test::FakeNet::ok(url.query().contains("id_list=1706.00000") ? test::ARXIV_ERROR
                                                                                    : test::ARXIV_SEARCH);
            }
            if (url.path().startsWith("/pdf/2010")) {
                return test::FakeNet::ok("<html>withdrawn</html>");  // (not a PDF)
            }
            return test::FakeNet::ok(pdfBytes);
        };
    }
    void TearDown() override { ArxivQueue::setInterval(3000); }
    void search() {
        ASSERT_TRUE(citations->arxivSearch("Attention is all you need"));
        until([&] { return !citations->arxivBusy(); });
        ASSERT_EQ(citations->arxivResults().size(), 3);
    }

    QTemporaryDir tmp;
    fs::path root;
    test::FakeNet net;
    std::unique_ptr<AppContext> app;
    std::unique_ptr<SettingsModel> settings;
    LibraryModel library;
    std::unique_ptr<Citations> citations;
    QByteArray pdfBytes;
};
}  // namespace

// Nothing is sent before networking was allowed (the first use asks: the setting is "ask"), nor when it is off.
TEST_F(ArxivTest, nothingIsSentUnlessNetworkingIsAllowed) {
    EXPECT_EQ(citations->networkAccess(), "ask") << "not decided yet";
    EXPECT_FALSE(citations->arxivSearch("Attention is all you need"));
    EXPECT_FALSE(citations->arxivLookUp("1706.03762"));
    EXPECT_FALSE(citations->arxivError().isEmpty()) << "says why";
    settings->set("networkAccess", "off");
    EXPECT_FALSE(citations->arxivSearch("Attention is all you need"));
    EXPECT_TRUE(citations->arxivError().contains("Settings"));
    QCoreApplication::processEvents();
    EXPECT_TRUE(net.calls.empty()) << "not a single request";

    settings->set("networkAccess", "on");
    EXPECT_TRUE(citations->arxivSearch("Attention is all you need"));
    until([&] { return !citations->arxivBusy(); });
    ASSERT_EQ(net.calls.size(), 1u);
    EXPECT_EQ(net.calls[0].url.toString(QUrl::FullyEncoded), citations->arxivSearchUrl("Attention is all you need"))
            << "exactly the address shown";
}

TEST_F(ArxivTest, theSearchListsArxivsAnswer) {
    settings->set("networkAccess", "on");
    search();
    const QVariantMap first = citations->arxivResults().front().toMap();
    EXPECT_EQ(first["full"].toString(), "1706.03762v7");
    EXPECT_EQ(first["title"].toString(), "Attention Is All You Need");
    EXPECT_EQ(first["authors"].toString(), "Ashish Vaswani, Noam Shazeer, Niki Parmar et al.");
    EXPECT_EQ(first["year"].toString(), "2017");
    EXPECT_EQ(first["pdfUrl"].toString(), "https://arxiv.org/pdf/1706.03762v7");
    EXPECT_EQ(first["fileName"].toString(), "Attention Is All You Need (1706.03762).pdf");
    EXPECT_EQ(citations->arxivError(), "");

    // An ID: its entry; arXiv's error is said; what is no ID is not asked about
    EXPECT_EQ(citations->arxivLookUpUrl("1706.03762v7"), "https://export.arxiv.org/api/query?id_list=1706.03762v7");
    const size_t before = net.calls.size();
    EXPECT_FALSE(citations->arxivLookUp("1234.5"));
    EXPECT_EQ(net.calls.size(), before);
    EXPECT_TRUE(citations->arxivLookUp("1706.00000"));
    until([&] { return !citations->arxivBusy(); });
    EXPECT_EQ(citations->arxivError(), "incorrect id format for 1234.5") << "(the saved answer)";
    EXPECT_TRUE(citations->arxivResults().isEmpty());
}

// The PDF goes into the chosen folder of the library, named by the paper's title; the same paper again is not
// downloaded twice; an answer that is not a PDF is an error.
TEST_F(ArxivTest, aPaperIsDownloadedIntoTheLibraryNamedByItsTitle) {
    settings->set("networkAccess", "on");
    search();
    const size_t before = net.calls.size();
    EXPECT_FALSE(citations->downloadExists(0, "Papers"));
    const QString target = citations->downloadPath(0, "Papers");
    EXPECT_EQ(target, QString::fromStdString((root / "Papers" / "Attention Is All You Need (1706.03762).pdf").string()));
    ASSERT_TRUE(citations->arxivDownload(0, "Papers"));
    until([&] { return !citations->arxivBusy(); });
    EXPECT_EQ(citations->arxivError(), "");
    ASSERT_EQ(net.calls.size(), before + 1);
    EXPECT_EQ(net.calls.back().url.toString(), "https://arxiv.org/pdf/1706.03762v7");
    EXPECT_EQ(citations->downloadedPath(), target);
    EXPECT_EQ(fileBytes(target), pdfBytes) << "the PDF as it came";
    EXPECT_TRUE(citations->downloadExists(0, "Papers"));
    // Indexed like any new document: found by its title
    until([&] { return library.searchIndex() && !library.indexing() &&
                       !library.searchIndex()->findTitle("Attention is all you need", "", 1).empty(); });
    ASSERT_NE(library.searchIndex(), nullptr);
    EXPECT_FALSE(library.searchIndex()->findTitle("Attention is all you need", "", 1).empty());

    // Again: there already, nothing fetched
    ASSERT_TRUE(citations->arxivDownload(0, "Papers"));
    EXPECT_EQ(net.calls.size(), before + 1);
    EXPECT_EQ(citations->downloadedPath(), target);

    // Not a PDF: an error, no file
    ASSERT_TRUE(citations->arxivDownload(1, ""));
    until([&] { return !citations->arxivBusy(); });
    EXPECT_TRUE(citations->arxivError().contains("PDF")) << citations->arxivError().toStdString();
    EXPECT_FALSE(citations->downloadExists(1, ""));
}

TEST_F(ArxivTest, networkErrorsAreSaid) {
    settings->set("networkAccess", "on");
    net.answer = [](const QUrl& url) {
        NetFetch::Reply r;
        r.error = QStringLiteral("%1 did not answer within 20 s.").arg(url.host());
        return r;
    };
    EXPECT_TRUE(citations->arxivSearch("Attention is all you need"));
    until([&] { return !citations->arxivBusy(); });
    EXPECT_EQ(citations->arxivError(), "export.arxiv.org did not answer within 20 s.");
    EXPECT_TRUE(citations->arxivResults().isEmpty());
}

// arXiv's rule: no more than one request every three seconds (here 300 ms), in the order asked.
TEST(ArxivQueueTest, requestsAreSpaced) {
    test::FakeNet net;
    net.answer = [](const QUrl&) { return test::FakeNet::ok(test::ARXIV_EMPTY); };
    ArxivQueue::setInterval(300);
    ArxivQueue queue;
    std::vector<int> answered;
    for (int i = 0; i < 3; ++i) {
        queue.get(QUrl(QStringLiteral("https://export.arxiv.org/api/query?id_list=%1").arg(i)), 1000, 1000,
                  [&answered, i](const NetFetch::Reply&) { answered.push_back(i); });
    }
    EXPECT_TRUE(queue.waiting()) << "the others wait their turn";
    until([&] { return answered.size() == 3; });
    ASSERT_EQ(answered, std::vector<int>({0, 1, 2}));
    ASSERT_EQ(net.calls.size(), 3u);
    EXPECT_GE(net.calls[1].atMs - net.calls[0].atMs, 290);
    EXPECT_GE(net.calls[2].atMs - net.calls[1].atMs, 290);
    EXPECT_FALSE(queue.waiting());
    ArxivQueue::setInterval(3000);
    EXPECT_TRUE(NetFetch::userAgent().startsWith("xournal-qt/")) << "a user agent that names the app";
}
