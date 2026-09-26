#include "Annotations.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <mutex>
#include <shared_mutex>

#include <QRegularExpression>
#include <QStringList>
#include <cairo.h>
#include <poppler.h>

#include "model/Document.h"
#include "model/DocumentOutline.h"
#include "model/Element.h"
#include "model/Layer.h"
#include "model/Stroke.h"
#include "model/Text.h"
#include "model/XojPage.h"
#include "view/LayerView.h"
#include "view/View.h"

#include "DocumentChapters.h"
#include "DocumentLinks.h"
#include "LinkRewrite.h"
#include "MdDocument.h"
#include "session/DocumentTextIndex.h"
#include "session/StickyNote.h"

namespace xqt::annotations {

namespace {
std::mutex noteMutex;
void stickyNotesOf(const XojPage& page, std::vector<Item>& notes);
NoteSource& noteSource() {
    static NoteSource source = stickyNotesOf;
    return source;
}

QRectF rectOf(const xoj::util::Rectangle<double>& r) { return QRectF(r.x, r.y, r.width, r.height); }

uint32_t rgbOf(Color c);

/// The sticky notes of a page (qt/docs/sticky-notes.md), the NoteSource used unless another is set: a note's texts
/// as its text, "(handwriting)" for a note with ink only.
void stickyNotesOf(const XojPage& page, std::vector<Item>& notes) {
    for (const Layer* layer: page.getLayersView()) {
        if (!layer->isVisible() || !sticky::isNote(*layer)) {
            continue;
        }
        const auto look = sticky::lookOf(*layer);
        if (!look) {
            continue;
        }
        QStringList texts;
        bool ink = false;
        const Stroke* paper = sticky::paperOf(*layer);
        for (const Element* e: layer->getElementsView()) {
            if (e->getType() == ELEMENT_TEXT) {
                texts << QString::fromStdString(static_cast<const Text*>(e)->getText()).trimmed();
            } else if (e != paper) {
                ink = true;
            }
        }
        texts.removeAll(QString());
        Item note;
        note.kind = Kind::Note;
        note.rect = rectOf(look->rect);
        note.color = rgbOf(look->color);
        note.text = !texts.isEmpty() ? texts.join(u'\n') : ink ? QObject::tr("(handwriting)") : QString();
        notes.push_back(std::move(note));
    }
}

uint32_t rgbOf(Color c) { return uint32_t(c) & 0xffffffU; }

/// Strokes written one after another this near (points, about 6 mm) are one piece of handwriting.
constexpr double INK_NEAR = 18;

double distance(const QRectF& a, const QRectF& b) {
    const double dx = std::max({0.0, a.left() - b.right(), b.left() - a.right()});
    const double dy = std::max({0.0, a.top() - b.bottom(), b.top() - a.bottom()});
    return std::hypot(dx, dy);
}

/// A highlighter stroke covers a character: at the character's middle (x) the stroke's band overlaps the
/// character's core, its box moved down by a quarter of its height. So an underline at the bottom of a line covers
/// that line and not the next one (whose box may begin right there), a strike through or a highlight the line it is
/// on.
bool covers(const PageContent::Mark& m, const QRectF& b) {
    const double cx = b.center().x();
    const double h = b.height();
    const double coreTop = b.top() + 0.25 * h, coreBottom = b.bottom() + 0.25 * h;
    const double half = m.width / 2;
    const auto overlaps = [&](double top, double bottom) { return bottom > coreTop + 0.01 && top < coreBottom - 0.01; };
    for (size_t i = 1; i < m.points.size(); ++i) {
        const QPointF& p = m.points[i - 1];
        const QPointF& q = m.points[i];
        if (cx < std::min(p.x(), q.x()) - 0.5 || cx > std::max(p.x(), q.x()) + 0.5) {
            continue;
        }
        if (std::abs(q.x() - p.x()) < 0.5) {  // (upright: all of it)
            if (overlaps(std::min(p.y(), q.y()) - half, std::max(p.y(), q.y()) + half)) {
                return true;
            }
            continue;
        }
        const double y = p.y() + (q.y() - p.y()) * (cx - p.x()) / (q.x() - p.x());
        if (overlaps(y - half, y + half)) {
            return true;
        }
    }
    return false;
}

/// The text of the characters `covered` says, in the PDF's order: spaces between covered characters kept, the gaps
/// between separate pieces as " … ".
QString coveredText(const PdfPageLayout& layout, const std::vector<char>& covered) {
    QString out;
    qsizetype last = -1;  // the last covered character
    for (qsizetype i = 0; i < layout.text.size(); ++i) {
        if (!covered[static_cast<size_t>(i)]) {
            continue;
        }
        if (last >= 0 && i > last + 1) {
            bool onlySpaces = true;
            for (qsizetype k = last + 1; k < i; ++k) {
                onlySpaces = onlySpaces && layout.text[k].isSpace();
            }
            out += onlySpaces ? QStringLiteral(" ") : QStringLiteral(" … ");
        }
        out += layout.text[i];
        last = i;
    }
    return out.simplified();
}

struct MarkGroup {
    std::vector<size_t> marks;
    QRectF rect;
    uint32_t color = 0;
};

/// Highlighter strokes drawn one after another, in one color, on the same or the next lines: one highlight.
std::vector<MarkGroup> groupMarks(const std::vector<PageContent::Mark>& marks) {
    std::vector<MarkGroup> groups;
    for (size_t i = 0; i < marks.size(); ++i) {
        const auto& m = marks[i];
        if (!groups.empty()) {
            MarkGroup& g = groups.back();
            const double gap = std::max(m.box.height(), 6.0) + 2;
            if (g.color == m.color && m.box.top() <= g.rect.bottom() + gap && m.box.bottom() >= g.rect.top() - gap) {
                g.marks.push_back(i);
                g.rect |= m.box;
                continue;
            }
        }
        groups.push_back({{i}, m.box, m.color});
    }
    return groups;
}

/// Pieces of handwriting: a stroke joins the group written just before it when it is near it; groups that overlap
/// are one.
std::vector<std::vector<size_t>> groupInk(const std::vector<QRectF>& strokes) {
    std::vector<std::vector<size_t>> groups;
    std::vector<QRectF> rects;
    for (size_t i = 0; i < strokes.size(); ++i) {
        if (!groups.empty() && distance(rects.back(), strokes[i]) <= INK_NEAR) {
            groups.back().push_back(i);
            rects.back() |= strokes[i];
            continue;
        }
        groups.push_back({i});
        rects.push_back(strokes[i]);
    }
    for (bool merged = true; merged;) {
        merged = false;
        for (size_t a = 0; a < groups.size() && !merged; ++a) {
            for (size_t b = a + 1; b < groups.size() && !merged; ++b) {
                if (rects[a].adjusted(-2, -2, 2, 2).intersects(rects[b])) {
                    groups[a].insert(groups[a].end(), groups[b].begin(), groups[b].end());
                    rects[a] |= rects[b];
                    groups.erase(groups.begin() + static_cast<std::ptrdiff_t>(b));
                    rects.erase(rects.begin() + static_cast<std::ptrdiff_t>(b));
                    merged = true;
                }
            }
        }
    }
    return groups;
}

/// A Markdown box that is only a link (a link marker): its title and target.
bool linkOnly(const QString& text, QString& title, QString& target) {
    static const QRegularExpression re(QStringLiteral(R"(^\s*\[([^\]\n]*)\]\(\s*(<[^>\n]*>|[^)\s]+)\s*\)\s*$)"));
    const auto m = re.match(text);
    if (!m.hasMatch()) {
        return false;
    }
    title = m.captured(1).trimmed();
    target = m.captured(2);
    if (target.startsWith(u'<')) {
        target = target.mid(1, target.size() - 2);
    }
    return true;
}

/// The markup annotations of a PDF page (highlight, underline, squiggly, strike out) with the text under them.
void pdfAnnotations(PopplerDocument* doc, int pdfPage, const std::function<const PdfPageLayout&()>& textLayout,
                    size_t index, std::vector<Item>& out) {
    if (!doc || pdfPage < 0 || pdfPage >= poppler_document_get_n_pages(doc)) {
        return;
    }
    PopplerPage* page = poppler_document_get_page(doc, pdfPage);
    if (!page) {
        return;
    }
    double width = 0, height = 0;
    poppler_page_get_size(page, &width, &height);
    GList* mapping = poppler_page_get_annot_mapping(page);
    for (GList* l = mapping; l; l = l->next) {
        auto* m = static_cast<PopplerAnnotMapping*>(l->data);
        PopplerAnnot* annot = m->annot;
        const PopplerAnnotType type = poppler_annot_get_annot_type(annot);
        if (type != POPPLER_ANNOT_HIGHLIGHT && type != POPPLER_ANNOT_UNDERLINE && type != POPPLER_ANNOT_SQUIGGLY &&
            type != POPPLER_ANNOT_STRIKE_OUT) {
            continue;
        }
        if (gchar* name = poppler_annot_get_name(annot)) {
            const bool ours = g_str_has_prefix(name, "xopp:");  // (the hybrid PDF's own: its layers are read)
            g_free(name);
            if (ours) {
                continue;
            }
        }
        // Quadrilaterals in PDF space (y upwards, from the crop box): one rectangle per line, from the top left
        std::vector<QRectF> areas;
        if (GArray* quads = poppler_annot_text_markup_get_quadrilaterals(POPPLER_ANNOT_TEXT_MARKUP(annot))) {
            for (guint i = 0; i < quads->len; ++i) {
                const PopplerQuadrilateral& q = g_array_index(quads, PopplerQuadrilateral, i);
                const double xs[] = {q.p1.x, q.p2.x, q.p3.x, q.p4.x};
                const double ys[] = {q.p1.y, q.p2.y, q.p3.y, q.p4.y};
                const auto [x0, x1] = std::minmax_element(std::begin(xs), std::end(xs));
                const auto [y0, y1] = std::minmax_element(std::begin(ys), std::end(ys));
                areas.emplace_back(QPointF(*x0, height - *y1), QPointF(*x1, height - *y0));
            }
            g_array_unref(quads);
        }
        if (areas.empty()) {
            areas.emplace_back(QPointF(m->area.x1, height - m->area.y2), QPointF(m->area.x2, height - m->area.y1));
        }
        Item item;
        item.kind = Kind::PdfHighlight;
        item.page = index;
        for (const QRectF& a: areas) {
            item.rect |= a;
        }
        const PdfPageLayout& layout = textLayout();
        std::vector<char> covered(static_cast<size_t>(layout.text.size()), 0);
        for (size_t i = 0; i < layout.boxes.size(); ++i) {
            const QRectF& b = layout.boxes[i];
            if (b.isNull()) {
                continue;
            }
            for (const QRectF& a: areas) {
                if (a.adjusted(-1, -1, 1, 1).contains(b.center())) {
                    covered[i] = 1;
                    break;
                }
            }
        }
        item.text = coveredText(layout, covered);
        if (gchar* contents = poppler_annot_get_contents(annot)) {
            item.comment = QString::fromUtf8(contents).trimmed();
            g_free(contents);
        }
        if (PopplerColor* c = poppler_annot_get_color(annot)) {
            item.color = (uint32_t(c->red >> 8) << 16) | (uint32_t(c->green >> 8) << 8) | uint32_t(c->blue >> 8);
            poppler_color_free(c);
        }
        out.push_back(std::move(item));
    }
    poppler_page_free_annot_mapping(mapping);
    g_object_unref(page);
}
}  // namespace

QString nameOf(Kind kind) {
    switch (kind) {
        case Kind::Highlight:
            return QStringLiteral("highlight");
        case Kind::PdfHighlight:
            return QStringLiteral("pdfHighlight");
        case Kind::Text:
            return QStringLiteral("text");
        case Kind::Markdown:
            return QStringLiteral("markdown");
        case Kind::Ink:
            return QStringLiteral("ink");
        case Kind::Link:
            return QStringLiteral("link");
        case Kind::Note:
            return QStringLiteral("note");
    }
    return {};
}

void setNoteSource(NoteSource source) {
    std::lock_guard lock(noteMutex);
    noteSource() = std::move(source);
}

PageContent read(const XojPage& page) {
    PageContent c;
    c.width = page.getWidth();
    c.height = page.getHeight();
    if (page.getBackgroundType().isPdfPage()) {
        c.pdfPage = static_cast<int>(page.getPdfPageNr());
    }
    for (const Layer* layer: page.getLayersView()) {
        if (!layer->isVisible() || sticky::isNote(*layer)) {
            continue;  // (what is on a sticky note is the note's: listed as the note, NoteSource)
        }
        for (const Element* e: layer->getElementsView()) {
            if (e->getType() == ELEMENT_TEXT) {
                const auto* t = static_cast<const Text*>(e);
                c.boxes.push_back({t->isMarkdown(), rectOf(t->getBoundingBox()), QString::fromStdString(t->getText()),
                                   rgbOf(t->getColor())});
            } else if (e->getType() == ELEMENT_STROKE) {
                const auto* s = static_cast<const Stroke*>(e);
                if (s->getToolType() == StrokeTool::HIGHLIGHTER) {
                    PageContent::Mark m;
                    m.box = rectOf(s->getBoundingBox());
                    m.width = s->getWidth();
                    m.color = rgbOf(s->getColor());
                    m.points.reserve(s->getPointCount());
                    for (const Point& p: s->getPointVector()) {
                        m.points.emplace_back(p.x, p.y);
                    }
                    c.marks.push_back(std::move(m));
                } else if (s->getToolType() == StrokeTool::PEN) {
                    c.ink.push_back(rectOf(s->getBoundingBox()));
                }
            }
        }
    }
    std::lock_guard lock(noteMutex);
    if (noteSource()) {
        noteSource()(page, c.notes);
    }
    return c;
}

std::vector<Item> itemsOf(const PageContent& c, size_t index, PdfLayoutReader* pdf) {
    std::vector<Item> items;
    // The PDF's text only when something needs it (most pages of a big PDF have no marks): read once
    PdfPageLayout pageLayout;
    bool layoutRead = false;
    const std::function<const PdfPageLayout&()> textLayout = [&]() -> const PdfPageLayout& {
        if (!layoutRead && pdf && c.pdfPage >= 0) {
            pageLayout = pdf->layout(c.pdfPage);
        }
        layoutRead = true;
        return pageLayout;
    };
    std::vector<QRectF> ink = c.ink;

    // Highlights: the text under the highlighter's strokes
    for (const MarkGroup& g: groupMarks(c.marks)) {
        const PdfPageLayout& layout = textLayout();
        std::vector<char> covered(static_cast<size_t>(layout.text.size()), 0);
        bool any = false;
        for (size_t i = 0; i < layout.boxes.size(); ++i) {
            const QRectF& b = layout.boxes[i];
            if (b.isNull() || !b.intersects(g.rect.adjusted(-1, -b.height(), 1, b.height()))) {
                continue;
            }
            for (size_t k: g.marks) {
                if (covers(c.marks[k], b)) {
                    covered[i] = 1;
                    any = true;
                    break;
                }
            }
        }
        Item item;
        item.kind = Kind::Highlight;
        item.page = index;
        item.rect = g.rect;
        item.color = g.color;
        if (any) {
            item.text = coveredText(layout, covered);
        }
        if (item.text.isEmpty()) {
            // Not over PDF text: over a text box, else it marks handwriting (drawn with it)
            for (const auto& box: c.boxes) {
                if (!box.markdown && box.rect.intersects(g.rect)) {
                    item.text = box.text.simplified();
                    break;
                }
            }
        }
        if (item.text.isEmpty()) {
            for (size_t k: g.marks) {
                ink.push_back(c.marks[k].box);
            }
            continue;
        }
        items.push_back(std::move(item));
    }

    // The PDF's own highlights
    if (pdf && c.pdfPage >= 0) {
        pdfAnnotations(pdf->document(), c.pdfPage, textLayout, index, items);
    }

    // Text boxes, Markdown boxes, link markers
    for (const auto& box: c.boxes) {
        if (box.text.trimmed().isEmpty()) {
            continue;
        }
        Item item;
        item.page = index;
        item.rect = box.rect;
        item.color = box.color;
        QString title, target;
        if (box.markdown && linkOnly(box.text, title, target)) {
            item.kind = Kind::Link;
            item.text = title;
            item.target = target;
        } else {
            item.kind = box.markdown ? Kind::Markdown : Kind::Text;
            item.text = box.text;
        }
        items.push_back(std::move(item));
    }

    // Handwriting in the margins and free areas: not the groups mostly over PDF text (marks, underlines, circles)
    const std::vector<QRectF> lines =
            ink.empty() ? std::vector<QRectF>() : textLayout().rects(0, textLayout().text.size());
    for (const auto& group: groupInk(ink)) {
        QRectF rect;
        size_t onText = 0;
        for (size_t k: group) {
            rect |= ink[k];
            if (std::any_of(lines.begin(), lines.end(), [&](const QRectF& l) { return l.intersects(ink[k]); })) {
                ++onText;
            }
        }
        if (2 * onText > group.size() || std::max(rect.width(), rect.height()) < 4) {
            continue;
        }
        Item item;
        item.kind = Kind::Ink;
        item.page = index;
        item.rect = rect;
        items.push_back(std::move(item));
    }

    for (Item note: c.notes) {
        note.kind = Kind::Note;
        note.page = index;
        items.push_back(std::move(note));
    }

    // Reading order: from the top (the same line: from the left)
    std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
        if (std::abs(a.rect.top() - b.rect.top()) > 4) {
            return a.rect.top() < b.rect.top();
        }
        return a.rect.left() < b.rect.left();
    });
    return items;
}

