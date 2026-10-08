/*
 * xournal-qt: the handwriting model that comes with the app (qt/docs/features/handwriting-search.md, "The built-in
 * model"): where ONNX Runtime is looked for on each platform, the models the build puts into the resource dir,
 * `xournal-qt --hwr-info`'s report, and (with XQT_ONNXRUNTIME=<path of libonnxruntime.so.1>) the built-in model
 * reading real handwriting.
 *
 * @license GNU GPLv2 or later
 */
#include <QStringList>
#include <gtest/gtest.h>

#include "hwr/OrtRuntime.h"

using namespace xqt;
using namespace xqt::hwr;

// ONNX Runtime where each platform's package puts it: XQT_ONNXRUNTIME alone when set; Linux in lib/xournal-qt beside
// bin/, Windows next to the program, macOS in the bundle's Frameworks, Android by its name (the APK's native
// libraries); the system's last
TEST(OrtRuntimeTest, theRuntimeIsLookedForWhereEachPackagePutsIt) {
    using ort::Platform;
    const QString app = QStringLiteral("/app/bin");
    EXPECT_EQ(ort::candidates(Platform::Linux, app, QString()),
              (QStringList{QStringLiteral("/app/bin/../lib/xournal-qt/libonnxruntime.so.1"),
                           QStringLiteral("/app/bin/libonnxruntime.so.1"), QStringLiteral("libonnxruntime.so.1")}));
    EXPECT_EQ(ort::candidates(Platform::Windows, QStringLiteral("C:/xournal-qt/bin"), QString()),
              (QStringList{QStringLiteral("C:/xournal-qt/bin/../lib/xournal-qt/onnxruntime.dll"),
                           QStringLiteral("C:/xournal-qt/bin/onnxruntime.dll"), QStringLiteral("onnxruntime.dll")}));
    const QString mac = QStringLiteral("/Applications/xournal-qt.app/Contents/MacOS");
    EXPECT_EQ(ort::candidates(Platform::MacOS, mac, QString()),
              (QStringList{mac + QStringLiteral("/../Frameworks/libonnxruntime.1.dylib"),
                           mac + QStringLiteral("/../lib/xournal-qt/libonnxruntime.1.dylib"),
                           mac + QStringLiteral("/libonnxruntime.1.dylib"), QStringLiteral("libonnxruntime.1.dylib")}));
    EXPECT_EQ(ort::candidates(Platform::Android, QStringLiteral("/data/app/x/lib/arm64"), QString()),
              QStringList{QStringLiteral("libonnxruntime.so")});
    // The variable wins, on every platform
    for (const Platform p: {Platform::Linux, Platform::Windows, Platform::MacOS, Platform::Android}) {
        EXPECT_EQ(ort::candidates(p, app, QStringLiteral("/opt/ort/libonnxruntime.so.1.30.0")),
                  QStringList{QStringLiteral("/opt/ort/libonnxruntime.so.1.30.0")});
    }
    // Without the program's folder: the system's only
    EXPECT_EQ(ort::candidates(Platform::Linux, QString(), QString()), QStringList{QStringLiteral("libonnxruntime.so.1")});
#if defined(__linux__) && !defined(__ANDROID__)
    EXPECT_EQ(ort::thisPlatform(), Platform::Linux);
#endif
}
