/*
 * xournal-qt: the snip tool of the window (qt/snip, qt/docs/snip.md): armed from the select tools' list or the image
 * button, the next rectangle or lasso on a page (Snip.h, CanvasView::snip) puts its picture on the clipboard, with a
 * link to where it came from; then the tool used before comes back. Pasted into a document of the app, the window
 * offers to add that link next to the picture (CanvasView::addSnipLink).
 *
 * @license GNU GPLv2 or later
 */
#include <QBuffer>
#include <QClipboard>
#include <QGuiApplication>
#include <QMimeData>

#include "control/ToolHandler.h"
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
    switch (snip::armed()) {
        case snip::Shape::Rectangle:
            return QStringLiteral("rect");
        case snip::Shape::Lasso:
            return QStringLiteral("lasso");
        default:
            return {};
    }
}

void AppController::startSnip(const QString& shape) {
    const bool lasso = shape == QLatin1String("lasso");
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
    ToolHandler* th = app->getToolHandler();
    th->selectTool(snipTool);
    th->fireToolChanged();
    snip::arm(lasso ? snip::Shape::Lasso : snip::Shape::Rectangle);
    Q_EMIT snipChanged();
    Q_EMIT toolChanged();
}

void AppController::cancelSnip() { endSnip(true); }

void AppController::endSnip(bool restore) {
    const QString previous = std::exchange(snipPreviousTool, QString());
    const bool wasArmed = snip::isArmed();
    snip::disarm();
    if (restore && !previous.isEmpty() && app->getToolHandler()->getToolType() == snipTool) {
        selectTool(previous);
    }
    if (wasArmed || restore) {
        Q_EMIT snipChanged();
    }
}

void AppController::followSnipTool() {
    // Another tool chosen (a key, a button, a menu): the snip ends, that tool stays
    if (snip::isArmed() && app->getToolHandler()->getToolType() != snipTool) {
        snipPreviousTool.clear();
        snip::disarm();
        Q_EMIT snipChanged();
    }
}

void AppController::snipped(DocumentSession& s, const QImage& image, int page, const QRectF& area) {
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
    Q_EMIT pageActionDone(tr("Copied picture"), false);
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
