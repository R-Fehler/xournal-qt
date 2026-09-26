/*
 * xournal-qt: web pictures of Markdown texts (qt/docs/md-images.md, "Web images"). A picture at an https:// address is
 * never fetched unasked: its "Load image" asks the window (webImageRequested), which shows the whole address, and
 * loadWebImage() fetches it through NetFetch (opt-in: `networkAccess`) into the app cache only. The texts that show it
 * are laid out again.
 *
 * @license GNU GPLv2 or later
 */
#include <mutex>

#include <QFileInfo>
#include <QImage>
#include <QPointer>
#include <QSaveFile>
#include <QUrl>

#include "control/settings/Settings.h"
#include "model/Document.h"
#include "model/Layer.h"
#include "model/Text.h"
#include "model/XojPage.h"

#include "AppController.h"
#include "MdBox.h"
#include "MdImages.h"
#include "session/AppContext.h"
#include "session/DocumentSession.h"
#include "shell/NetFetch.h"
#include "shell/TabManager.h"

using namespace xqt;

namespace {
/// Settings `networkAccess`: "ask" (not decided yet), "on", "off" (as Citations reads it)
QString networkAccessOf(Settings& settings) {
    std::string v;
    settings.getCustomElement("xournalQt").getString("networkAccess", v);
    return v == "on" || v == "off" ? QString::fromStdString(v) : QStringLiteral("ask");
}
}  // namespace

bool AppController::loadWebImage(const QString& address) {
    const QUrl url(address);
    if (!url.isValid() || (url.scheme() != QLatin1String("https") && url.scheme() != QLatin1String("http"))) {
        return false;
    }
    const std::string cache = md::images::webCachePath(address.toStdString());
    if (cache.empty()) {
        return false;
    }
    Settings& settings = *app->getSettings();
    if (networkAccessOf(settings) == QLatin1String("off")) {
        Q_EMIT message(tr("Load image"),
                       tr("Connecting to the web is turned off (Settings → Documents → Web and citations)."), true);
        return false;
    }
    // (the window showed the address and the user chose "Load": that is the opt-in when it was not decided yet)
    if (networkAccessOf(settings) == QLatin1String("ask")) {
        settings.getCustomElement("xournalQt").setString("networkAccess", "on");
        settings.customSettingsChanged();
    }
    QPointer<AppController> self(this);
    Q_EMIT pageActionDone(tr("Loading the picture from %1…").arg(url.host()), false);
    NetFetch::instance().get(url, 20000, 40 * 1024 * 1024, [self, address, cache](const NetFetch::Reply& reply) {
        if (!self) {
            return;
        }
        if (!reply.error.isEmpty() || reply.status != 200) {
            Q_EMIT self->message(tr("Load image"),
                                 tr("The picture could not be loaded: %1")
                                         .arg(reply.error.isEmpty() ? QString::number(reply.status) : reply.error),
                                 true);
            return;
        }
        if (QImage::fromData(reply.body).isNull() && !reply.body.trimmed().startsWith("<svg") &&
            !reply.body.contains("<svg")) {
            Q_EMIT self->message(tr("Load image"), tr("What that address gave is no picture."), true);
            return;
        }
        QSaveFile file(QString::fromStdString(cache));
        if (!file.open(QIODevice::WriteOnly) || file.write(reply.body) != reply.body.size() || !file.commit()) {
            Q_EMIT self->message(tr("Load image"), tr("The picture could not be kept in the app's cache."), true);
            return;
        }
        md::images::changed();
        self->relayoutPictures(address.toStdString());
        Q_EMIT self->pageActionDone(tr("Picture loaded"), false);
    });
    return true;
}

void AppController::relayoutPictures(const std::string& link) {
    // Every open document (all windows): its Markdown texts that show this picture are laid out again and drawn
    AppController* main = primary ? primary : this;
    std::vector<AppController*> all{main};
    all.insert(all.end(), main->windows.begin(), main->windows.end());
    for (AppController* w: all) {
        for (int i = 0; i < w->tabs->count(); ++i) {
            DocumentSession* s = w->tabs->session(i);
            Document* doc = s ? s->getDocument() : nullptr;
            if (!doc) {
                continue;
            }
            for (size_t p = 0; p < doc->getPageCount(); ++p) {
                const PageRef page = doc->getPage(p);
                bool shows = false;
                {
                    std::unique_lock lock(*doc);
                    Layer* layer = md::markdownLayer(page);
                    if (!layer) {
                        continue;
                    }
                    for (const auto* e: layer->getElementsView()) {
                        if (e->getType() == ELEMENT_TEXT) {
                            auto* text = const_cast<Text*>(static_cast<const Text*>(e));
                            if (text->getText().find(link) != std::string::npos) {
                                text->setText(text->getText());  // (its size again: the picture's)
                                shows = true;
                            }
                        }
                    }
                }
                if (shows) {
                    page->firePageChanged();
                    s->firePageChanged(p);
                }
            }
        }
    }
}
