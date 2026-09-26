/*
 * xournal-qt: the title of a PDF (see PdfTitle.h).
 *
 * @license GNU GPLv2 or later
 */
#include "PdfTitle.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <vector>

#include <QRegularExpression>
#include <glib.h>
#include <poppler.h>

#include "util/PathUtil.h"

namespace xqt::pdftitle {

bool plausibleMeta(const QString& title, const QString& fileName) {
    const QString t = title.simplified();
    if (t.size() < 4 || t.compare(fileName, Qt::CaseInsensitive) == 0 ||
        t.compare(QString(fileName).section(QLatin1Char('.'), 0, -2), Qt::CaseInsensitive) == 0) {
        return false;
    }
    static const QRegularExpression junk(
            QStringLiteral("\\.(pdf|dvi|ps|eps|docx?|odt|rtf|tex|pptx?|indd|qxd|xopp|txt)$|^Microsoft (Word|PowerPoint)"
                           "|^(untitled|title|no title|document|unknown|none|null)\\b|^arXiv:|^[\\d\\W_]+$|[/\\\\]"),
            QRegularExpression::CaseInsensitiveOption);
    if (junk.match(t).hasMatch()) {
        return false;
    }
    // At least one word of three letters
    static const QRegularExpression word(QStringLiteral("\\p{L}{3}"));
    return word.match(t).hasMatch();
}

namespace {
/// The largest text of a page: the runs of the largest font size (text that runs up or down left out)
QString largestText(PopplerPage* page) {
    char* raw = poppler_page_get_text(page);
    if (!raw) {
        return {};
    }
    const std::vector<char32_t> chars = [&] {
        const QList<uint> ucs4 = QString::fromUtf8(raw).toUcs4();
        return std::vector<char32_t>(ucs4.begin(), ucs4.end());
    }();
    g_free(raw);
    PopplerRectangle* rects = nullptr;
    guint n = 0;
    poppler_page_get_text_layout(page, &rects, &n);
    const bool haveRects = rects && n == chars.size();
    GList* attributes = poppler_page_get_text_attributes(page);

    struct Run {
        size_t from, to;  // [from, to]
        double size;
    };
    std::vector<Run> runs;
    for (GList* l = attributes; l; l = l->next) {
        const auto* a = static_cast<PopplerTextAttributes*>(l->data);
        const size_t from = static_cast<size_t>(std::max(0, a->start_index));
        const size_t to = std::min(static_cast<size_t>(std::max(0, a->end_index)), chars.size() ? chars.size() - 1 : 0);
        if (chars.empty() || from > to) {
            continue;
        }
        // Letters in it, and where they are: a run that goes up or down the page is a stamp in the margin
        int letters = 0;
        double minX = 1e9, maxX = -1e9, minY = 1e9, maxY = -1e9;
        for (size_t i = from; i <= to; ++i) {
            if (QChar::isLetter(chars[i])) {
                ++letters;
                if (haveRects) {
                    const PopplerRectangle& r = rects[i];
                    minX = std::min({minX, r.x1, r.x2});
                    maxX = std::max({maxX, r.x1, r.x2});
                    minY = std::min({minY, r.y1, r.y2});
                    maxY = std::max({maxY, r.y1, r.y2});
                }
            }
        }
        const bool upright = !haveRects || letters < 4 || (maxX - minX) >= (maxY - minY) * 0.5;
        if (letters >= 2 && upright) {
            runs.push_back({from, to, a->font_size});
        }
    }
    poppler_page_free_text_attributes(attributes);
    g_free(rects);
    if (runs.empty()) {
        return {};
    }
    const double largest = std::max_element(runs.begin(), runs.end(), [](const Run& a, const Run& b) {
                               return a.size < b.size;
                           })->size;
    QString text;
    for (const Run& r: runs) {
        if (r.size < largest - 0.5) {
            continue;
        }
        std::u32string part(chars.begin() + static_cast<std::ptrdiff_t>(r.from),
                            chars.begin() + static_cast<std::ptrdiff_t>(r.to) + 1);
        text += QLatin1Char(' ') + QString::fromUcs4(part.data(), static_cast<qsizetype>(part.size()));
        if (text.size() > HEADING_CHARS) {
            break;
        }
    }
    text = text.simplified();
    // (the stamp may still be there when the page has no text layout)
    static const QRegularExpression stamp(QStringLiteral("arXiv:\\S+(\\s+\\[[^\\]]*\\])?(\\s+\\d{1,2}\\s+\\p{L}{3}\\s+\\d{4})?"));
    text.remove(stamp);
    text = text.simplified();
    if (text.size() > HEADING_CHARS) {
        const qsizetype cut = text.lastIndexOf(QLatin1Char(' '), HEADING_CHARS);
        text.truncate(cut > HEADING_CHARS / 2 ? cut : HEADING_CHARS);
    }
    return text;
}
}  // namespace

Titles read(const fs::path& pdf, int page) {
    Titles t;
    GError* error = nullptr;
    const std::optional<std::string> uri = Util::toUri(pdf);
    PopplerDocument* doc = uri ? poppler_document_new_from_file(uri->c_str(), nullptr, &error) : nullptr;
    if (!doc) {
        if (error) {
            g_error_free(error);
        }
        return t;
    }
    if (gchar* title = poppler_document_get_title(doc)) {
        const QString meta = QString::fromUtf8(title).simplified();
        g_free(title);
        if (plausibleMeta(meta, QString::fromStdString(pdf.filename().string()))) {
            t.meta = meta;
        }
    }
    if (page >= 0 && page < poppler_document_get_n_pages(doc)) {
        if (PopplerPage* p = poppler_document_get_page(doc, page)) {
            t.heading = largestText(p);
            g_object_unref(p);
        }
    }
    g_object_unref(doc);
    return t;
}

}  // namespace xqt::pdftitle