std::vector<Item> collect(Document& doc, PdfLayoutReader* pdf) {
    std::vector<Item> out;
    for (size_t i = 0;; ++i) {
        PageContent content;
        {
            std::shared_lock lock(doc);
            if (i >= doc.getPageCount()) {
                break;
            }
            content = read(*doc.getPage(i));
        }
        auto items = itemsOf(content, i, pdf);
        out.insert(out.end(), std::make_move_iterator(items.begin()), std::make_move_iterator(items.end()));
    }
    return out;
}

QImage drawArea(Document& doc, const PageRef& page, const QRectF& rect, double scale) {
    const int w = std::max(1, static_cast<int>(std::ceil(rect.width() * scale)));
    const int h = std::max(1, static_cast<int>(std::ceil(rect.height() * scale)));
    QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::white);
    cairo_surface_t* surface = cairo_image_surface_create_for_data(img.bits(), CAIRO_FORMAT_ARGB32, img.width(),
                                                                   img.height(), static_cast<int>(img.bytesPerLine()));
    cairo_t* cr = cairo_create(surface);
    cairo_scale(cr, scale, scale);
    cairo_translate(cr, -rect.x(), -rect.y());
    cairo_rectangle(cr, rect.x(), rect.y(), rect.width(), rect.height());
    cairo_clip(cr);
    {
        std::shared_lock lock(doc);
        for (const Layer* layer: page->getLayersView()) {
            if (layer->isVisible()) {
                xoj::view::LayerView(layer).draw(xoj::view::Context::createDefault(cr));
            }
        }
    }
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
    return img;
}

