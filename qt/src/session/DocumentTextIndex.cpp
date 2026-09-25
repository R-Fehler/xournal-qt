#include "DocumentTextIndex.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <set>
#include <shared_mutex>

#include <QElapsedTimer>
#include <optional>

#include <QCoreApplication>
#include <QThread>
#include <QThreadPool>
#include <poppler.h>

#include "model/Document.h"
#include "model/Element.h"
#include "model/Layer.h"
#include "model/Text.h"
#include "model/XojPage.h"
#include "render/RenderService.h"

#include "DocumentSession.h"
#include "MdBox.h"
#include "TextMatch.h"
#include "util/PathUtil.h"

namespace xqt {

namespace {
DocumentTextIndex::Seeder& seeder() {
    static DocumentTextIndex::Seeder s;
    return s;
}
int startDelayMs = 2000;
/// A worker's turn: then the documents of the other tabs get theirs, and what was read is shown.
constexpr int SLICE_MS = 100;

/// One worker for the PDF text of all open documents (it competes with the renderer for the processor only).
QThreadPool& textPool() {
    static QThreadPool* pool = [] {
        auto* p = new QThreadPool;  // (never destroyed: see ~DocumentTextIndex)
        p->setMaxThreadCount(1);
        p->setThreadPriority(QThread::LowPriority);
        // Before the program's statics go: a task still queued or running would release its poppler document
        // while poppler and glib are torn down (a crash at exit, which the crash handler then takes for a real one)
        qAddPostRoutine([] {
            QThreadPool& p = textPool();
            p.clear();
            p.waitForDone();
        });
        return p;
    }();
    return *pool;
}
}  // namespace

// --- PdfPageLayout ---------------------------------------------------------------------------------------------

PdfPageLayout PdfPageLayout::from(const char* utf8, const double* boxes, size_t count) {
    PdfPageLayout l;
    const QString original = QString::fromUtf8(utf8);
    // The boxes are per character (code point) of the text
    std::vector<size_t> codePoint(static_cast<size_t>(original.size()) + 1);
    size_t cp = 0;
    for (qsizetype i = 0; i < original.size(); ++i) {
        codePoint[static_cast<size_t>(i)] = cp;
        if (!original[i].isHighSurrogate()) {
            ++cp;
        }
    }
    const auto s = textmatch::simplify(original);
    l.text = s.text;
    l.boxes.resize(static_cast<size_t>(s.text.size()));
    for (qsizetype i = 0; i < s.text.size(); ++i) {
        if (s.text[i] == u' ') {
            continue;
        }
        const size_t k = codePoint[static_cast<size_t>(s.origin[static_cast<size_t>(i)])];
        if (k < count) {
            const double* b = boxes + 4 * k;
            l.boxes[static_cast<size_t>(i)] = QRectF(QPointF(b[0], b[1]), QPointF(b[2], b[3])).normalized();
        }
    }
    return l;
}

std::vector<QRectF> PdfPageLayout::rects(qsizetype start, qsizetype end) const {
    std::vector<QRectF> out;
    QRectF line;
    for (qsizetype i = std::max<qsizetype>(0, start); i < std::min<qsizetype>(end, boxes.size()); ++i) {
        const QRectF& b = boxes[static_cast<size_t>(i)];
        if (b.isNull()) {
            continue;
        }
        if (line.isNull()) {
            line = b;
            continue;
        }
        // The same line: they overlap by at least half the smaller height, and it goes on to the right
        const double overlap = std::min(line.bottom(), b.bottom()) - std::max(line.top(), b.top());
        if (overlap >= 0.5 * std::min(line.height(), b.height()) && b.left() >= line.left() - 1) {
            line |= b;
        } else {
            out.push_back(line);
            line = b;
        }
    }
    if (!line.isNull()) {
        out.push_back(line);
    }
    return out;
}

// --- texts of elements -----------------------------------------------------------------------------------------

std::vector<ElementText> elementTexts(const XojPage& page) {
    std::vector<ElementText> out;
    for (const Layer* l: page.getLayersView()) {
        if (!l->isVisible()) {
            continue;
        }
        for (const auto& e: l->getElementsView()) {
            if (e->getType() != ELEMENT_TEXT) {
                continue;
            }
            const auto* text = static_cast<const Text*>(e);
            if (text->isMarkdown()) {
                int item = 0;
                for (const std::string& shown: md::shownTexts(*text)) {
                    out.push_back({text, item++, QString::fromStdString(shown)});
                }
            } else {
                out.push_back({text, -1, QString::fromStdString(text->getText())});
            }
        }
    }
    return out;
}

std::vector<QRectF> elementRects(const ElementText& piece, qsizetype start, qsizetype end) {
    const int from = static_cast<int>(QStringView(piece.shown).first(start).toUtf8().size());
    const int to = from + static_cast<int>(QStringView(piece.shown).sliced(start, end - start).toUtf8().size());
    std::vector<md::Rect> rects;
    if (piece.item >= 0) {
        rects = md::shownRects(*piece.element, static_cast<size_t>(piece.item), from, to);
    } else {
        // As upstream's Text::findText places it: the layout at the text's origin
        md::Item it;
        it.layout = piece.element->createPangoLayout();
        const std::string& text = piece.element->getText();
        pango_layout_set_text(it.layout.get(), text.c_str(), static_cast<int>(text.size()));
        it.x = piece.element->getOrigin().x;
        it.y = piece.element->getOrigin().y;
        rects = md::textRects(it, from, to);
    }
    std::vector<QRectF> out;
    out.reserve(rects.size());
    for (const md::Rect& r: rects) {
        out.emplace_back(r.x, r.y, r.width, r.height);
    }
    return out;
}

// --- a reader of the text of PDF pages -------------------------------------------------------------------------

namespace {
/// The text of a poppler page and the box of each of its characters
PdfPageLayout layoutOf(PopplerPage* page) {
    char* raw = poppler_page_get_text(page);
    PopplerRectangle* rects = nullptr;
    guint n = 0;
    poppler_page_get_text_layout(page, &rects, &n);  // (the text of the page is built once for both)
    std::vector<double> boxes;
    boxes.reserve(4 * n);
    for (guint i = 0; i < n; ++i) {
        boxes.insert(boxes.end(), {rects[i].x1, rects[i].y1, rects[i].x2, rects[i].y2});
    }
    g_free(rects);
    PdfPageLayout l = PdfPageLayout::from(raw ? raw : "", boxes.data(), n);
    g_free(raw);
    return l;
}

PopplerDocument* openPdf(const fs::path& file) {
    // (Util::toUri: a path is wchar_t on Windows, and glib wants UTF-8 file names there)
    const std::optional<std::string> uri = Util::toUri(file);
    GError* error = nullptr;
    PopplerDocument* doc = uri ? poppler_document_new_from_file(uri->c_str(), nullptr, &error) : nullptr;
    if (error) {
        g_error_free(error);
    }
    return doc;
}
}  // namespace

PdfLayoutReader::PdfLayoutReader(fs::path pdf): file(std::move(pdf)) {}

PdfLayoutReader::~PdfLayoutReader() {
    if (doc) {
        g_object_unref(doc);
    }
}

::_PopplerDocument* PdfLayoutReader::document() {
    if (!doc && !failed) {
        doc = openPdf(file);
        failed = !doc;
    }
    return doc;
}

PdfPageLayout PdfLayoutReader::layout(int nr) {
    document();
    if (!doc || nr < 0 || nr >= poppler_document_get_n_pages(doc)) {
        return {};
    }
    PopplerPage* page = poppler_document_get_page(doc, nr);
    if (!page) {
        return {};
    }
    PdfPageLayout l = layoutOf(page);
    g_object_unref(page);
    return l;
}

std::vector<QRectF> termRects(const XojPage& page, PdfLayoutReader* pdf, const std::vector<textmatch::Term>& terms) {
    std::vector<QRectF> out;
    if (terms.empty()) {
        return out;
    }
    if (pdf && page.getBackgroundType().isPdfPage()) {
        const PdfPageLayout layout = pdf->layout(static_cast<int>(page.getPdfPageNr()));
        for (const auto& m: textmatch::find(layout.text, terms)) {
            const auto rects = layout.rects(m.start, m.end);
            out.insert(out.end(), rects.begin(), rects.end());
        }
    }
    for (const ElementText& piece: elementTexts(page)) {
        const auto s = textmatch::simplify(piece.shown);
        for (const auto& m: textmatch::find(s.text, terms)) {
            const auto rects = elementRects(piece, s.origin[static_cast<size_t>(m.start)],
                                            s.origin[static_cast<size_t>(m.end - 1)] + 1);
            out.insert(out.end(), rects.begin(), rects.end());
        }
    }
    return out;
}

// --- the worker ------------------------------------------------------------------------------------------------

struct DocumentTextIndex::Worker {
    std::mutex mtx;
    std::condition_variable idle;
    DocumentTextIndex* owner = nullptr;  ///< nullptr: gone
    fs::path file;
    std::deque<int> layoutsWanted;  ///< front first
    std::set<int> textWanted;
    int focus = 0;
    bool queued = false;   ///< a task for it is in the pool or running
    bool working = false;  ///< a page is being read (outside the lock)
    bool releaseWanted = false;
    PopplerDocument* doc = nullptr;  ///< used by the task while queued, else by anyone holding mtx
    bool openFailed = false;

