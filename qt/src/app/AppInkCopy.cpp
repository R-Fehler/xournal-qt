/*
 * xournal-qt: copy handwriting as text (qt/copy-tools, qt/docs/features/handwriting-search.md, "Copy handwriting as
 * text").
 *
 * The tool (the "Text" button's second variant, Shift+T) is armed as a snip is (AppSnip.cpp, Snip.h with
 * snip::Purpose::InkText): the next lasso dragged over a page is a sweep (CanvasView::inkSwept), and the tool used
 * before comes back at once, so the writing goes on. The words the sweep goes over (hwr/InkCopy.h) are read by the
 * handwriting search's worker as a job the user waits for (InkRecognitionService::Job::urgent: only the lines there,
 * first, lines read before taken as they are) and go to the clipboard as text in reading order; "Copy as text" of the
 * selection's pill does the same with every word of the selected ink. The window shows the text near the place
 * (inkTextCopy). Nothing is written into the document.
 *
 * @license GNU GPLv2 or later
 */
#include <shared_mutex>

#include <QClipboard>
#include <QGuiApplication>

#include "control/tools/EditSelection.h"
#include "hwr/HandwritingSearch.h"
#include "hwr/InkCopy.h"
#include "hwr/InkLayout.h"
#include "hwr/InkRecognitionService.h"
#include "model/Document.h"
#include "model/Stroke.h"
#include "model/XojPage.h"
#include "session/DocumentSession.h"

#include "AppController.h"
#include "CanvasPage.h"
#include "CanvasView.h"
#include "Snip.h"

using namespace xqt;

namespace {
/// How near the sweep a word counts as touched (page points)
constexpr double REACH = 3.0;

/// The text with the words the recogniser was unsure of in grey (the popup's rich text)
QString htmlOf(const hwr::CopiedText& t) {
    QStringList out;
    size_t line = 0;
    for (const QString& text: t.text.split(u'\n')) {
        if (text.isEmpty() || line >= t.lines.size()) {
            out << QString();
            continue;
        }
        QStringList words;
        for (const hwr::CopiedWord& w: t.lines[line]) {
            const QString escaped = w.text.trimmed().toHtmlEscaped();
            words << (w.unsure() ? QStringLiteral("<span style=\"color:#9aa0a6\">%1</span>").arg(escaped) : escaped);
        }
        out << words.join(u' ');
        ++line;
    }
    return out.join(QStringLiteral("<br>"));
}
}  // namespace

bool AppController::startInkCopy() {
    if (!handwriting || !handwriting->enabled()) {
        QVariantMap off{{QStringLiteral("state"), QStringLiteral("off")}};
        Q_EMIT inkTextCopy(off);  // (the window says so, with the way to Settings)
        return false;
    }
    armSnip(true, static_cast<int>(snip::Purpose::InkText));
    return true;
}

bool AppController::selectionHasInk() const {
    const EditSelection* sel = canvas() ? canvas()->getSelection() : nullptr;
    if (!sel) {
        return false;
    }
    for (const Element* e: sel->getElementsView()) {
        if (e->getType() == ELEMENT_STROKE && static_cast<const Stroke*>(e)->getToolType() == StrokeTool::PEN) {
            return true;
        }
    }
    return false;
}

QVariantMap AppController::inkCopyPlace(CanvasView* v, int page, const QRectF& box) const {
    QVariantMap out;
    const double zoom = v->getViewController().zoom();
    const QRectF pageRect = v->pageViewRect(static_cast<size_t>(page));
    const QRectF onCanvas = v->getViewController().viewToScreen(
            QRectF(pageRect.topLeft() + box.topLeft() * zoom, box.size() * zoom));
    out[QStringLiteral("x")] = onCanvas.x();
    out[QStringLiteral("y")] = onCanvas.y();
    out[QStringLiteral("width")] = onCanvas.width();
    out[QStringLiteral("height")] = onCanvas.height();
    out[QStringLiteral("reference")] = v != canvas();
    return out;
}

