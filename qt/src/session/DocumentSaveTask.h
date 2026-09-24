/*
 * xournal-qt: a save of a DocumentSession while it runs (DocumentSave.cpp). Internal to the session.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <functional>
#include <future>
#include <memory>
#include <unordered_map>

#include "control/xojfile/SaveHandler.h"
#include "model/Document.h"
#include "model/PageRef.h"
#include "pdf/base/XojPdfPage.h"
#include "util/Util.h"
#include "util/raii/CairoWrappers.h"

#include "DocumentSession.h"
#include "PdfPageKeeper.h"
#include "filesystem.h"

namespace xqt {

struct DocumentSession::SaveTask {
    SaveRequest request;
    fs::path target;
    bool hybrid = false;
    int plans = 0;
    PdfPageKeeper::SavePlan plan;
    bool pdfWork = false;        ///< step 2 writes the merged PDF
    fs::path expectedBg;         ///< the background PDF after step 3 (another one at step 4: plan again)
    fs::path original, ownCopy;  ///< a hybrid PDF: copies made before its file is replaced
    // --- the copy (step 4)
    std::unique_ptr<Document> snapshot;
    bool snapshotTaken = false;  ///< the saved point was moved to it
    size_t pdfPageCount = npos;
    PageRef previewPage;
    XojPdfPageSPtr previewPdf;
    std::unordered_map<const XojPage*, size_t> baseOf;  ///< a hybrid PDF: page of the copy -> page of its clean copy
    bool createBackup = false;
    fs::path staged, stagedAs;  ///< the merged PDF written under another name, and its name
    fs::path pathWhenTaken;
    // --- written by step 5
    SaveResult result;
    std::unique_ptr<SaveHandler> handler;
    xoj::util::CairoSurfaceSPtr preview;
    bool xoppWritten = false;
    bool commitTried = false, committed = false;
    // --- the step that runs
    std::shared_future<void> work;
    std::function<void()> then;
};

}  // namespace xqt
