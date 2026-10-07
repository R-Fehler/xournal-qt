/*
 * xournal-qt: documents that no session or view owns (DetachedDocument.h).
 *
 * @license GNU GPLv2 or later
 */
#include "DetachedDocument.h"

#include "model/Document.h"
#include "model/DocumentHandler.h"

namespace xqt {

DocumentHandler& detachedHandler() {
    static DocumentHandler handler;
    return handler;
}

std::unique_ptr<Document> newDetachedDocument() { return std::make_unique<Document>(&detachedHandler()); }

}  // namespace xqt
