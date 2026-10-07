/*
 * xournal-qt: stickers in the window (qt/stickers, qt/docs/features/stickers.md): the picker's list, saving what is
 * selected as a sticker (into the library's Stickers folder or the app-wide set; also onto the clipboard) and pasting
 * one on the current page. The files are written and read off the UI thread by the current view
 * (CanvasView::saveSticker, loadSticker), which waits for that work when it goes.
 *
 * @license GNU GPLv2 or later
 */
#include <QBuffer>
#include <QClipboard>
#include <QDate>
#include <QGuiApplication>
#include <QImage>
#include <QMimeData>

#include "control/ToolHandler.h"
#include "session/AppContext.h"
#include "session/DocumentSession.h"
#include "session/StickerFile.h"
#include "shell/Library.h"
#include "shell/LibraryModel.h"
#include "shell/Stickers.h"

#include "AppController.h"
#include "CanvasView.h"
#include "MixedSelection.h"

using namespace xqt;

QObject* AppController::stickersModel() const {
    if (!stickers) {
        stickers = std::make_unique<StickersModel>();
        syncStickers();
        connect(library, &LibraryModel::libraryChanged, stickers.get(), [this] { syncStickers(); });
    }
    return stickers.get();
}

void AppController::syncStickers() const {
    const Library* lib = library ? library->library() : nullptr;
    stickers->setLibrary(lib ? lib->root() : fs::path(), lib ? lib->configDir() : fs::path());
}

bool AppController::canPasteSticker() const {
    return canvas() && session() && !session()->isReadOnly() && !canvas()->isReadingOnly() && !textPagesFixed() &&
           textDocument().isEmpty();
}

QVariantMap AppController::stickerDraft(bool appWide) const {
    QVariantMap draft;
    const auto source = canvas() ? canvas()->stickerSource() : std::nullopt;
    draft.insert(QStringLiteral("offered"), source.has_value());
    QString name = source ? QString::fromStdString(stickers::suggestedName(source->content)) : QString();
    if (name.isEmpty()) {
        name = tr("Sticker %1").arg(QDate::currentDate().toString(Qt::ISODate));
    }
    draft.insert(QStringLiteral("name"), name);
    draft.insert(QStringLiteral("picture"), source && source->pictureOffered);
    auto* model = static_cast<StickersModel*>(stickersModel());
    const fs::path set = model->rootOf(appWide || !model->hasLibrary() ? QStringLiteral("app") : QStringLiteral("library"));
    draft.insert(QStringLiteral("folders"), stickers::folders(set));
    draft.insert(QStringLiteral("hasLibrary"), model->hasLibrary());
    return draft;
}

bool AppController::saveSticker(const QString& name, const QString& folder, bool withPicture, bool appWide) {
    CanvasView* view = canvas();
    if (!view) {
        return false;
    }
    auto source = view->stickerSource();
    if (!source) {
        return false;
    }
    auto* model = static_cast<StickersModel*>(stickersModel());
    fs::path into = model->rootOf(appWide || !model->hasLibrary() ? QStringLiteral("app") : QStringLiteral("library"));
    if (into.empty() || folder.contains(QLatin1String(".."))) {
        return false;
    }
    for (const QString& part: folder.split('/', Qt::SkipEmptyParts)) {
        const std::string n = stickers::fileNameOf(part.toStdString());
        if (!n.empty()) {
            into /= n;
        }
    }
    std::error_code ec;
    fs::create_directories(into, ec);
    if (ec) {
        Q_EMIT pageActionDone(tr("The sticker could not be saved: %1").arg(QString::fromStdString(ec.message())), false);
        return false;
    }
    std::string file = stickers::fileNameOf(name.trimmed().toStdString());
    if (file.empty()) {
        file = tr("Sticker %1").arg(QDate::currentDate().toString(Qt::ISODate)).toStdString();
    }
    const fs::path target = stickers::uniqueTarget(into, file);
    const bool picture = withPicture && source->pictureOffered;
    auto shared = std::make_shared<CanvasView::StickerSource>(std::move(*source));
    QPointer<AppController> self(this);
    const QString path = QString::fromStdString(target.string());
    const QString shown = QString::fromStdString(target.stem().string());
    const bool started = view->saveSticker(shared, picture, target, [self, path, shown](const QString& error,
                                                                                        const std::string& bytes) {
        if (!self) {
            return;
        }
        if (!error.isEmpty() || bytes.empty()) {
            Q_EMIT self->pageActionDone(tr("The sticker could not be saved: %1").arg(error), false);
            Q_EMIT self->stickerSaved(QString(), error.isEmpty() ? QStringLiteral("empty") : error);
            return;
        }
        // On the clipboard too (as the author wanted): Ctrl+V pastes it again at once
        MixedSelection::setClipboard(bytes);
        if (self->stickers) {
            self->stickers->refresh();
        }
        Q_EMIT self->pageActionDone(tr("Saved sticker “%1”").arg(shown), false);
        Q_EMIT self->stickerSaved(path, QString());
    });
    if (!started) {
        Q_EMIT pageActionDone(tr("A sticker is still being saved"), false);
    }
    return started;
}

bool AppController::pasteSticker(const QString& path) {
    CanvasView* view = canvas();
    if (!view || !canPasteSticker() || path.isEmpty()) {
        return false;
    }
    QPointer<AppController> self(this);
    QPointer<CanvasView> target(view);
    return view->loadSticker(fs::path(path.toStdString()), [self, target, path](const QString& error,
                                                                                const std::string& bytes, bool picture) {
        if (!self || !target) {
            return;
        }
        if (!error.isEmpty()) {
            Q_EMIT self->pageActionDone(tr("The sticker could not be read: %1").arg(error), false);
            Q_EMIT self->stickerPasted(QString(), error);
            return;
        }
        // (a select tool: the sticker can be moved and resized right away, as an inserted image)
        const ToolType tool = self->app->getToolHandler()->getToolType();
        if (tool != TOOL_SELECT_RECT && tool != TOOL_SELECT_REGION) {
            self->selectTool("selectRect");
        }
        bool pasted = false;
        if (picture) {
            // A picture: on the clipboard as a picture, pasted at the size its resolution gives it
            const QByteArray data(bytes.data(), static_cast<qsizetype>(bytes.size()));
            QImage image = QImage::fromData(data);
            auto* mime = new QMimeData;
            mime->setImageData(image);
            mime->setData(QStringLiteral("image/png"), data);
            QGuiApplication::clipboard()->setMimeData(mime);
            std::optional<QSizeF> size;
            if (!image.isNull() && image.dotsPerMeterX() > 0 && image.dotsPerMeterY() > 0) {
                size = QSizeF(image.width() * 72.0 / (image.dotsPerMeterX() * 0.0254),
                              image.height() * 72.0 / (image.dotsPerMeterY() * 0.0254));
            }
            pasted = target->insertImage(data, std::nullopt, size);
        } else {
            MixedSelection::setClipboard(bytes);
            pasted = target->pasteSticker(bytes);
        }
        if (!pasted) {
            Q_EMIT self->stickerPasted(QString(), QStringLiteral("not pasted"));
            return;
        }
        if (self->stickers) {
            self->stickers->markUsed(path);
        }
        Q_EMIT self->stickerPasted(path, QString());
    });
}
