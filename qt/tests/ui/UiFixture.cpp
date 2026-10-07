/*
 * xournal-qt: the fixture of the UI tests.
 *
 * @license GNU GPLv2 or later
 */
#include "UiFixture.h"

#include <algorithm>
#include <filesystem>
#include <fstream>

#include <QSignalSpy>
#include <QTest>

#include "EngineSetup.h"
#include "support/TestSupport.h"

namespace xqt::test {

void UiFixture::SetUp() {
    makeController();
    ASSERT_NO_FATAL_FAILURE(loadWindow());
}

void UiFixture::TearDown() { closeApp(); }

void UiFixture::makeController() { controller = std::make_unique<AppController>(); }

void UiFixture::loadWindow(const WindowOptions& options) {
    if (!controller) {
        makeController();
    }
    engine = std::make_unique<QQmlApplicationEngine>();
    setUpEngine(*engine, controller.get());
    engine->loadFromModule("XournalQt", "Main");
    ASSERT_FALSE(engine->rootObjects().isEmpty());
    window = qobject_cast<QQuickWindow*>(engine->rootObjects().first());
    ASSERT_NE(window, nullptr);
    if (options.size.isValid()) {
        window->resize(options.size);
    }
    if (options.activate) {
        window->requestActivate();
    }
    ASSERT_TRUE(QTest::qWaitForWindowExposed(window));
    QTest::mouseMove(window, QPoint(-20, -20));  // (the pointer rests outside: nothing hovered, no tool tips)
}

void UiFixture::closeApp() {
    if (controller) {
        controller->shutdown();  // first the image workers, then the engine that owns their providers
    }
    engine.reset();
    controller.reset();
    window = nullptr;
}

void UiFixture::wait(int ms, std::source_location where) {
    static const QByteArray log = qgetenv("XQT_WAIT_LOG");
    if (!log.isEmpty()) {
        std::ofstream(log.toStdString(), std::ios::app)
                << std::filesystem::path(where.file_name()).filename().string() << ":" << where.line() << " " << ms
                << "\n";
    }
    processEventsFor(ms);
}

bool UiFixture::until(const std::function<bool()>& done, int ms, std::source_location where) const {
    return waitFor(done, ms > 0 ? ms : untilMs, where);
}

bool UiFixture::upTo(const std::function<bool()>& done, int ms) { return waitUpTo(done, ms); }

bool UiFixture::waitOpened(QObject* popup, bool opened, int timeoutMs) {
    return popup && waitUpTo(
                            [&] {
                                return popup->property("opened").toBool() == opened &&
                                       popup->property("visible").toBool() == opened;
                            },
                            timeoutMs);
}

void UiFixture::nextFrame() {
    QSignalSpy drawn(window, &QQuickWindow::frameSwapped);
    window->update();
    drawn.wait(5000);
}

QQuickItem* UiFixture::findUnder(QQuickItem* root, const QString& name, bool shown) {
    if (!root) {
        return nullptr;
    }
    if (root->objectName() == name && (!shown || root->isVisible())) {
        return root;
    }
    for (QQuickItem* c: root->childItems()) {
        if (QQuickItem* f = findUnder(c, name, shown)) {
            return f;
        }
    }
    return nullptr;
}

QQuickItem* UiFixture::findItem(const QString& name, bool shown) const {
    return window ? findUnder(window->contentItem(), name, shown) : nullptr;
}

QQuickItem* UiFixture::findInScene(const QString& name, bool shown) const {
    if (!window) {
        return nullptr;
    }
    QQuickItem* content = window->contentItem();
    QQuickItem* root = content->parentItem() ? content->parentItem() : content;
    return findUnder(root, name, shown);
}

QObject* UiFixture::entryOf(QObject* menu, const QString& name) {
    const int n = menu ? menu->property("count").toInt() : 0;
    for (int i = 0; i < n; ++i) {
        QQuickItem* it = nullptr;
        QMetaObject::invokeMethod(menu, "itemAt", Q_RETURN_ARG(QQuickItem*, it), Q_ARG(int, i));
        if (it && it->objectName() == name) {
            return it;
        }
    }
    return nullptr;
}

void UiFixture::click(QQuickItem* item, Qt::KeyboardModifiers m, Qt::MouseButton button) {
    ASSERT_NE(item, nullptr);
    QTest::mouseClick(window, button, m, item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint());
    wait(50);  // (no state to wait for: what a click starts differs)
}

void UiFixture::key(Qt::Key k, Qt::KeyboardModifiers m) {
    QTest::keyClick(window, k, m);
    wait(20);
}

void UiFixture::type(const char* text) {
    for (const char* c = text; *c; ++c) {
        QTest::keyClick(window, *c);
    }
    wait(20);
}

void UiFixture::scrollIntoView(QQuickItem* item) {
    ASSERT_NE(item, nullptr);
    QQuickItem* flick = item->parentItem();
    while (flick && !flick->inherits("QQuickFlickable")) {
        flick = flick->parentItem();
    }
    if (!flick) {
        return;
    }
    auto* content = flick->property("contentItem").value<QQuickItem*>();
    if (!content) {
        return;
    }
    const qreal y = item->mapToItem(content, QPointF(0, 0)).y();
    const qreal maxY = std::max(0.0, flick->property("contentHeight").toReal() - flick->height());
    flick->setProperty("contentY", std::clamp(y - flick->height() / 3, 0.0, maxY));
    nextFrame();
}

}  // namespace xqt::test
