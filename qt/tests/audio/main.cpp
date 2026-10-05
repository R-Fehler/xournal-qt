/*
 * xournal-qt: test runner for the audio recordings (qt/docs/audio.md). A QCoreApplication for the timers of the fake
 * devices and queued calls; config, cache and data folders in a temporary folder, so nothing touches the user's.
 *
 * @license GNU GPLv2 or later
 */
#include <clocale>

#include <QCoreApplication>
#include <QTemporaryDir>
#include <gtest/gtest.h>

int main(int argc, char* argv[]) {
    QTemporaryDir home;
    qputenv("XDG_CACHE_HOME", (home.path() + "/cache").toUtf8());
    qputenv("XDG_CONFIG_HOME", (home.path() + "/config").toUtf8());
    qputenv("XDG_DATA_HOME", (home.path() + "/data").toUtf8());
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("xournal-qt-tests"));
    QCoreApplication::setApplicationName(QStringLiteral("xournal-qt-audio-tests"));
    setlocale(LC_NUMERIC, "C");
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
