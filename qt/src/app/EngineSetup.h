/*
 * xournal-qt: what the QML engine of the app's windows has before Main.qml is loaded. main.cpp and the UI tests' fixture
 * (qt/tests/ui/UiFixture.h) both use it, so the tests run with the engine the app has.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

class QQmlEngine;
class QObject;

namespace xqt {

/// The image providers of the window's pictures (thumbnails, sketches, covers, search hits, Markdown snippets,
/// annotations; the engine owns them) and `app`, the window's AppController, as the root context's property.
void setUpEngine(QQmlEngine& engine, QObject* app);

}  // namespace xqt
