/*
 * xournal-qt: the command line tool (xournal-qt-cli), for scripts that work on many documents.
 *
 * @license GNU GPLv2 or later
 */
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include "config-test.h"
#include "support/TestSupport.h"

using xqt::test::fixturePath;

namespace {
QString cli() { return QCoreApplication::applicationDirPath() + "/xournal-qt-cli"; }

}  // namespace

TEST(Cli, exportsManyDocumentsToAFolderAtOnce) {
    ASSERT_TRUE(QFileInfo::exists(cli())) << cli().toStdString();
    QTemporaryDir out;
    ASSERT_TRUE(out.isValid());
    const QString target = out.filePath("pdfs");

    QProcess process;
    process.start(cli(), {"--pdf-dir=" + target, fixturePath(u8"load/pages.xopp"), fixturePath(u8"load/strokes.xopp")});
    ASSERT_TRUE(process.waitForFinished(60000));
    EXPECT_EQ(process.exitCode(), 0) << process.readAllStandardError().toStdString();
    EXPECT_TRUE(QFileInfo::exists(target + "/pages.pdf"));
    EXPECT_TRUE(QFileInfo::exists(target + "/strokes.pdf"));
    EXPECT_GT(QFileInfo(target + "/pages.pdf").size(), 100);

    // A file that is not there is counted, the others are still exported
    QProcess withBroken;
    withBroken.start(cli(), {"--pdf-dir=" + out.filePath("more"), fixturePath(u8"load/pages.xopp"),
                             out.filePath("nothing.xopp")});
    ASSERT_TRUE(withBroken.waitForFinished(60000));
    EXPECT_NE(withBroken.exitCode(), 0) << "it says that one failed";
    EXPECT_TRUE(QFileInfo::exists(out.filePath("more") + "/pages.pdf"));
}

TEST(Cli, exportsAPageRangeOfOneDocument) {
    QTemporaryDir out;
    ASSERT_TRUE(out.isValid());
    QProcess process;
    process.start(cli(), {fixturePath(u8"load/pages.xopp"), "--create-pdf=" + out.filePath("two.pdf"),
                          "--export-range=1-2"});
    ASSERT_TRUE(process.waitForFinished(60000));
    EXPECT_EQ(process.exitCode(), 0) << process.readAllStandardError().toStdString();
    EXPECT_TRUE(QFileInfo::exists(out.filePath("two.pdf")));
}

TEST(Cli, exportsPagesAsPicturesNamedAsTheAppNamesThem) {
    QTemporaryDir out;
    ASSERT_TRUE(out.isValid());
    QProcess process;
    process.start(cli(), {"--png-dir=" + out.filePath("pictures"), "--export-range=1,2", "--export-png-dpi=50",
                          fixturePath(u8"load/pages.xopp")});
    ASSERT_TRUE(process.waitForFinished(60000));
    EXPECT_EQ(process.exitCode(), 0) << process.readAllStandardError().toStdString();
    // (qt/docs/features/page-files.md: "name-p001.png", the page's number with at least three digits)
    EXPECT_TRUE(QFileInfo::exists(out.filePath("pictures/pages-p001.png")));
    EXPECT_TRUE(QFileInfo::exists(out.filePath("pictures/pages-p002.png")));
    EXPECT_FALSE(QFileInfo::exists(out.filePath("pictures/pages-p003.png")));
}

// Plugin commands without a window (qt/cli/PluginRun.cpp): permissions only by --allow, dialog fields by --set, a live
// dialog answered at once; the result written where it is told
TEST(Cli, runsAPluginCommandOnADocument) {
    QTemporaryDir out;
    ASSERT_TRUE(out.isValid());
    QProcess list;
    list.start(cli(), {"plugin", "list"});
    ASSERT_TRUE(list.waitForFinished(60000));
    EXPECT_EQ(list.exitCode(), 0) << list.readAllStandardError().toStdString();
    EXPECT_TRUE(list.readAllStandardOutput().contains("org.xournalqt.function-plotter"));

    // Not allowed: nothing written
    QProcess refused;
    refused.start(cli(), {"plugin", "run", "org.xournalqt.function-plotter", "plot", "-o", out.filePath("no.xopp")});
    ASSERT_TRUE(refused.waitForFinished(60000));
    EXPECT_NE(refused.exitCode(), 0);
    EXPECT_TRUE(refused.readAllStandardError().contains("--allow edit"));
    EXPECT_FALSE(QFileInfo::exists(out.filePath("no.xopp")));

    // A plot on a copy of a document, its function from --set
    QProcess run;
    run.start(cli(), {"plugin", "run", "org.xournalqt.function-plotter", "plot", fixturePath(u8"load/pages.xopp"),
                      "--allow", "edit", "--set", "f0_expr=sin(x)", "--set", "exact=true", "--out-dir",
                      out.filePath("plots")});
    ASSERT_TRUE(run.waitForFinished(60000));
    EXPECT_EQ(run.exitCode(), 0) << run.readAllStandardError().toStdString();
    const QString written = out.filePath("plots/pages.xopp");
    ASSERT_TRUE(QFileInfo::exists(written));
    QProcess gz;
    gz.start("gzip", {"-dc", written});
    ASSERT_TRUE(gz.waitForFinished(60000));
    const QByteArray xml = gz.readAllStandardOutput();
    EXPECT_TRUE(xml.contains("\\sin")) << "the formula";
    EXPECT_TRUE(xml.contains("xqt-group=")) << "one group";
    EXPECT_TRUE(xml.contains("xqt-data=")) << "the plot's description";

    // A formula with a mistake: an error, nothing written
    QProcess bad;
    bad.start(cli(), {"plugin", "run", "org.xournalqt.function-plotter", "plot", "--allow", "edit", "--set",
                      "f0_expr=sni(x)", "-o", out.filePath("bad.xopp")});
    ASSERT_TRUE(bad.waitForFinished(60000));
    EXPECT_NE(bad.exitCode(), 0);
    EXPECT_TRUE(bad.readAllStandardError().contains("did you mean sin"));
    EXPECT_FALSE(QFileInfo::exists(out.filePath("bad.xopp")));
}
