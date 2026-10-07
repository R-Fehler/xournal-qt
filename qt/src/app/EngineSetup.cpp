/*
 * xournal-qt: what the QML engine of the app's windows has before Main.qml is loaded.
 *
 * @license GNU GPLv2 or later
 */
#include "EngineSetup.h"

#include <QQmlContext>
#include <QQmlEngine>

#include "shell/AnnotationsModel.h"
#include "shell/HitPages.h"
#include "shell/MdSnippets.h"
#include "shell/PageSketches.h"
#include "shell/DocumentCovers.h"
#include "shell/Thumbnails.h"

namespace xqt {

void setUpEngine(QQmlEngine& engine, QObject* app) {
    engine.addImageProvider("thumbnail", new ThumbnailProvider);  // the engine takes ownership
    engine.addImageProvider("sketch", new SketchProvider);
    engine.addImageProvider("cover", new CoverProvider);
    engine.addImageProvider("hitpage", new HitPageProvider);
    engine.addImageProvider("mdsnippet", new MdSnippetProvider);
    engine.addImageProvider("annotation", new AnnotationImageProvider);
    engine.rootContext()->setContextProperty("app", app);
}

}  // namespace xqt