// --- the Markdown export ---------------------------------------------------------------------------------------

std::vector<Chapter> chaptersOf(Document& doc) {
    std::vector<Chapter> chapters;
    {
        std::shared_lock lock(doc);
        std::map<size_t, size_t> firstPageOf;  // PDF page -> the first page showing it
        for (size_t i = 0; i < doc.getPageCount(); ++i) {
            const PageRef p = doc.getPage(i);
            if (p->getBackgroundType().isPdfPage()) {
                firstPageOf.emplace(p->getPdfPageNr(), i);
            }
        }
        std::function<void(const DocumentOutline&, int)> walk = [&](const DocumentOutline& entries, int level) {
            for (const auto& e: entries) {
                if (auto it = firstPageOf.find(e.dest.getPdfPage()); it != firstPageOf.end()) {
                    chapters.push_back({QString::fromStdString(e.title), level, it->second});
                }
                walk(e.children, level + 1);
            }
        };
        walk(doc.getOutline(), 0);
    }
    if (chapters.empty()) {
        for (const auto& c: DocumentChapters::find(doc)) {
            chapters.push_back({QString::fromStdString(c.title), c.level, c.page});
        }
    }
    return chapters;
}

fs::path defaultFile(const fs::path& document) {
    return document.parent_path() / (document.stem().string() + ".annotations.md");
}

