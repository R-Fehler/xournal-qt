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
