#include "LayoutWalk.h"

#include <functional>

#include <QQuickItem>
#include <QQuickWindow>

namespace xqt::uitest {

const WindowSize auditSizes[18] = {
        {1920, 1080, "desktop-fhd"},        {1366, 768, "laptop"},           {1280, 800, "laptop-16x10"},
        {1024, 700, "small-desktop"},       {800, 600, "tiny-desktop"},      {600, 800, "narrow-tall"},
        {412, 915, "phone-portrait"},       {915, 412, "phone-landscape"},   {900, 1000, "fold7-inner"},
        {1280, 500, "short-wide"},          {960, 1392, "surface-200-portrait"}, {1440, 912, "surface-200-landscape"},
        {1280, 1872, "surface-150-portrait"}, {1920, 1232, "surface-150-landscape"},
        {864, 1488, "2in1-150-portrait"},   {720, 1232, "2in1-125-portrait"}, {1536, 816, "2in1-landscape"},
        {1280, 672, "2in1-125-landscape"},
};

QString labelOf(QQuickItem* i) {
    if (!i->objectName().isEmpty()) {
        return i->objectName();
    }
    for (const char* p: {"iconName", "text", "tip"}) {
        const QString v = i->property(p).toString();
        if (!v.isEmpty()) {
            return QString(p) + "=" + v.left(24);
        }
    }
    return QString::fromLatin1(i->metaObject()->className());
}

QStringList LayoutFindings::outsideNames() const {
    QStringList names;
    for (const QString& o: outside) {
        names << o.section('@', 0, 0);
    }
    names.removeDuplicates();
    return names;
}

LayoutFindings walkLayout(QQuickWindow* window, int minTarget) {
    LayoutFindings f;
    const QRectF win(0, 0, window->width(), window->height());
    // Every visible item: the part of it that is shown (clipping ancestors cut it) and where it is
    std::function<void(QQuickItem*, QRectF)> walk = [&](QQuickItem* i, QRectF clip) {
        if (!i->isVisible() || i->opacity() <= 0.01) {
            return;
        }
        const QRectF r = i->mapRectToScene(QRectF(0, 0, i->width(), i->height()));
        const bool button = i->inherits("QQuickAbstractButton");
        const bool control = button || i->inherits("QQuickTextInput") || i->inherits("QQuickTextEdit") ||
                             i->inherits("QQuickTextField") || i->inherits("QQuickComboBox") ||
                             i->inherits("QQuickSpinBox");
        if (control && r.width() > 1 && r.height() > 1 && i->isEnabled()) {
            const QRectF shown = r.intersected(clip);
            if (button) {
                ++f.buttons;
            }
            if (shown.width() < r.width() - 2 || shown.height() < r.height() - 2) {
                // cut by a scrolling (clipping) area: hidden until scrolled
                if (!clip.contains(win)) {
                    f.hidden << labelOf(i);
                }
            }
            if (!win.contains(shown.adjusted(1, 1, -1, -1)) && !shown.isEmpty()) {
                f.outside << QString("%1@%2,%3").arg(labelOf(i)).arg(int(shown.right())).arg(int(shown.bottom()));
            }
            if (button && !shown.isEmpty() && (r.width() < minTarget || r.height() < minTarget)) {
                f.small << QString("%1(%2x%3)").arg(labelOf(i)).arg(int(r.width())).arg(int(r.height()));
            }
        }
        const QRectF inner = i->clip() ? clip.intersected(r) : clip;
        for (QQuickItem* c: i->childItems()) {
            walk(c, inner);
        }
    };
    walk(window->contentItem(), QRectF(-1e6, -1e6, 2e6, 2e6));
    f.outside.removeDuplicates();
    f.hidden.removeDuplicates();
    f.small.removeDuplicates();
    return f;
}

}  // namespace xqt::uitest