fs::path assetsFolder(const fs::path& markdownFile) {
    return markdownFile.parent_path() / (markdownFile.stem().string() + ".assets");
}

links::Link pageLink(const ExportInput& input, size_t page, const fs::path& markdownFile) {
    links::Link link;
    link.path = links::relativePath(markdownFile, input.document);
    link.page = static_cast<int>(page) + 1;
    if (page < input.pages.size()) {
        if (input.pages[page].pdfPage > 0) {
            link.pdfPage = input.pages[page].pdfPage;
        } else {
            link.text = links::fingerprint(input.pages[page].text);
        }
    }
    return link;
}

namespace {
/// Plain text as Markdown shows it: the characters that mean something escaped (also `$`: math).
QString escaped(const QString& text) {
    QString out;
    out.reserve(text.size());
    bool lineStart = true;
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar ch = text[i];
        if (QStringLiteral("\\`*_[]<>$|").contains(ch) ||
            (lineStart && (ch == u'#' || ch == u'-' || ch == u'+' || ch == u'='))) {
            out += u'\\';
        }
        out += ch;
        if (lineStart && ch.isDigit()) {
            // "1. " at a line's start would be a list
            qsizetype k = i;
            while (k + 1 < text.size() && text[k + 1].isDigit()) {
                out += text[++k];
            }
            i = k;
            if (i + 1 < text.size() && (text[i + 1] == u'.' || text[i + 1] == u')')) {
                out += u'\\';
            }
            lineStart = false;
            continue;
        }
        if (ch == u'\n') {
            lineStart = true;
        } else if (!ch.isSpace()) {
            lineStart = false;
        }
    }
    return out;
}