    ~Worker() {
        if (doc) {
            g_object_unref(doc);
        }
    }
    /// Start a task if none is there (mtx held)
    static void queue(const std::shared_ptr<Worker>& w, int priority) {
        if (!w->queued) {
            w->queued = true;
            textPool().start([w] { drain(w); }, priority);
        }
    }
    bool open() {
        if (!doc && !openFailed) {
            doc = openPdf(file);
            openFailed = !doc;  // (e.g. with a password: its text is not searched)
        }
        return doc != nullptr;
    }
    static void drain(const std::shared_ptr<Worker>& w);
};

void DocumentTextIndex::Worker::drain(const std::shared_ptr<Worker>& w) {
    QElapsedTimer slice;
    slice.start();
    std::vector<std::pair<int, QString>> texts;
    auto post = [&] {
        if (texts.empty()) {
            return;
        }
        std::lock_guard lock(w->mtx);
        if (DocumentTextIndex* o = w->owner) {
            QMetaObject::invokeMethod(
                    o, [o, t = std::move(texts)]() mutable { o->received(std::move(t)); }, Qt::QueuedConnection);
        }
        texts.clear();
    };
    for (;;) {
        int nr = -1;
        bool layout = false;
        {
            std::unique_lock lock(w->mtx);
            w->working = false;
            w->idle.notify_all();
            if (!w->owner) {
                w->queued = false;
                return;
            }
            if (!w->layoutsWanted.empty()) {
                nr = w->layoutsWanted.front();
                w->layoutsWanted.pop_front();
                layout = true;
            } else if (!w->textWanted.empty()) {
                if (slice.elapsed() > SLICE_MS) {
                    // Show what was read, and let the other documents have their turn
                    lock.unlock();
                    post();
                    textPool().start([w] { drain(w); }, 0);
                    return;
                }
                // The page nearest to the reader's
                auto it = w->textWanted.lower_bound(w->focus);
                if (it == w->textWanted.end() ||
                    (it != w->textWanted.begin() && w->focus - *std::prev(it) < *it - w->focus)) {
                    it = it == w->textWanted.begin() ? it : std::prev(it);
                }
                nr = *it;
                w->textWanted.erase(it);
            } else {
                w->queued = false;
                if (w->releaseWanted && w->doc) {
                    g_object_unref(w->doc);
                    w->doc = nullptr;
                }
                lock.unlock();
                post();
                return;
            }
            w->working = true;
        }
        if (!layout) {
            // Not for the pages in view: they are rendered first
            RenderService::waitForVisiblePages(std::chrono::milliseconds(500));
        }
        QString text;
        std::shared_ptr<PdfPageLayout> pageLayout;
        if (w->open() && nr < poppler_document_get_n_pages(w->doc)) {
            PopplerPage* page = poppler_document_get_page(w->doc, nr);
            if (page) {
                if (layout) {
                    pageLayout = std::make_shared<PdfPageLayout>(layoutOf(page));
                } else {
                    char* raw = poppler_page_get_text(page);
                    text = QString::fromUtf8(raw ? raw : "").simplified();
                    g_free(raw);
                }
                g_object_unref(page);
            }
        }
        if (layout) {
            if (!pageLayout) {
                pageLayout = std::make_shared<PdfPageLayout>();
            }
            std::lock_guard lock(w->mtx);
            if (DocumentTextIndex* o = w->owner) {
                QMetaObject::invokeMethod(
                        o, [o, nr, l = std::shared_ptr<const PdfPageLayout>(pageLayout)] { o->receivedLayout(nr, l); },
                        Qt::QueuedConnection);
            }
        } else {
            texts.emplace_back(nr, std::move(text));
        }
    }
}

// --- the index -------------------------------------------------------------------------------------------------

void DocumentTextIndex::setSeeder(Seeder s) { seeder() = std::move(s); }
void DocumentTextIndex::setStartDelay(int ms) { startDelayMs = ms; }

DocumentTextIndex::DocumentTextIndex(DocumentSession& session): session(session) {
    rebuild();
    registerListener(&session);
    startTimer.setSingleShot(true);
    connect(&startTimer, &QTimer::timeout, this, &DocumentTextIndex::start);
    startTimer.start(startDelayMs);
    // Edited pages are read again once the edits pause
    dirtyTimer.setSingleShot(true);
    dirtyTimer.setInterval(300);
    connect(&dirtyTimer, &QTimer::timeout, this, [this] {
        std::vector<size_t> changed;
        for (size_t i = 0; i < pages.size(); ++i) {
            if (pages[i].dirty) {
                changed.push_back(i);
            }
        }
        refreshDirty();
        if (!changed.empty()) {
            Q_EMIT textChanged(changed);
        }
    });
}

DocumentTextIndex::~DocumentTextIndex() {
    unregisterListener();
    // The worker may be reading a page of this document: it is done with it before the document goes (it posts
    // nothing any more; its poppler instance goes with its last task)
    std::unique_lock lock(worker->mtx);
    worker->owner = nullptr;
    worker->idle.wait(lock, [&] { return !worker->working; });
}

void DocumentTextIndex::rebuild() {
    Document* doc = session.getDocument();
    fs::path file;
    size_t pdfPages = 0;
    std::vector<Page> fresh;
    {
        std::shared_lock lock(*doc);
        file = doc->getPdfFilepath();
        pdfPages = doc->getPdfPageCount();
        fresh.resize(doc->getPageCount());
        for (size_t i = 0; i < fresh.size(); ++i) {
            const PageRef p = doc->getPage(i);
            fresh[i].pdf = p->getBackgroundType().isPdfPage() && p->getPdfPageNr() < pdfPages
                                   ? static_cast<int>(p->getPdfPageNr())
                                   : -1;
        }
    }
    pages = std::move(fresh);
    if (!worker || file != pdf || pdfText.size() != pdfPages) {
        pdf = file;
        pdfText.assign(pdfPages, QString());
        pdfWords.assign(pdfPages, nullptr);
        pdfKnown.assign(pdfPages, 0);
        layouts.clear();
        if (worker) {
            std::lock_guard lock(worker->mtx);
            worker->owner = nullptr;  // (its last task ends it)
        }
        worker = std::make_shared<Worker>();
        worker->owner = this;
        worker->file = pdf;
    }
    countUnknown();
}

void DocumentTextIndex::countUnknown() {
    unknownPages = 0;
    for (const Page& p: pages) {
        if (p.pdf >= 0 && !pdfKnown[static_cast<size_t>(p.pdf)]) {
            ++unknownPages;
        }
    }
}

bool DocumentTextIndex::known(size_t page) const {
    return page < pages.size() && (pages[page].pdf < 0 || pdfKnown[static_cast<size_t>(pages[page].pdf)]);
}

void DocumentTextIndex::refresh(size_t index) {
    Page& page = pages[index];
    Document* doc = session.getDocument();
    QString joined;
    int pdfNr = -1;
    {
        std::shared_lock lock(*doc);
        if (index >= doc->getPageCount()) {
            return;
        }
        const PageRef p = doc->getPage(index);
        pdfNr = p->getBackgroundType().isPdfPage() && p->getPdfPageNr() < pdfText.size()
                        ? static_cast<int>(p->getPdfPageNr())
                        : -1;
        for (const ElementText& piece: elementTexts(*p)) {
            if (!joined.isEmpty()) {
                joined += u'\n';
            }
            joined += piece.shown.simplified();
        }
    }
    if (joined != page.elements) {
        page.elements = std::move(joined);
        page.words.reset();
    }
    page.dirty = false;
    if (pdfNr != page.pdf) {
        page.pdf = pdfNr;
        countUnknown();
        if (started && !known(index)) {
            wantText();
        }
    }
}

void DocumentTextIndex::refreshDirty() {
    for (size_t i = 0; i < pages.size(); ++i) {
        if (pages[i].dirty) {
            refresh(i);
        }
    }
}

int DocumentTextIndex::count(size_t page, QStringView query) {
    if (page >= pages.size()) {
        return 0;
    }
    if (pages[page].dirty) {
        refresh(page);
    }
    const Page& p = pages[page];
    int n = textmatch::count(p.elements, query);
    if (p.pdf >= 0 && pdfKnown[static_cast<size_t>(p.pdf)]) {
        n += textmatch::count(pdfText[static_cast<size_t>(p.pdf)], query);
    }
    return n;
}

int DocumentTextIndex::count(size_t page, const std::vector<textmatch::Term>& terms) {
    return terms.empty() ? 0 : count(page, words::Terms(terms));
}

int DocumentTextIndex::count(size_t page, const words::Terms& terms) {
    if (page >= pages.size() || terms.empty()) {
        return 0;
    }
    if (pages[page].dirty) {
        refresh(page);
    }
    const bool fuzzy = terms.fuzzy();
    const int nr = pages[page].pdf;
    int n = terms.count({pages[page].elements}, fuzzy ? elementWords(page) : nullptr);
    if (nr >= 0 && pdfKnown[static_cast<size_t>(nr)]) {
        n += terms.count({pdfText[static_cast<size_t>(nr)]}, fuzzy ? pdfWordsOf(nr) : nullptr);
    }
    return n;
}

bool DocumentTextIndex::contains(size_t page, const textmatch::Term& term) {
    return contains(page, words::Terms({term}), 0);
}

bool DocumentTextIndex::contains(size_t page, const words::Terms& terms, size_t i) {
    if (page >= pages.size()) {
        return false;
    }
    if (pages[page].dirty) {
        refresh(page);
    }
    const bool fuzzy = terms.fuzzy();
    const int nr = pages[page].pdf;
    return terms.contains(i, {pages[page].elements}, fuzzy ? elementWords(page) : nullptr) ||
           (nr >= 0 && pdfKnown[static_cast<size_t>(nr)] &&
            terms.contains(i, {pdfText[static_cast<size_t>(nr)]}, fuzzy ? pdfWordsOf(nr) : nullptr));
}

void DocumentTextIndex::prepareWords() {
    for (size_t i = 0; i < pages.size(); ++i) {
        if (pages[i].dirty) {
            refresh(i);
        }
        elementWords(i);
    }
    for (size_t nr = 0; nr < pdfText.size(); ++nr) {
        if (pdfKnown[nr]) {
            pdfWordsOf(static_cast<int>(nr));
        }
    }
}

const words::Vocabulary* DocumentTextIndex::elementWords(size_t page) {
    Page& p = pages[page];
    if (!p.words) {
        p.words = std::make_shared<const words::Vocabulary>(std::initializer_list<QStringView>{p.elements});
    }
    return p.words.get();
}

const words::Vocabulary* DocumentTextIndex::pdfWordsOf(int nr) {
    auto& w = pdfWords[static_cast<size_t>(nr)];
    if (!w) {
        w = std::make_shared<const words::Vocabulary>(
                std::initializer_list<QStringView>{pdfText[static_cast<size_t>(nr)]});
    }
    return w.get();
}

std::map<int, QString> DocumentTextIndex::pdfTexts() const {
    std::map<int, QString> out;
    for (size_t i = 0; i < pdfText.size(); ++i) {
        if (pdfKnown[i]) {
            out.emplace(static_cast<int>(i), pdfText[i]);
        }
    }
    return out;
}

void DocumentTextIndex::start() {
    if (started) {
        return;
    }
    started = true;
    startTimer.stop();
    std::vector<size_t> changed;
    if (seeder() && !pdf.empty()) {
        for (auto& [nr, text]: seeder()(pdf)) {
            if (nr >= 0 && static_cast<size_t>(nr) < pdfText.size() && !pdfKnown[static_cast<size_t>(nr)]) {
                setPdfText(nr, std::move(text), changed);
                ++seededCount;
            }
        }
    }
    countUnknown();
    wantText();
    if (!changed.empty()) {
        Q_EMIT textChanged(changed);
    }
    if (complete() && seededCount > 0) {
        Q_EMIT completed();
    }
}

void DocumentTextIndex::wantText() {
    std::set<int> wanted;
    for (const Page& p: pages) {
        if (p.pdf >= 0 && !pdfKnown[static_cast<size_t>(p.pdf)]) {
            wanted.insert(p.pdf);
        }
    }
    if (wanted.empty()) {
        return;
    }
    std::lock_guard lock(worker->mtx);
    worker->textWanted = std::move(wanted);
    worker->releaseWanted = false;
    Worker::queue(worker, 0);
}

void DocumentTextIndex::setPdfText(int nr, QString text, std::vector<size_t>& changed) {
    if (nr < 0 || static_cast<size_t>(nr) >= pdfText.size()) {
        return;
    }
    auto& known = pdfKnown[static_cast<size_t>(nr)];
    if (known && pdfText[static_cast<size_t>(nr)] == text) {
        return;
    }
    pdfText[static_cast<size_t>(nr)] = std::move(text);
    pdfWords[static_cast<size_t>(nr)].reset();
    known = 1;
    for (size_t i = 0; i < pages.size(); ++i) {
        if (pages[i].pdf == nr) {
            changed.push_back(i);
        }
    }
}

void DocumentTextIndex::received(std::vector<std::pair<int, QString>> texts) {
    const bool wasComplete = complete();
    std::vector<size_t> changed;
    for (auto& [nr, text]: texts) {
        setPdfText(nr, std::move(text), changed);
        ++readCount;
    }
    countUnknown();
    std::sort(changed.begin(), changed.end());
    if (!changed.empty()) {
        Q_EMIT textChanged(changed);
    }
    if (complete() && !wasComplete) {
        Q_EMIT completed();
    }
}

void DocumentTextIndex::receivedLayout(int nr, std::shared_ptr<const PdfPageLayout> layout) {
    layouts.remove_if([nr](const auto& e) { return e.first == nr; });
    layouts.emplace_front(nr, layout);
    while (layouts.size() > LAYOUTS) {
        layouts.pop_back();
    }
    // The text as it was read with the layout counts (the same as the index's, unless the file changed)
    const bool wasComplete = complete();
    std::vector<size_t> changed;
    setPdfText(nr, layout->text, changed);
    countUnknown();
    if (!changed.empty()) {
        Q_EMIT textChanged(changed);
    }
    Q_EMIT layoutReady(nr);
    if (complete() && !wasComplete) {
        Q_EMIT completed();
    }
}

const PdfPageLayout* DocumentTextIndex::layout(int nr, bool urgent) {
    for (auto it = layouts.begin(); it != layouts.end(); ++it) {
        if (it->first == nr) {
            layouts.splice(layouts.begin(), layouts, it);
            return layouts.front().second.get();
        }
    }
    if (nr < 0 || static_cast<size_t>(nr) >= pdfText.size()) {
        return nullptr;
    }
    std::lock_guard lock(worker->mtx);
    auto& wanted = worker->layoutsWanted;
    if (auto it = std::find(wanted.begin(), wanted.end(), nr); it != wanted.end()) {
        if (!urgent) {
            return nullptr;
        }
        wanted.erase(it);
    }
    if (urgent) {
        wanted.push_front(nr);
    } else {
        wanted.push_back(nr);
    }
    worker->releaseWanted = false;
    Worker::queue(worker, 1);
    return nullptr;
}

size_t DocumentTextIndex::textBytes() const {
    size_t bytes = 0;
    for (const QString& t: pdfText) {
        bytes += static_cast<size_t>(t.capacity()) * 2;
    }
    for (const Page& p: pages) {
        bytes += sizeof(Page) + static_cast<size_t>(p.elements.capacity()) * 2;
    }
    return bytes + pdfKnown.size();
}

size_t DocumentTextIndex::vocabularyBytes() const {
    size_t bytes = 0;
    for (const auto& w: pdfWords) {
        bytes += w ? w->bytes() : 0;
    }
    for (const Page& p: pages) {
        bytes += p.words ? p.words->bytes() : 0;
    }
    return bytes;
}

size_t DocumentTextIndex::layoutBytes() const {
    size_t bytes = 0;
    for (const auto& [nr, l]: layouts) {
        bytes += static_cast<size_t>(l->text.capacity()) * 2 + l->boxes.capacity() * sizeof(QRectF);
    }
    return bytes;
}

void DocumentTextIndex::setFocusPage(size_t page) {
    if (const int nr = pdfPageOf(page); nr >= 0) {
        std::lock_guard lock(worker->mtx);
        worker->focus = nr;
    }
}

void DocumentTextIndex::release() {
    std::lock_guard lock(worker->mtx);
    worker->releaseWanted = true;
    if (!worker->queued && worker->doc) {
        g_object_unref(worker->doc);
        worker->doc = nullptr;
    }
}

// --- document events (the index is up to date before the search hears of them) -----------------------------------

void DocumentTextIndex::documentChanged(DocumentChangeType type) {
    // (PDF_BOOKMARKS: another background PDF was loaded, e.g. pasted PDF pages joined the merged PDF)
    if (type == DOCUMENT_CHANGE_CLEARED || type == DOCUMENT_CHANGE_COMPLETE || type == DOCUMENT_CHANGE_PDF_BOOKMARKS) {
        if (QThread::currentThread() != thread()) {
            QMetaObject::invokeMethod(this, [this, type] { documentChanged(type); }, Qt::QueuedConnection);
            return;
        }
        rebuild();
        if (started) {
            wantText();
        }
        Q_EMIT reset();
    }
}

void DocumentTextIndex::pageChanged(size_t page) {
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, [this, page] { pageChanged(page); }, Qt::QueuedConnection);
        return;
    }
    if (page < pages.size()) {
        pages[page].dirty = true;
        dirtyTimer.start();
    }
}

void DocumentTextIndex::pageInserted(size_t page) {
    if (page > pages.size()) {
        return;
    }
    pages.insert(pages.begin() + static_cast<std::ptrdiff_t>(page), Page());
    refresh(page);
    countUnknown();
    if (started && !known(page)) {
        wantText();
    }
    Q_EMIT pageMoved(page, 1);
}

void DocumentTextIndex::pageDeleted(size_t page) {
    if (page >= pages.size()) {
        return;
    }
    pages.erase(pages.begin() + static_cast<std::ptrdiff_t>(page));
    countUnknown();
    Q_EMIT pageMoved(page, -1);
}

}  // namespace xqt
