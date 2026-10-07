/*
 * xournal-qt: the real window on a screen at 125 %, 150 % (qt/docs/hidpi.md). CTest runs these once as they are and
 * once more with QT_SCALE_FACTOR=1.5 (FractionalScale.ui@150), as Qt runs on a screen at 150 % (Wayland's
 * fractional-scale-v1, X11's Xft.dpi, Windows' per-monitor DPI): the window's device pixel ratio is 1.5.
 *
 * @license GNU GPLv2 or later
 */
#include <cmath>
#include <functional>

#include <QColor>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTest>
#include <gtest/gtest.h>

#include "shell/AnnotationsModel.h"
#include "shell/HitPages.h"
#include "shell/MdSnippets.h"
#include "shell/PageSketches.h"
#include "shell/DocumentCovers.h"
#include "shell/RecentFiles.h"
#include "shell/Thumbnails.h"

#include "AppController.h"
#include "UiFixture.h"
#include "config-test.h"
#include "support/TestSupport.h"

using xqt::test::fixturePath;

namespace {

QQuickItem* itemAt(QQuickItem* view, int row) {
    QQuickItem* item = nullptr;
    QMetaObject::invokeMethod(view, "itemAtIndex", Q_RETURN_ARG(QQuickItem*, item), Q_ARG(int, row));
    return item;
}

template <typename T = QQuickItem>
T* childNamed(QQuickItem* item, const QString& name) {
    for (auto* c: item->findChildren<T*>()) {
        if (c->objectName() == name) {
            return c;
        }
    }
    return nullptr;
}

class FractionalScale: public xqt::test::UiFixture {
protected:
    void SetUp() override {
        makeController();
        qobject_cast<xqt::RecentFiles*>(controller->recentModel())->clear();
        QMetaObject::invokeMethod(controller->settingsModel(), "resetLayoutChoices");
        ASSERT_NO_FATAL_FAILURE(loadWindow());
        wait(100);
    }

};
}  // namespace

// A page in the sidebar is drawn with the pixels of its frame on the screen: its width times the device pixel ratio,
// once. Qt Quick multiplies an image provider's sourceSize by the pixel ratio itself; the QML multiplied it as well,
// so at 150 % the thumbnails were drawn 2.25 times as wide as their frame (5 times the pixels), at 200 % 4 times.
TEST_F(FractionalScale, sidebarThumbnailsAreDrawnWithThePixelsOfTheirFrame) {
    const double dpr = window->effectiveDevicePixelRatio();
    ASSERT_TRUE(controller->openPath(fixturePath(u8"load/pages.xopp")));
    wait(100);
    QMetaObject::invokeMethod(window, "showSidebar", Q_ARG(QVariant, true));
    auto* list = find<QQuickItem>("sidebarList");
    ASSERT_NE(list, nullptr);
    ASSERT_TRUE(until([&] { return itemAt(list, 0) != nullptr; }));
    QQuickItem* picture = childNamed(itemAt(list, 0), "sidebarThumbnail");
    ASSERT_NE(picture, nullptr);
    QQuickItem* sharp = childNamed(picture, "pageSharp");
    ASSERT_NE(sharp, nullptr);
    ASSERT_TRUE(until([&] { return picture->property("sharpShown").toBool(); })) << "the sharp thumbnail is shown";

    const double frame = picture->parentItem()->width();
    // (the image's implicit size is its pixels over the pixel ratio; the provider rounds the width up to its steps)
    const double pixels = sharp->implicitWidth() * dpr;
    EXPECT_GE(pixels, frame * dpr - 1) << "sharp on this screen";
    EXPECT_LT(pixels, frame * dpr + xqt::ThumbnailProvider::WIDTH_STEP)
            << "drawn " << pixels << " pixels wide for a frame of " << frame * dpr << " pixels (dpr " << dpr << ")";
}

namespace {
/// The lines of the window's own QML (Rectangles a pixel thin, and frames of square Rectangles) whose thickness is
/// not a whole number of device pixels: at 125 % or 150 % they come out 1 or 2 pixels thick, depending on where they
/// land, so lines of the same kind look uneven.
QStringList unevenLines(QQuickItem* root, double dpr) {
    QStringList found;
    const auto whole = [&](double logical) {
        const double px = logical * dpr;
        return std::abs(px - std::round(px)) < 0.01;
    };
    std::function<void(QQuickItem*)> walk = [&](QQuickItem* item) {
        if (!item->isVisible() || item->opacity() <= 0.01) {
            return;
        }
        const QQmlContext* context = qmlContext(item);
        // (the app's own QML; Qt's controls draw their separators themselves, see qt/docs/hidpi.md)
        const bool own = context && context->baseUrl().toString().startsWith("qrc:/qt/qml/XournalQt/");
        if (own && QString(item->metaObject()->className()).startsWith("QQuickRectangle")) {
            // (its file, and the nearest named item above it)
            QString near;
            for (QQuickItem* up = item->parentItem(); up && near.isEmpty(); up = up->parentItem()) {
                near = up->objectName();
            }
            const QString name = context->baseUrl().fileName() + " near " + near + " (" +
                                 item->property("color").value<QColor>().name() + ")";
            const double w = item->width(), h = item->height();
            if ((h > 0 && h <= 1.01 && w >= 8 && !whole(h)) || (w > 0 && w <= 1.01 && h >= 8 && !whole(w))) {
                found << QStringLiteral("line in %1 (%2 x %3)").arg(name).arg(w).arg(h);
            }
            // (a border is drawn once its width or colour is set; unset it is 1 and black)
            const auto* border = item->property("border").value<QObject*>();
            const double bw = border ? border->property("width").toDouble() : 0;
            const bool drawn = border && (bw != 1.0 || border->property("color").value<QColor>() != QColor(Qt::black));
            if (drawn && bw > 0 && item->property("radius").toDouble() == 0 && w >= 8 && h >= 8 && !whole(bw)) {
                found << QStringLiteral("frame of %1 (%2)").arg(name).arg(bw);
            }
        }
        for (QQuickItem* child: item->childItems()) {
            walk(child);
        }
    };
    walk(root);
    found.removeDuplicates();
    return found;
}
}  // namespace

// The separators and frames of the window are whole device pixels thick (1 at 125 %, 150 % and 175 %, 2 at 200 %), so
// they look alike wherever they are, as at 100 %.
TEST_F(FractionalScale, linesAndFramesOfTheWindowAreWholeDevicePixels) {
    const double dpr = window->effectiveDevicePixelRatio();
    QQuickItem* root = window->contentItem();  // (the root: with the overlay, menus and sheets)
    QStringList found = unevenLines(root, dpr);
    // A document with the page sidebar, the page grid, the settings
    ASSERT_TRUE(controller->openPath(fixturePath(u8"load/pages.xopp")));
    wait(100);
    QMetaObject::invokeMethod(window, "showSidebar", Q_ARG(QVariant, true));
    wait(300);
    found << unevenLines(root, dpr);
    QTest::keyClick(window, Qt::Key_G, Qt::ControlModifier | Qt::AltModifier);
    wait(400);
    found << unevenLines(root, dpr);
    QTest::keyClick(window, Qt::Key_G, Qt::ControlModifier | Qt::AltModifier);
    QTest::keyClick(window, Qt::Key_Comma, Qt::ControlModifier);
    wait(500);
    found << unevenLines(root, dpr);
    found.removeDuplicates();
    EXPECT_TRUE(found.isEmpty()) << found.join("\n").toStdString();
}