/// A Markdown box's own links, relative to the document, written relative to the Markdown file.
QString relinked(const QString& source, const fs::path& document, const fs::path& markdownFile) {
    if (document.parent_path() == markdownFile.parent_path()) {
        return source;
    }
    std::string text = source.toStdString();
    std::vector<LinkRewrite::Change> changes;
    for (const std::string& written: md::parse(text).links) {
        const QString target = QString::fromStdString(written);
        const auto link = links::parse(target);
        if (!link || link->path.isEmpty() || fs::path(link->path.toStdString()).is_absolute()) {
            continue;
        }
        const fs::path file = links::resolvePath(document.parent_path(), link->path);
        changes.push_back({target, DocumentLinks::relinked(target, markdownFile, file), false});
    }
    if (!changes.empty()) {
        LinkRewrite::rewriteMarkdown(text, changes);
    }
    return QString::fromStdString(text);
}

/// A link target of a link marker, written for the Markdown file: a document by its path from there, a place in the
/// document itself as a link to the document.
QString markerTarget(const QString& target, const fs::path& document, const fs::path& markdownFile) {
    auto link = links::parse(target);
    if (!link) {
        return target;  // a web address and the like
    }
    if (link->path.isEmpty()) {
        link->path = links::relativePath(markdownFile, document);
        return links::write(*link);
    }
    if (fs::path(link->path.toStdString()).is_absolute() || document.parent_path() == markdownFile.parent_path()) {
        return target;
    }
    return DocumentLinks::relinked(target, markdownFile, links::resolvePath(document.parent_path(), link->path));
}

