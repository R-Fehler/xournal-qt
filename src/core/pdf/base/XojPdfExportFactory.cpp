#include "XojPdfExportFactory.h"

#include "model/Document.h"
#include "model/XojPage.h"  // xournal-qt

#include "QPdfExport.h"
#include "XojCairoPdfExport.h"  // for XojCairoPdfExport

class XojPdfExport;

XojPdfExportFactory::XojPdfExportFactory() = default;

XojPdfExportFactory::~XojPdfExportFactory() = default;

auto XojPdfExportFactory::createExport(const Document* doc, ProgressListener* listener, ExportBackend backend)
        -> std::unique_ptr<XojPdfExport> {
    // xournal-qt: the qpdf backend lays the drawing over the PDF page's own box; pages with space for notes
    // (model/NoteSpace.h) are larger than that, which the cairo backend draws
    bool noteSpace = false;
    for (size_t i = 0; i < doc->getPageCount() && !noteSpace; ++i) {
        noteSpace = !doc->getPage(i)->getNoteSpace().empty();
    }
    if (!doc->getPdfFilepath().empty() && !noteSpace) {
        switch (backend) {
            case ExportBackend::DEFAULT:  // fallback to qpdf/podofo/mupdf/cairo in that order
#ifdef ENABLE_QPDF
            case ExportBackend::QPDF:
                return std::make_unique<QPdfExport>(doc, listener);
#endif
            case ExportBackend::CAIRO:
            default:  // The requested backend has not been included in this build
                return std::make_unique<XojCairoPdfExport>(doc, listener);
        }
    }
    return std::make_unique<XojCairoPdfExport>(doc, listener);
}
