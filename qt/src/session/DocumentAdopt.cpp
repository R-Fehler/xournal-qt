/*
 * xournal-qt: DocumentSession, annotations of other apps made editable (AdoptAnnotations.h,
 * qt/docs/features/adopt-annotations.md).
 *
 * @license GNU GPLv2 or later
 */
#include <algorithm>
#include <atomic>
#include <mutex>
#include <shared_mutex>
#include <set>
#include <system_error>

#include <glib.h>

#include "control/layer/LayerController.h"
#include "model/Document.h"
#include "model/Layer.h"
#include "model/XojPage.h"
#include "undo/UndoAction.h"
#include "undo/UndoRedoHandler.h"
#include "util/PathUtil.h"
#include "util/Util.h"
#include "util/i18n.h"

#include "AdoptAnnotations.h"
#include "DocumentSession.h"
#include "HybridPdf.h"
#include "MergedPdf.h"
#include "FileIo.h"
#include "PageNoteSpace.h"
#include "PdfEncryption.h"
#include "PdfPageKeeper.h"
#include "StickyNote.h"

namespace xqt {

namespace {

using fileio::stampOf;

bool inSomeCache(const fs::path& pdf) { return HybridPdf::inCache(pdf) || MergedPdf::inCache(pdf); }

}  // namespace

/// Adopting annotations, as one undo step: the layers added to pages and the background PDF swapped for the copy
/// without the originals. Undone: the layers go and the earlier background PDF (with the originals) comes back.
class AdoptUndoAction final: public UndoAction {
public:
    struct Added {
        PageRef page;
        Layer* layer;
        Layer::Index position;  ///< 0-based
    };
    AdoptUndoAction(DocumentSession& session, std::vector<Added> added, DocumentSession::BackgroundState before,
                    std::string beforeStamp, fs::path beforeLink, DocumentSession::BackgroundState after,
                    quint64 numbering):
            UndoAction("AdoptUndoAction"),
            session(session),
            added(std::move(added)),
            before(std::move(before)),
            beforeStamp(std::move(beforeStamp)),
            beforeLink(std::move(beforeLink)),
            after(std::move(after)),
            numbering(numbering) {
        for (const Added& a: this->added) {
            if (std::find(pages.begin(), pages.end(), a.page) == pages.end()) {
                pages.push_back(a.page);
            }
        }
    }
    ~AdoptUndoAction() override {
        if (!applied) {
            for (Added& a: added) {
                delete a.layer;
            }
        }
    }

    /// Put the layers on their pages (the background is swapped by the caller first)
    void insertLayers() {
        LayerController* lc = session.getLayerController();
        for (const Added& a: added) {
            sticky::NoteLayerChange change(*a.layer);
            lc->insertLayer(a.page, a.layer, std::min<Layer::Index>(a.position, a.page->getLayerCount()));
        }
        applied = true;
        repaint();
    }

    bool undo(Control*) override {
        if (!applied) {
            return true;
        }
        if (session.pdfNumbering() != numbering) {
            return false;  // (a save renumbered the PDF pages: the earlier PDF no longer matches)
        }
        // The background as it was: the file itself while it is unchanged, else the link kept to its bytes
        DocumentSession::BackgroundState back = before;
        std::error_code ec;
        if (!inSomeCache(before.pdf) && stampOf(before.pdf) != beforeStamp) {
            if (beforeLink.empty() || !fs::exists(beforeLink, ec)) {
                return false;
            }
            back.pdf = beforeLink;
        }
        std::string error;
        if (!session.setBackgroundState(back, error)) {
            g_warning("Could not bring back the annotations of the other app: %s", error.c_str());
            return false;
        }
        LayerController* lc = session.getLayerController();
        for (auto it = added.rbegin(); it != added.rend(); ++it) {
            sticky::NoteLayerChange change(*it->layer);
            lc->removeLayer(it->page, it->layer);
        }
        applied = false;
        repaint();
        return true;
    }

    bool redo(Control*) override {
        if (applied) {
            return true;
        }
        std::error_code ec;
        if (session.pdfNumbering() != numbering || !fs::exists(after.pdf, ec)) {
            return false;
        }
        std::string error;
        if (!session.setBackgroundState(after, error)) {
            g_warning("Could not take the annotations of the other app again: %s", error.c_str());
            return false;
        }
        insertLayers();
        return true;
    }

