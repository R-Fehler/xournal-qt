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

namespace {
QString cli() { return QCoreApplication::applicationDirPath() + "/xournal-qt-cli"; }

QString fixture(const char8_t* rel) {
    const auto p = GET_TESTFILE(rel);
    return QString::fromUtf8(reinterpret_cast<const char*>(p.c_str()));
}
}  // namespace

TEST(Cli, exportsManyDocumentsToAFolderAtOnce) {
    ASSERT_TRUE(QFileInfo::exists(cli())) << cli().toStdString();
    QTemporaryDir out;
    ASSERT_TRUE(out.isValid());
    const QString target = out.filePath("pdfs");

    QProcess process;
    process.start(cli(), {"--pdf-dir=" + target, fixture(u8"load/pages.xopp"), fixture(u8"load/strokes.xopp")});
    ASSERT_TRUE(process.waitForFinished(60000));
    EXPECT_EQ(process.exitCode(), 0) << process.readAllStandardError().toStdString();
    EXPECT_TRUE(QFileInfo::exists(target + "/pages.pdf"));
    EXPECT_TRUE(QFileInfo::exists(target + "/strokes.pdf"));
    EXPECT_GT(QFileInfo(target + "/pages.pdf").size(), 100);

    // A file that is not there is counted, the others are still exported
    QProcess withBroken;
    withBroken.start(cli(), {"--pdf-dir=" + out.filePath("more"), fixture(u8"load/pages.xopp"),
                             out.filePath("nothing.xopp")});
    ASSERT_TRUE(withBroken.waitForFinished(60000));
    EXPECT_NE(withBroken.exitCode(), 0) << "it says that one failed";
    EXPECT_TRUE(QFileInfo::exists(out.filePath("more") + "/pages.pdf"));
}

TEST(Cli, exportsAPageRangeOfOneDocument) {
    QTemporaryDir out;
    ASSERT_TRUE(out.isValid());
    QProcess process;
    process.start(cli(), {fixture(u8"load/pages.xopp"), "--create-pdf=" + out.filePath("two.pdf"),
                          "--export-range=1-2"});
    ASSERT_TRUE(process.waitForFinished(60000));
    EXPECT_EQ(process.exitCode(), 0) << process.readAllStandardError().toStdString();
    EXPECT_TRUE(QFileInfo::exists(out.filePath("two.pdf")));
}