QString quoted(const QString& text) {
    QStringList lines = text.split(u'\n');
    for (QString& l: lines) {
        l = l.isEmpty() ? QStringLiteral(">") : QStringLiteral("> ") + l;
    }
    return lines.join(u'\n');
}

QString bullet(const QString& text) {
    QStringList lines = text.trimmed().split(u'\n');
    for (qsizetype i = 0; i < lines.size(); ++i) {
        lines[i] = (i == 0 ? QStringLiteral("- ") : (lines[i].isEmpty() ? QString() : QStringLiteral("  "))) + lines[i];
    }
    return lines.join(u'\n');
}
}  // namespace

std::string markdown(const std::vector<Item>& items, const ExportInput& input, const fs::path& markdownFile,
                     std::vector<Picture>* pictures) {
    QString out;
    const QString docName = QString::fromStdString(input.document.filename().string());
    out += QStringLiteral("# Annotations: %1\n\n").arg(escaped(input.title));
    links::Link docLink;
    docLink.path = links::relativePath(markdownFile, input.document);
    out += links::markdown(docName, docLink) + QStringLiteral("\n");

    std::vector<size_t> order(items.size());
    for (size_t i = 0; i < order.size(); ++i) {
        order[i] = i;
    }
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return items[a].page < items[b].page; });

    // Headings: the chapters (with the chapters above them, once), else the pages
    std::vector<char> written(input.chapters.size(), 0);
    int chapter = -2;  // the chapter the last item was in (-1: before the first)
    size_t lastPage = SIZE_MAX;
    const auto chapterOf = [&](size_t page) {
        int found = -1;
        for (size_t c = 0; c < input.chapters.size(); ++c) {
            if (input.chapters[c].page <= page) {
                found = static_cast<int>(c);
            }
        }
        return found;
    };
    const auto heading = [&](int level, const QString& title) {
        out += QStringLiteral("\n") + QString(std::min(6, 2 + std::max(0, level)), u'#') + u' ' + escaped(title) +
               QStringLiteral("\n");
    };
    int pictureNo = 0;

    for (size_t idx: order) {
        const Item& item = items[idx];
        if (!input.chapters.empty()) {
            const int c = chapterOf(item.page);
            if (c != chapter) {
                chapter = c;
                if (c < 0) {
                    heading(0, QObject::tr("Beginning"));
                } else {
                    // The chapters it is in, from the outermost, that were not written yet
                    std::vector<int> chain{c};
                    for (int k = c - 1, level = input.chapters[static_cast<size_t>(c)].level; k >= 0 && level > 0; --k) {
                        if (input.chapters[static_cast<size_t>(k)].level < level) {
                            chain.push_back(k);
                            level = input.chapters[static_cast<size_t>(k)].level;
                        }
                    }
                    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
                        if (!written[static_cast<size_t>(*it)]) {
                            written[static_cast<size_t>(*it)] = 1;
                            const Chapter& ch = input.chapters[static_cast<size_t>(*it)];
                            heading(ch.level, ch.title);
                        }
                    }
                }
            }
        } else if (item.page != lastPage) {
            heading(0, QObject::tr("Page %1").arg(item.page + 1));
        }
        lastPage = item.page;

        const QString place = links::markdown(QObject::tr("p. %1").arg(item.page + 1),
                                              pageLink(input, item.page, markdownFile));
        out += u'\n';
        switch (item.kind) {
            case Kind::Highlight:
            case Kind::PdfHighlight: {
                out += quoted(escaped(item.text)) + QStringLiteral(" (") + place + QStringLiteral(")\n");
                if (!item.comment.isEmpty()) {
                    out += QStringLiteral(">\n") + quoted(escaped(item.comment)) + u'\n';
                }
                break;
            }
            case Kind::Text:
                out += bullet(escaped(item.text)) + QStringLiteral(" (") + place + QStringLiteral(")\n");
                break;
            case Kind::Markdown:
                out += quoted(relinked(item.text.trimmed(), input.document, markdownFile)) + QStringLiteral("\n>\n> (") +
                       place + QStringLiteral(")\n");
                break;
            case Kind::Link: {
                QString title = item.text;
                title.replace(u'[', QStringLiteral("\\[")).replace(u']', QStringLiteral("\\]"));
                QString target = markerTarget(item.target, input.document, markdownFile);
                if (target.contains(u' ')) {
                    target = u'<' + target + u'>';
                }
                out += QStringLiteral("- [%1](%2) (").arg(title, target) + place + QStringLiteral(")\n");
                break;
            }
            case Kind::Ink:
                if (input.inkImages && pictures) {
                    const fs::path folder = assetsFolder(markdownFile);
                    const QString file =
                            QStringLiteral("p%1-%2.png").arg(item.page + 1).arg(++pictureNo, 2, 10, QLatin1Char('0'));
                    links::Link image;
                    image.path = QString::fromStdString(folder.filename().string()) + u'/' + file;
                    pictures->push_back({image.path, idx});
                    out += QStringLiteral("- ![%1](%2) (")
                                   .arg(QObject::tr("Handwriting, page %1").arg(item.page + 1), links::write(image)) +
                           place + QStringLiteral(")\n");
                } else {
                    out += QStringLiteral("- ") + place + u' ' + QObject::tr("(handwriting)") + u'\n';
                }
                break;
            case Kind::Note:
                out += bullet(QObject::tr("Note: %1").arg(escaped(item.text))) + QStringLiteral(" (") + place +
                       QStringLiteral(")\n");
                break;
        }
    }
    if (items.empty()) {
        out += u'\n' + QObject::tr("No annotations.") + u'\n';
    }
    return out.toStdString();
}

}  // namespace xqt::annotations