bool AppController::copySelectionAsText() {
    CanvasView* v = canvas();
    EditSelection* sel = v ? v->getSelection() : nullptr;
    CanvasPage* page = v ? v->selectionPage() : nullptr;
    const auto index = page ? v->indexOf(page) : std::nullopt;
    if (!sel || !index) {
        return false;
    }
    if (!handwriting || !handwriting->enabled()) {
        QVariantMap off{{QStringLiteral("state"), QStringLiteral("off")}};
        Q_EMIT inkTextCopy(off);
        return false;
    }
    std::vector<hwr::InkStroke> strokes;
    {
        std::shared_lock lock(*v->getSession().getDocument());
        std::vector<const Element*> elements;
        for (const Element* e: sel->getElementsView()) {
            elements.push_back(e);
        }
        strokes = hwr::strokesOf(elements);
    }
    if (strokes.empty()) {
        return false;
    }
    // (the selection's elements may lie where it was made: the popup goes where it is now)
    const xoj::util::Rectangle<double> r = sel->getRect();
    copyInkText(v, static_cast<int>(*index), std::move(strokes), QRectF(), QPolygonF(),
                QRectF(r.x, r.y, r.width, r.height));
    return true;
}

void AppController::inkSwept(CanvasView* v, int page, const QPolygonF& path) {
    endSnip(true);  // (the tool before comes back at once: the writing goes on while the words are read)
    if (!v || path.isEmpty()) {
        return;
    }
    std::vector<hwr::InkStroke> strokes;
    {
        Document* doc = v->getSession().getDocument();
        std::shared_lock lock(*doc);
        if (page < 0 || static_cast<size_t>(page) >= doc->getPageCount()) {
            return;
        }
        strokes = hwr::strokesOf(*doc->getPage(static_cast<size_t>(page)));
    }
    const QRectF area = path.boundingRect().adjusted(-REACH, -REACH, REACH, REACH);
    copyInkText(v, page, std::move(strokes), area, path, area);
}

void AppController::copyInkText(CanvasView* v, int page, std::vector<hwr::InkStroke> strokes, const QRectF& area,
                                const QPolygonF& path, const QRectF& box) {
    QVariantMap place = inkCopyPlace(v, page, box);
    if (strokes.empty()) {
        place[QStringLiteral("state")] = QStringLiteral("nothing");
        Q_EMIT inkTextCopy(place);
        return;
    }
    hwr::InkRecognitionService& service = handwriting->service();
    if (inkCopyOwner) {
        service.cancel(inkCopyOwner.get());  // (a copy before it that is still being read: this one instead)
    }
    inkCopyOwner = std::make_unique<QObject>();
    hwr::InkRecognitionService::Job job;
    job.strokes = std::move(strokes);
    job.area = area;
    job.urgent = true;
    if (hwr::InkTextIndexer* indexer = handwriting->indexerOf(&v->getSession())) {
        job.plan = indexer->plan();  // (the document's language: the models that read it)
    }
    place[QStringLiteral("state")] = QStringLiteral("reading");
    Q_EMIT inkTextCopy(place);
    std::vector<QPointF> sweep(path.begin(), path.end());
    service.submit(inkCopyOwner.get(), std::move(job),
                   [this, view = QPointer<CanvasView>(v), page, sweep = std::move(sweep), box](hwr::PageResult r) {
                       const bool ready = handwriting && handwriting->service().ready();
                       hwr::CopiedText t;
                       if (r.text) {
                           t = hwr::inReadingOrder(sweep.empty() ? hwr::allWords(*r.text)
                                                                 : hwr::sweptWords(*r.text, sweep, REACH));
                       }
                       if (!view) {
                           return;  // (the document went meanwhile)
                       }
                       QVariantMap out = inkCopyPlace(view, page, t.empty() ? box : t.box);
                       if (t.empty()) {
                           out[QStringLiteral("state")] = !r.complete && !ready ? QStringLiteral("noModel")
                                                                                : QStringLiteral("nothing");
                           Q_EMIT inkTextCopy(out);
                           return;
                       }
                       QGuiApplication::clipboard()->setText(t.text);
                       out[QStringLiteral("state")] = QStringLiteral("copied");
                       out[QStringLiteral("text")] = t.text;
                       out[QStringLiteral("html")] = htmlOf(t);
                       out[QStringLiteral("words")] = t.words;
                       out[QStringLiteral("unsure")] = t.unsure;
                       out[QStringLiteral("partial")] = !r.complete && !ready;
                       Q_EMIT inkTextCopy(out);
                   });
}
