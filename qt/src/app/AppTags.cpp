/*
 * xournal-qt: the library's tags (qt/docs/tags.md) as the window offers them: the Tags view of the library home.
 *
 * @license GNU GPLv2 or later
 */
#include "AppController.h"
#include "shell/LibraryTags.h"

using namespace xqt;

QObject* AppController::libraryTagsModel() const { return libraryTags; }