    std::vector<PageRef> getPages() override { return pages; }
    std::string getText() override { return _("Adopt annotations"); }

private:
    void repaint() {
        Document* doc = session.getDocument();
        for (const PageRef& p: pages) {
            const size_t i = doc->indexOf(p);
            if (i != npos) {
                session.firePageChanged(i);
                session.revisePage(i);
            }
        }
    }

    DocumentSession& session;
    std::vector<Added> added;
    std::vector<PageRef> pages;
    DocumentSession::BackgroundState before;
    std::string beforeStamp;
    fs::path beforeLink;
    DocumentSession::BackgroundState after;
    quint64 numbering;
    bool applied = false;
};

auto DocumentSession::backgroundState() const -> BackgroundState {
    const PdfPageKeeper::Held h = pdfPages->held();
    return {h.pdf, h.madeFrom, h.grownFrom};
}

bool DocumentSession::setBackgroundState(const BackgroundState& state, std::string& error) {
    if (!pdfPages->takeBackground({state.pdf, state.madeFrom, state.grownFrom}, error)) {
        return false;
    }
    if (HybridPdf::inCache(state.pdf) &&
        std::find(retainedBases.begin(), retainedBases.end(), state.pdf) == retainedBases.end()) {
        HybridPdf::retain(state.pdf);
        retainedBases.push_back(state.pdf);
    }
    hybridRevision.reset();  // (the other app's annotations go or come back: the next save writes the file anew)
    return true;
}

auto DocumentSession::planAdoption() const -> AdoptPlan {
    AdoptPlan plan;
    {
        std::shared_lock lock(*doc);
        plan.pdf = doc->getPdfFilepath();
        if (plan.pdf.empty() || doc->getPdfPageCount() == 0) {
            return plan;
        }
        const size_t count = doc->getPdfPageCount();
        for (size_t i = 0; i < doc->getPageCount(); ++i) {
            const PageRef p = doc->getPage(i);
            size_t n = npos;
            if (p->getBackgroundType().isPdfPage()) {
                n = p->getPdfPageNr();
            } else if (auto it = hybridBase.find(p.get()); it != hybridBase.end() && it->second.first.lock() == p) {
                n = it->second.second;  // (a generated page of a hybrid PDF: the other app's marks were on its page)
            }
            if (n >= count) {
                n = npos;
            }
            plan.pageOf.push_back(n);
            if (n != npos) {
                plan.pdfPages.push_back(n);
            }
        }
    }
    std::sort(plan.pdfPages.begin(), plan.pdfPages.end());
    plan.pdfPages.erase(std::unique(plan.pdfPages.begin(), plan.pdfPages.end()), plan.pdfPages.end());
    if (plan.pdfPages.empty()) {
        return plan;
    }
    static std::atomic<unsigned> counter{0};
    const std::string name =
            std::to_string(Util::getPid()) + "-" + std::to_string(serialNo) + "-adopt" + std::to_string(++counter);
    if (HybridPdf::inCache(plan.pdf)) {
        // The clean copy of a PDF with notes: the copy goes beside it (it keeps the clean copy's mark)
        plan.copy = HybridPdf::cacheFolder() / name / "base.pdf";
    } else {
        // A user's PDF, or a merged PDF: the copy is a merged PDF in the cache (saving a .xopp puts it beside it as
        // ".name.pages.pdf"; the user's PDF is never written)
        plan.copy = MergedPdf::cacheFolder() / (name + ".pdf");
        if (MergedPdf::kindOf(plan.pdf) == MergedPdf::Kind::None) {
            plan.mark = static_cast<int>(MergedPdf::Kind::WithSource);
        }
    }
    plan.numbering = pdfPages->numbering();
    plan.ok = true;
    return plan;
}

bool DocumentSession::applyAdoption(adopt::Prepared prepared, const AdoptPlan& plan, std::string& error) {
    if (!prepared.ok) {
        error = prepared.error;
        return false;
    }
    if (prepared.converted == 0 || prepared.copy.empty()) {
        return true;
    }
    if (backgroundState().pdf != plan.pdf || pdfPages->numbering() != plan.numbering) {
        error = _("The document's PDF changed meanwhile. Try again.");
        std::error_code ec;
        fs::remove(prepared.copy, ec);
        return false;
    }
    clearSelectionEndText();
    const BackgroundState before = backgroundState();
    const std::string beforeStamp = stampOf(before.pdf);
    // A user's PDF (or a merged PDF beside a .xopp) may be written over by a save (notes saved into the PDF itself):
    // undo then takes the original bytes from a link kept in the cache
    fs::path link;
    if (!inSomeCache(before.pdf)) {
        link = HybridPdf::cacheFolder() / ("adopt-before-" + prepared.copy.stem().string()) / before.pdf.filename();
        std::error_code ec;
        fs::create_directories(link.parent_path(), ec);
        fs::create_hard_link(before.pdf, link, ec);
        if (ec) {
            ec.clear();
            fs::copy_file(before.pdf, link, fs::copy_options::overwrite_existing, ec);
        }
        if (ec) {
            link.clear();
        } else {
            PdfEncryption::derive(link, before.pdf);
            HybridPdf::retain(link);
            retainedBases.push_back(link);
        }
    }
    BackgroundState after;
    after.pdf = prepared.copy;
    after.madeFrom = MergedPdf::inCache(prepared.copy) ? annotatedPdf() : before.madeFrom;
    after.grownFrom = before.pdf;  // (its pages keep their numbers in the copy)
    if (!setBackgroundState(after, error)) {
        std::error_code ec;
        fs::remove(prepared.copy, ec);
        return false;
    }

    // The layers: per page of the document that shows a PDF page with converted marks (a PDF page shown twice gets
    // them twice)
    const std::string layerName = adopt::layerName(prepared.app);
    std::vector<AdoptUndoAction::Added> added;
    for (size_t i = 0; i < plan.pageOf.size(); ++i) {
        const size_t n = plan.pageOf[i];
        auto it = n == npos ? prepared.pages.end() : prepared.pages.find(n);
        if (it == prepared.pages.end()) {
            continue;
        }
        const adopt::PageMarks& marks = it->second;
        PageRef page;
        Layer::Index position = 0, top = 0;
        {
            std::shared_lock lock(*doc);
            if (i >= doc->getPageCount()) {
                break;
            }
            page = doc->getPage(i);
            // Below the page's sticky notes, above everything else; the new notes on top
            const auto& layers = page->getLayers();
            position = layers.size();
            for (size_t k = 0; k < layers.size(); ++k) {
                if (sticky::isNote(*layers[k])) {
                    position = k;
                    break;
                }
            }
            top = layers.size();
        }
        const QPointF offset = notespace::offsetOf(*page);
        auto copyOf = [&](const Element& e) {
            ElementPtr copy = e.clone();  // (a PDF page shown on two pages gets them twice)
            if (offset.x() != 0 || offset.y() != 0) {
                copy->move(offset.x(), offset.y());
            }
            return copy;
        };
        if (!marks.elements.empty()) {
            auto* layer = new Layer();
            layer->setName(layerName);
            for (const auto& e: marks.elements) {
                layer->addElement(copyOf(*e));
            }
            added.push_back({page, layer, position});
            ++top;
        }
        for (const auto& note: marks.notes) {
            auto* layer = new Layer();
            layer->setName(note->getName());
            for (const auto* e: note->getElementsView()) {
                layer->addElement(copyOf(*e));
            }
            added.push_back({page, layer, top++});
        }
    }
    auto action = std::make_unique<AdoptUndoAction>(*this, std::move(added), before, beforeStamp, link, after,
                                                    pdfPages->numbering());
    action->insertLayers();
    // Pages whose PDF page lost annotations without getting a layer (nothing to show of them: empty notes)
    {
        std::shared_lock lock(*doc);
        for (size_t i = 0; i < plan.pageOf.size() && i < doc->getPageCount(); ++i) {
            if (plan.pageOf[i] != npos && prepared.pages.count(plan.pageOf[i])) {
                lock.unlock();
                firePageChanged(i);
                revisePage(i);
                lock.lock();
            }
        }
    }
    undoRedo->addUndoAction(std::move(action));
    return true;
}

bool DocumentSession::adoptAnnotations(std::string& error, size_t* converted) {
    const AdoptPlan plan = planAdoption();
    if (!plan.ok) {
        if (converted) {
            *converted = 0;
        }
        return true;
    }
    adopt::Prepared prepared =
            adopt::prepare(plan.pdf, plan.pdfPages, plan.copy,
                           plan.mark < 0 ? std::nullopt : std::optional(static_cast<MergedPdf::Kind>(plan.mark)));
    const size_t n = prepared.converted;
    if (!applyAdoption(std::move(prepared), plan, error)) {
        return false;
    }
    if (converted) {
        *converted = n;
    }
    return true;
}

}  // namespace xqt
