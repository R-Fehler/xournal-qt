/*
 * xournal-qt: the snip tool of the window (qt/snip, qt/docs/features/snip.md): armed from its button (a fixed tool of
 * the rail, qt/copy-tools), the image button's list or a toolbox entry, the next rectangle or lasso on a page (Snip.h,
 * CanvasView::snip) puts its picture on the clipboard, with a link to where it came from; then the tool used before
 * comes back. Pasted into a document of the app, the window offers to add that link next to the picture
 * (CanvasView::addSnipLink). "Copy handwriting as text" is armed the same way (AppInkCopy.cpp).
 *
 * @license GNU GPLv2 or later
 */
#include <cmath>

#include <QBuffer>
#include <QClipboard>
#include <QGuiApplication>
#include <QMimeData>

#include "control/ToolHandler.h"
#include "control/settings/Settings.h"
#include "session/AppContext.h"
#include "session/DocumentLink.h"
#include "session/DocumentSession.h"
#include "shell/DocumentLinks.h"
#include "shell/ReferenceMode.h"

#include "AppController.h"
#include "CanvasView.h"
#include "Snip.h"

using namespace xqt;

QString AppController::snipShape() const {
    if (snip::purpose() != snip::Purpose::Picture) {
        return {};  // (copying handwriting as text: inkCopy)
    }
    switch (snip::armed()) {
        case snip::Shape::Rectangle:
            return QStringLiteral("rect");
        case snip::Shape::Lasso:
            return QStringLiteral("lasso");
        default:
            return {};
    }
}

bool AppController::inkCopyArmed() const { return snip::isArmed() && snip::purpose() == snip::Purpose::InkText; }

void AppController::startSnip(const QString& shape) {
    armSnip(shape == QLatin1String("lasso"), static_cast<int>(snip::Purpose::Picture));
}

void AppController::armSnip(bool lasso, int purpose) {
    if (!snip::isArmed()) {
        snipPreviousTool = tool();
    }
    // (a press on a selection would move it: the snip starts with none)
    if (canvas()) {
        canvas()->clearSelection();
        canvas()->clearPdfTextSelection();
    }
    if (auto* ref = qobject_cast<CanvasView*>(referenceMode->view())) {
        ref->clearSelection();
        ref->clearPdfTextSelection();
    }
    snipTool = lasso ? TOOL_SELECT_REGION : TOOL_SELECT_RECT;
    snip::disarm();  // (the tool changes: not the end of this snip)
    ToolHandler* th = context().getToolHandler();
    th->selectTool(snipTool);
    th->fireToolChanged();
    snip::arm(lasso ? snip::Shape::Lasso : snip::Shape::Rectangle, static_cast<snip::Purpose>(purpose));
    Q_EMIT snipChanged();
    Q_EMIT toolChanged();
}

void AppController::cancelSnip() { endSnip(true); }

void AppController::endSnip(bool restore) {
    const QString previous = std::exchange(snipPreviousTool, QString());
    const bool wasArmed = snip::isArmed();
    snip::disarm();
    if (restore && !previous.isEmpty() && context().getToolHandler()->getToolType() == snipTool) {
        selectTool(previous);
    }
    if (wasArmed || restore) {
        Q_EMIT snipChanged();
    }
}

void AppController::followSnipTool() {
    // Another tool chosen (a key, a button, a menu): the snip ends, that tool stays
    if (snip::isArmed() && context().getToolHandler()->getToolType() != snipTool) {
        snipPreviousTool.clear();
        snip::disarm();
        Q_EMIT snipChanged();
    }
}

QString AppController::snipResolution() const {
    std::string set;
    context().getSettings()->getCustomElement("xournalQt").getString("snipResolution", set);
    return set == "high" || set == "veryHigh" ? QString::fromStdString(set) : QStringLiteral("screen");
}

void AppController::setSnipResolution(const QString& resolution) {
    if (resolution == snipResolution()) {
        return;
    }
    context().getSettings()->getCustomElement("xournalQt").setString("snipResolution", resolution.toStdString());
    context().getSettings()->customSettingsChanged();
    applySnipResolution();
}

void AppController::applySnipResolution() {
    const QString r = snipResolution();
    const snip::Resolution wanted = r == QLatin1String("veryHigh") ? snip::Resolution::VeryHigh
                                    : r == QLatin1String("high")   ? snip::Resolution::High
                                                                   : snip::Resolution::Screen;
    if (wanted != snip::resolution()) {
        snip::setResolution(wanted);
    }
    Q_EMIT snipResolutionChanged();
}

void AppController::snipped(DocumentSession& s, const QImage& image, int page, const QRectF& area, bool capped) {
    endSnip(true);
    if (image.isNull()) {
        Q_EMIT pageActionDone(tr("Nothing of the page there to copy"), false);
        return;
    }
    auto* mime = new QMimeData;
    mime->setImageData(image);  // (the formats of pictures Qt offers other apps)
    QByteArray png;
    {
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        image.save(&buffer, "PNG");
    }
    mime->setData(QStringLiteral("image/png"), png);
    // Where it came from: a link to the page (with the document's absolute path; none for a document without a file)
    snip::Source source;
    source.page = page;
    source.area = area;
    const fs::path file = s.documentFile();
    source.title = tr("%1, page %2").arg(QString::fromStdString(file.stem().string())).arg(page + 1);
    if (!file.empty()) {
        links::Link link = DocumentLinks::linkTo(s, static_cast<size_t>(page), {});
        link.wiki = false;
        link.path = QString::fromStdString(file.generic_string());
        source.link = links::write(link);
    }
    mime->setData(QString::fromLatin1(snip::MIME), snip::encode(source));
    QGuiApplication::clipboard()->setMimeData(mime);
    // Its size (and when it was made smaller than the resolution asked for: the limit of a picture's pixels)
    const int dpi = area.width() > 0 ? static_cast<int>(std::lround(image.width() * 72.0 / area.width())) : 0;
    Q_EMIT pageActionDone(
            capped ? tr("Copied picture (%1×%2 pixels, %3 dpi: the area is too large for more)")
                             .arg(image.width())
                             .arg(image.height())
                             .arg(dpi)
                   : tr("Copied picture (%1×%2 pixels, %3 dpi)").arg(image.width()).arg(image.height()).arg(dpi),
            false);
}

bool AppController::addSnipLink() {
    CanvasView* v = snipLinkView;
    snipLinkView.clear();
    if (!v || !v->addSnipLink()) {
        Q_EMIT pageActionDone(tr("The picture is not there any more: no link added"), false);
        return false;
    }
    return true;
}
