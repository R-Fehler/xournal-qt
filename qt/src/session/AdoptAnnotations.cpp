#include "AdoptAnnotations.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <regex>
#include <set>
#include <sstream>

#include <QDateTime>
#include <QTimeZone>
#include <cairo.h>
#include <poppler.h>
#include <qpdf/Buffer.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFAnnotationObjectHelper.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFPageObjectHelper.hh>
#include <qpdf/QPDFWriter.hh>

#include "model/Font.h"
#include "model/Image.h"
#include "model/LineStyle.h"
#include "model/Point.h"
#include "model/Stroke.h"
#include "model/Text.h"
#include "util/Matrix.h"

#include "ElementTimes.h"
#include "PdfEncryption.h"
#include "StickyNote.h"

namespace xqt::adopt {

namespace {

using QH = QPDFObjectHandle;

// --- which annotations
// ------------------------------------------------------------------------------------------------

enum class Fate {
    Skip,     ///< ours, hidden, a link, a form field, a popup...: not counted
    Convert,  ///< made editable
    Keep,     ///< of another app, but nothing of ours stands for it: stays in the PDF as it is
};

std::string subtypeOf(QH a) {
    QH s = a.getKey("/Subtype");
    return s.isName() ? s.getName().substr(1) : std::string();
}

std::string text(QH v) { return v.isString() ? v.getUTF8Value() : std::string(); }

bool isOurs(QH a) { return text(a.getKey("/NM")).rfind("xopp:", 0) == 0 || a.hasKey("/XournalQt"); }

bool hasAppearance(QH a) {
    QH ap = a.getKey("/AP");
    if (!ap.isDictionary()) {
        return false;
    }
    QH n = ap.getKey("/N");
    return n.isStream() || n.isDictionary();
}

Fate fateOf(QH a) {
    if (!a.isDictionary() || isOurs(a)) {
        return Fate::Skip;
    }
    if (QH f = a.getKey("/F"); f.isInteger() && (f.getIntValue() & (2 | 32))) {
        return Fate::Skip;  // (hidden: nothing to see)
    }
    static const std::set<std::string> convert{"Ink",    "Highlight", "Underline", "StrikeOut", "Squiggly", "FreeText",
                                               "Square", "Circle",    "Line",      "Polygon",   "PolyLine", "Text"};
    static const std::set<std::string> skip{"Link",  "Widget", "Popup",       "FileAttachment", "Sound",
                                            "Movie", "Screen", "PrinterMark", "TrapNet",        "Watermark",
                                            "3D",    "Redact", "RichMedia",   "Projection"};
    const std::string type = subtypeOf(a);
    if (skip.count(type)) {
        return Fate::Skip;
    }
    if (type == "Stamp") {
        return hasAppearance(a) ? Fate::Convert : Fate::Keep;
    }
    if (type == "Ink") {
        return a.getKey("/InkList").isArray() ? Fate::Convert : Fate::Keep;
    }
    if (type == "FreeText") {  // (an empty text box shows nothing of its own)
        const std::string c = text(a.getKey("/Contents"));
        return c.find_first_not_of(" \t\r\n") != std::string::npos ? Fate::Convert : Fate::Keep;
    }
    if (type == "Square" || type == "Circle" || type == "Line" || type == "Polygon" || type == "PolyLine") {
        auto colored = [&](const char* key) {
            QH c = a.getKey(key);
            return c.isArray() && c.getArrayNItems() > 0;
        };
        return colored("/C") || colored("/IC") ? Fate::Convert : Fate::Skip;  // (no colour: invisible)
    }
    return convert.count(type) ? Fate::Convert : Fate::Keep;
}

std::vector<size_t> pagesOf(size_t count, const std::vector<size_t>& asked) {
    std::vector<size_t> pages;
    if (asked.empty()) {
        for (size_t i = 0; i < count; ++i) {
            pages.push_back(i);
        }
    } else {
        for (size_t p: asked) {
            if (p < count) {
                pages.push_back(p);
            }
        }
        std::sort(pages.begin(), pages.end());
        pages.erase(std::unique(pages.begin(), pages.end()), pages.end());
    }
    return pages;
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return s;
}

/// The app an annotation itself names (its private keys, its name); empty: none
std::string appOfAnnotation(QH a) {
    for (const std::string& key: a.getKeys()) {
        if (key.rfind("/AAPL:", 0) == 0) {
            return "Preview";  // (AnnotationKit's editing data: Preview, iOS Markup, PDFKit apps)
        }
    }
    const std::string nm = lower(text(a.getKey("/NM")));
    if (nm.rfind("okular-", 0) == 0) {
        return "Okular";
    }
    return {};
}

std::string producerOf(QPDF& q) {
    QH info = q.getTrailer().getKey("/Info");
    if (!info.isDictionary()) {
        return {};
    }
    return text(info.getKey("/Producer")) + "\n" + text(info.getKey("/Creator"));
}

// --- where things are on the shown page -----------------------------------------------------------------------------

/// PDF user space -> the page as shown (crop box, /Rotate; top left 0,0; y down)
struct Placement {
    double llx = 0, lly = 0, w = 0, h = 0;  ///< the crop box (within the media box)
    int rotate = 0;
    double shownW() const { return rotate % 180 ? h : w; }
    double shownH() const { return rotate % 180 ? w : h; }
    std::pair<double, double> map(double x, double y) const {
        const double u = x - llx, v = y - lly;
        switch (rotate) {
            case 90:
                return {v, u};
            case 180:
                return {w - u, v};
            case 270:
                return {h - v, w - u};
            default:
                return {u, h - v};
        }
    }
};

Placement placementOf(QPDFPageObjectHelper& page) {
    QH::Rectangle crop = page.getCropBox(false).getArrayAsRectangle();
    const QH::Rectangle media = page.getMediaBox(false).getArrayAsRectangle();
    crop = {std::max(crop.llx, media.llx), std::max(crop.lly, media.lly), std::min(crop.urx, media.urx),
            std::min(crop.ury, media.ury)};  // (as poppler shows it)
    Placement p;
    p.llx = std::min(crop.llx, crop.urx);
    p.lly = std::min(crop.lly, crop.ury);
    p.w = std::abs(crop.urx - crop.llx);
    p.h = std::abs(crop.ury - crop.lly);
    QH rot = page.getAttribute("/Rotate", false);
    int r = rot.isInteger() ? static_cast<int>(rot.getIntValue() % 360) : 0;
    p.rotate = ((r + 360) % 360) / 90 * 90;
    return p;
}

struct Box {
    double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    double width() const { return x1 - x0; }
    double height() const { return y1 - y0; }
};

/// A PDF rectangle (with an inset, /RD: left, bottom, right, top) as it is shown
Box shownBox(const Placement& pl, QH rect, QH rd = QH::newNull()) {
    if (!rect.isRectangle()) {
        return {};
    }
    QH::Rectangle r = rect.getArrayAsRectangle();
    if (rd.isArray() && rd.getArrayNItems() == 4) {
        r.llx += rd.getArrayItem(0).getNumericValue();
        r.lly += rd.getArrayItem(1).getNumericValue();
        r.urx -= rd.getArrayItem(2).getNumericValue();
        r.ury -= rd.getArrayItem(3).getNumericValue();
    }
    const auto [ax, ay] = pl.map(r.llx, r.lly);
    const auto [bx, by] = pl.map(r.urx, r.ury);
    return {std::min(ax, bx), std::min(ay, by), std::max(ax, bx), std::max(ay, by)};
}

std::vector<double> numbers(QH arr) {
    std::vector<double> out;
    if (!arr.isArray()) {
        return out;
    }
    for (int i = 0; i < arr.getArrayNItems(); ++i) {
        if (QH n = arr.getArrayItem(i); n.isNumber()) {
            out.push_back(n.getNumericValue());
        }
    }
    return out;
}

// --- the look --------------------------------------------------------------------------------------------------------

uint8_t byte(double v) { return static_cast<uint8_t>(std::lround(std::clamp(v, 0.0, 1.0) * 255)); }

/// A PDF colour array (grey, RGB, CMYK); nothing for an empty one (transparent)
std::optional<Color> colorOf(const std::vector<double>& c) {
    switch (c.size()) {
        case 1:
            return Color(byte(c[0]), byte(c[0]), byte(c[0]));
        case 3:
            return Color(byte(c[0]), byte(c[1]), byte(c[2]));
        case 4:
            return Color(byte((1 - c[0]) * (1 - c[3])), byte((1 - c[1]) * (1 - c[3])), byte((1 - c[2]) * (1 - c[3])));
        default:
            return std::nullopt;
    }
}

double borderWidth(QH a) {
    if (QH bs = a.getKey("/BS"); bs.isDictionary() && bs.getKey("/W").isNumber()) {
        return bs.getKey("/W").getNumericValue();
    }
    if (const auto b = numbers(a.getKey("/Border")); b.size() >= 3) {
        return b[2];
    }
    return 1;
}

std::vector<double> dashesOf(QH a, double width) {
    QH bs = a.getKey("/BS");
    if (!bs.isDictionary() || !bs.getKey("/S").isNameAndEquals("/D")) {
        return {};
    }
    std::vector<double> d = numbers(bs.getKey("/D"));
    if (d.empty()) {
        d = {3};
    }
    if (d.size() == 1) {
        d.push_back(d[0]);
    }
    (void)width;
    return d;
}

double opacityOf(QH a) {
    QH ca = a.getKey("/CA");
    return ca.isNumber() ? std::clamp(ca.getNumericValue(), 0.0, 1.0) : 1.0;
}

/// Whether the annotation's appearance draws translucently or multiplies (a highlighter pen), as some apps write it
/// instead of /CA
bool appearanceTranslucent(QH a) {
    QH ap = a.getKey("/AP");
    QH n = ap.isDictionary() ? ap.getKey("/N") : QH::newNull();
    if (n.isDictionary() && !n.isStream()) {  // (appearance states: the one shown)
        QH as = a.getKey("/AS");
        n = as.isName() ? n.getKey(as.getName()) : QH::newNull();
    }
    if (!n.isStream()) {
        return false;
    }
    QH res = n.getDict().getKey("/Resources");
    QH gs = res.isDictionary() ? res.getKey("/ExtGState") : QH::newNull();
    if (!gs.isDictionary()) {
        return false;
    }
    for (const std::string& k: gs.getKeys()) {
        QH g = gs.getKey(k);
        if (!g.isDictionary()) {
            continue;
        }
        for (const char* key: {"/CA", "/ca"}) {
            if (QH v = g.getKey(key); v.isNumber() && v.getNumericValue() < 0.8) {
                return true;
            }
        }
        if (QH bm = g.getKey("/BM"); bm.isNameAndEquals("/Multiply") || bm.isNameAndEquals("/Darken")) {
            return true;
        }
    }
    return false;
}

/// When it was made (/CreationDate, else /M): milliseconds since 1970 (0: not known)
int64_t timeOf(QH a) {
    std::string d = text(a.getKey("/CreationDate"));
    if (d.empty()) {
        d = text(a.getKey("/M"));
    }
    if (d.rfind("D:", 0) == 0) {
        d = d.substr(2);
    }
    static const std::regex re(R"((\d{4})(\d{2})?(\d{2})?(\d{2})?(\d{2})?(\d{2})?([Zz+\-])?(\d{2})?'?(\d{2})?)");
    std::smatch m;
    if (!std::regex_search(d, m, re) || m.position(0) != 0) {
        return 0;
    }
    auto part = [&](int i, int fallback) { return m[i].matched ? std::stoi(m[i].str()) : fallback; };
    QDateTime t(QDate(part(1, 1970), part(2, 1), part(3, 1)), QTime(part(4, 0), part(5, 0), part(6, 0)),
                QTimeZone::UTC);
    if (!t.isValid()) {
        return 0;
    }
    if (m[7].matched && (m[7].str() == "+" || m[7].str() == "-")) {
        const int offset = (part(8, 0) * 60 + part(9, 0)) * 60;
        t = t.addSecs(m[7].str() == "+" ? -offset : offset);
    }
    return t.toMSecsSinceEpoch();
}

void stamp(Element& e, int64_t at) {
    if (at > 0) {
        timeline::stampNew(e, at);
    } else {
        timeline::stampNew(e);
    }
}

std::unique_ptr<Stroke> makeStroke(const std::vector<std::pair<double, double>>& pts, Color color, double width,
                                   StrokeTool tool) {
    auto s = std::make_unique<Stroke>();
    s->setToolType(tool);
    s->setColor(color);
    s->setWidth(width > 0 ? width : 1);
    for (const auto& [x, y]: pts) {
        if (s->getPointCount() > 0) {
            const Point& last = s->getPointVector().back();
            if (std::abs(last.x - x) < 0.01 && std::abs(last.y - y) < 0.01) {
                continue;
            }
        }
        s->addPoint(Point(x, y, -1));
    }
    if (s->getPointCount() == 1) {  // (a dot)
        const Point p = s->getPointVector().front();
        s->addPoint(Point(p.x + 0.1, p.y, -1));
    }
    return s->getPointCount() >= 2 ? std::move(s) : nullptr;
}

// --- text ------------------------------------------------------------------------------------------------------------

struct TextStyle {
    double size = 12;
    std::optional<Color> color;
    std::string family = "Sans";
};

std::string familyOf(const std::string& pdfFont) {
    const std::string f = lower(pdfFont);
    if (f.find("cour") != std::string::npos || f.find("mono") != std::string::npos) {
        return "Monospace";
    }
    if (f.find("times") != std::string::npos || f.find("tiro") != std::string::npos ||
        (f.find("serif") != std::string::npos && f.find("sans") == std::string::npos)) {
        return "Serif";
    }
    return "Sans";
}

/// From /DA ("/Helv 12 Tf 1 0 0 rg") and /DS ("font: 12pt Helvetica; color:#FF0000")
TextStyle styleOf(QH a) {
    TextStyle s;
    std::istringstream in(text(a.getKey("/DA")));
    std::vector<std::string> stack;
    for (std::string tok; in >> tok;) {
        auto num = [&](size_t back) { return std::atof(stack[stack.size() - back].c_str()); };
        if (tok == "Tf" && stack.size() >= 2) {
            if (num(1) > 0) {
                s.size = num(1);
            }
            s.family = familyOf(stack[stack.size() - 2]);
        } else if (tok == "rg" && stack.size() >= 3) {
            s.color = colorOf({num(3), num(2), num(1)});
        } else if (tok == "g" && !stack.empty()) {
            s.color = colorOf({num(1)});
        } else if (tok == "k" && stack.size() >= 4) {
            s.color = colorOf({num(4), num(3), num(2), num(1)});
        }
        stack.push_back(tok);
    }
    const std::string ds = text(a.getKey("/DS"));
    std::smatch m;
    static const std::regex size(R"((?:font-size\s*:\s*|font\s*:[^;]*?)(\d+(?:\.\d+)?)\s*pt)", std::regex::icase);
    if (std::regex_search(ds, m, size)) {
        s.size = std::atof(m[1].str().c_str());
    }
    static const std::regex color(R"(color\s*:\s*#([0-9a-fA-F]{6}))", std::regex::icase);
    if (std::regex_search(ds, m, color)) {
        const unsigned long v = std::stoul(m[1].str(), nullptr, 16);
        s.color = Color(uint8_t(v >> 16), uint8_t(v >> 8), uint8_t(v));
    }
    static const std::regex family(R"(font-family\s*:\s*([^;]+))", std::regex::icase);
    if (std::regex_search(ds, m, family)) {
        s.family = familyOf(m[1].str());
    }
    s.size = std::clamp(s.size, 4.0, 144.0);
    return s;
}

/// Plain text as Markdown text (a sticky note's text is Markdown): its marks kept as characters
std::string markdownEscaped(const std::string& plain) {
    std::string out;
    bool lineStart = true;
    for (char c: plain) {
        if (c == '\r') {
            c = '\n';
        }
        if (std::string("\\`*_[]<>|~").find(c) != std::string::npos ||
            (lineStart && (c == '#' || c == '-' || c == '+'))) {
            out += '\\';
        }
        out += c;
        lineStart = c == '\n';
    }
    return out;
}

std::string trimmed(const std::string& s) {
    const auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) {
        return {};
    }
    const auto e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

/// A sticky note with this text, its top left at x, y (kept on the page)
std::unique_ptr<Layer> makeNote(const std::string& contents, double x, double y, const Placement& pl, int64_t at) {
    size_t lines = 1, col = 0;
    for (char c: contents) {
        if (c == '\n' || ++col > 30) {
            ++lines;
            col = 0;
        }
    }
    sticky::Look look;
    look.rect.width = 180;
    look.rect.height = std::clamp(2 * sticky::TEXT_PADDING + 15.0 * double(lines), 50.0, 320.0);
    look.rect.x = std::clamp(x, 0.0, std::max(0.0, pl.shownW() - look.rect.width));
    look.rect.y = std::clamp(y, 0.0, std::max(0.0, pl.shownH() - look.rect.height));
    look.color = sticky::presetColors().front();
    std::unique_ptr<Layer> layer(sticky::makeNote(look));
    auto t = std::make_unique<Text>();
    const auto origin = sticky::textOrigin(look);
    t->setTransformation(xoj::util::Matrix::TRANSLATION(origin.x, origin.y));
    t->setFont(XojFont("Sans", 11));
    t->setColor(Color(0, 0, 0));
    t->setWrap(sticky::textWidth(look));
    t->setText(markdownEscaped(contents));
    for (const auto& e: layer->getElementsView()) {
        stamp(*const_cast<Element*>(e), at);
    }
    stamp(*t, at);
    layer->addElement(std::move(t));
    return layer;
}

// --- stamps: their appearance as a picture ------------------------------------------------------------------------

cairo_status_t toString(void* closure, const unsigned char* data, unsigned int length) {
    static_cast<std::string*>(closure)->append(reinterpret_cast<const char*>(data), length);
    return CAIRO_STATUS_SUCCESS;
}

/// The annotation's look drawn by poppler on a page of its own (its /Rect, the page's /Rotate) as a PNG
std::string pictureOf(QH annot, int rotate, double& pxW, double& pxH) {
    QH rect = annot.getKey("/Rect");
    if (!rect.isRectangle()) {
        return {};
    }
    const QH::Rectangle r = rect.getArrayAsRectangle();
    const double w = std::abs(r.urx - r.llx), h = std::abs(r.ury - r.lly);
    if (w < 1 || h < 1) {
        return {};
    }
    QPDF out;
    out.emptyPDF();
    QPDFAnnotationObjectHelper helper(annot);
    QH ap = helper.getAppearanceStream("/N");
    if (!ap.isStream()) {
        return {};
    }
    const std::string content = helper.getPageContentForAppearance("/Xqt0", 0, 0, 0);
    if (content.empty()) {
        return {};
    }
    QH xobjects = QH::newDictionary();
    xobjects.replaceKey("/Xqt0", out.copyForeignObject(ap));
    QH resources = QH::newDictionary();
    resources.replaceKey("/XObject", xobjects);
    QH page = QH::newDictionary();
    page.replaceKey("/Type", QH::newName("/Page"));
    page.replaceKey("/MediaBox", QH::newArray(QH::Rectangle(std::min(r.llx, r.urx), std::min(r.lly, r.ury),
                                                            std::max(r.llx, r.urx), std::max(r.lly, r.ury))));
    page.replaceKey("/Rotate", QH::newInteger(rotate));
    page.replaceKey("/Resources", resources);
    page.replaceKey("/Contents", QH::newStream(&out, content));
    QPDFPageDocumentHelper(out).addPage(QPDFPageObjectHelper(out.makeIndirectObject(page)), false);
    QPDFWriter writer(out);
    writer.setOutputMemory();
    writer.write();
    std::shared_ptr<Buffer> buffer = writer.getBufferSharedPointer();
    GBytes* bytes = g_bytes_new(buffer->getBuffer(), buffer->getSize());
    PopplerDocument* doc = poppler_document_new_from_bytes(bytes, nullptr, nullptr);
    g_bytes_unref(bytes);
    if (!doc) {
        return {};
    }
    std::string png;
    if (PopplerPage* p = poppler_document_get_page(doc, 0)) {
        double pw = 0, ph = 0;
        poppler_page_get_size(p, &pw, &ph);
        const double scale = std::min(4.0, 1600.0 / std::max(pw, ph));  // (sharp enough; at most 1600 px a side)
        const int iw = std::max(1, int(std::ceil(pw * scale))), ih = std::max(1, int(std::ceil(ph * scale)));
        cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, iw, ih);
        cairo_t* cr = cairo_create(surface);
        cairo_scale(cr, scale, scale);
        poppler_page_render(p, cr);
        cairo_destroy(cr);
        cairo_surface_flush(surface);
        cairo_surface_write_to_png_stream(surface, toString, &png);
        cairo_surface_destroy(surface);
        pxW = iw;
        pxH = ih;
        g_object_unref(p);
    }
    g_object_unref(doc);
    return png;
}

// --- one annotation ------------------------------------------------------------------------------------------------

struct Converter {
    const Placement& pl;
    PageMarks& marks;
    int64_t at = 0;

    void add(std::unique_ptr<Element> e) {
        if (e) {
            stamp(*e, at);
            marks.elements.push_back(std::move(e));
        }
    }

    /// The comment of an annotation that is not a note itself: a note beside it
    void comment(QH a, const Box& near) {
        const std::string c = trimmed(text(a.getKey("/Contents")));
        if (!c.empty()) {
            marks.notes.push_back(makeNote(c, near.x1 + 6, near.y0, pl, at));
        }
    }

    std::pair<double, double> pt(double x, double y) const { return pl.map(x, y); }

    bool ink(QH a) {
        const Color color = colorOf(numbers(a.getKey("/C"))).value_or(Color(0, 0, 0));
        const double width = borderWidth(a);
        const bool highlighter = opacityOf(a) < 0.8 || appearanceTranslucent(a);
        const auto dashes = dashesOf(a, width);
        QH list = a.getKey("/InkList");
        Box box{1e9, 1e9, -1e9, -1e9};
        bool any = false;
        for (int i = 0; i < list.getArrayNItems(); ++i) {
            const std::vector<double> n = numbers(list.getArrayItem(i));
            std::vector<std::pair<double, double>> pts;
            for (size_t k = 0; k + 1 < n.size(); k += 2) {
                pts.push_back(pt(n[k], n[k + 1]));
                box = {std::min(box.x0, pts.back().first), std::min(box.y0, pts.back().second),
                       std::max(box.x1, pts.back().first), std::max(box.y1, pts.back().second)};
            }
            auto s = makeStroke(pts, color, width, highlighter ? StrokeTool::HIGHLIGHTER : StrokeTool::PEN);
            if (s) {
                if (!dashes.empty()) {
                    LineStyle style;
                    style.setDashes(std::vector<double>(dashes));
                    s->setLineStyle(style);
                }
                add(std::move(s));
                any = true;
            }
        }
        if (any) {
            comment(a, box);
        }
        return any;
    }

    bool markup(QH a, const std::string& type) {
        const std::vector<double> q = numbers(a.getKey("/QuadPoints"));
        const Color color = colorOf(numbers(a.getKey("/C"))).value_or(Color(255, 230, 0));
        const double opacity = opacityOf(a);
        Box all{1e9, 1e9, -1e9, -1e9};
        bool any = false;
        auto piece = [&](std::array<std::pair<double, double>, 4> p) {
            double x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9;
            for (const auto& [x, y]: p) {
                x0 = std::min(x0, x), y0 = std::min(y0, y), x1 = std::max(x1, x), y1 = std::max(y1, y);
            }
            all = {std::min(all.x0, x0), std::min(all.y0, y0), std::max(all.x1, x1), std::max(all.y1, y1)};
            // The direction of the text: from the first point to the second (Acrobat's order), else the longer side
            const double dx = p[1].first - p[0].first, dy = p[1].second - p[0].second;
            const bool horizontal = std::hypot(dx, dy) > 0.5 ? std::abs(dx) >= std::abs(dy) : x1 - x0 >= y1 - y0;
            const double thick = horizontal ? y1 - y0 : x1 - x0;
            // (for a vertical line of text the "bottom" is its right side, as text turned clockwise has it)
            auto across = [&](double at) {
                return horizontal ? std::vector<std::pair<double, double>>{{x0, at}, {x1, at}} :
                                    std::vector<std::pair<double, double>>{{at, y0}, {at, y1}};
            };
            const double lo = horizontal ? y0 : x0, hi = horizontal ? y1 : x1;
            std::unique_ptr<Stroke> s;
            if (type == "Highlight") {
                s = makeStroke(across((lo + hi) / 2), color, thick, StrokeTool::HIGHLIGHTER);
                if (s && opacity < 1) {
                    s->setFill(int(std::lround(opacity * 255)));
                }
            } else {
                const double w = std::max(1.0, thick * 0.07);
                if (type == "StrikeOut") {
                    s = makeStroke(across((lo + hi) / 2), color, w, StrokeTool::HIGHLIGHTER);
                } else if (type == "Underline") {
                    s = makeStroke(across(hi - w / 2), color, w, StrokeTool::HIGHLIGHTER);
                } else {  // Squiggly: a zigzag along the bottom
                    const double amp = std::max(1.0, thick * 0.08), step = amp * 2;
                    std::vector<std::pair<double, double>> zig;
                    const double from = horizontal ? x0 : y0, to = horizontal ? x1 : y1;
                    int k = 0;
                    for (double t = from; t <= to + 0.01; t += step, ++k) {
                        const double off = hi - amp - (k % 2 ? amp : 0);
                        zig.push_back(horizontal ? std::make_pair(t, off) : std::make_pair(off, t));
                    }
                    s = makeStroke(zig, color, std::max(0.8, amp * 0.6), StrokeTool::HIGHLIGHTER);
                }
                if (s) {
                    s->setFill(230);  // (nearly solid, as marking selected PDF text underlines)
                }
            }
            if (s) {
                s->setStrokeCapStyle(StrokeCapStyle::BUTT);
                add(std::move(s));
                any = true;
            }
        };
        for (size_t k = 0; k + 7 < q.size(); k += 8) {
            piece({pt(q[k], q[k + 1]), pt(q[k + 2], q[k + 3]), pt(q[k + 4], q[k + 5]), pt(q[k + 6], q[k + 7])});
        }
        if (!any) {  // (no pieces: the rectangle)
            const Box b = shownBox(pl, a.getKey("/Rect"));
            if (b.width() > 0 && b.height() > 0) {
                piece({std::make_pair(b.x0, b.y0), std::make_pair(b.x1, b.y0), std::make_pair(b.x0, b.y1),
                       std::make_pair(b.x1, b.y1)});
            }
        }
        if (any) {
            comment(a, all);
        }
        return any;
    }

    bool freeText(QH a) {
        const std::string contents = text(a.getKey("/Contents"));
        const Box b = shownBox(pl, a.getKey("/Rect"), a.getKey("/RD"));
        if (trimmed(contents).empty() || b.width() <= 0) {
            return false;
        }
        const TextStyle style = styleOf(a);
        auto t = std::make_unique<Text>();
        t->setFont(XojFont(style.family, style.size));
        t->setColor(style.color.value_or(colorOf(numbers(a.getKey("/C"))).value_or(Color(0, 0, 0))));
        std::string plain = contents;
        std::replace(plain.begin(), plain.end(), '\r', '\n');
        t->setText(plain);
        const double pad = 2 + borderWidth(a);
        t->setWrap(std::max(8.0, b.width() - 2 * pad));
        t->setTransformation(xoj::util::Matrix::TRANSLATION(b.x0 + pad, b.y0 + pad));
        add(std::move(t));
        return true;
    }

    bool shape(QH a, const std::string& type) {
        const std::optional<Color> stroke = colorOf(numbers(a.getKey("/C")));
        const std::optional<Color> fill = colorOf(numbers(a.getKey("/IC")));
        if (!stroke && !fill) {
            return false;
        }
        double width = borderWidth(a);
        std::vector<std::pair<double, double>> pts;
        Box box{1e9, 1e9, -1e9, -1e9};
        if (type == "Square" || type == "Circle") {
            const Box b = shownBox(pl, a.getKey("/Rect"), a.getKey("/RD"));
            // (the border lies inside the rectangle)
            const Box in{b.x0 + width / 2, b.y0 + width / 2, b.x1 - width / 2, b.y1 - width / 2};
            if (in.width() <= 0 || in.height() <= 0) {
                return false;
            }
            box = b;
            if (type == "Square") {
                pts = {{in.x0, in.y0}, {in.x1, in.y0}, {in.x1, in.y1}, {in.x0, in.y1}, {in.x0, in.y0}};
            } else {
                const double cx = (in.x0 + in.x1) / 2, cy = (in.y0 + in.y1) / 2;
                const double rx = in.width() / 2, ry = in.height() / 2;
                constexpr int N = 64;
                for (int i = 0; i <= N; ++i) {
                    const double t = 2 * M_PI * i / N;
                    pts.emplace_back(cx + rx * std::cos(t), cy + ry * std::sin(t));
                }
            }
        } else {
            const std::vector<double> v = numbers(a.getKey(type == "Line" ? "/L" : "/Vertices"));
            for (size_t k = 0; k + 1 < v.size(); k += 2) {
                pts.push_back(pt(v[k], v[k + 1]));
            }
            if (pts.size() < 2) {
                return false;
            }
            if (type == "Polygon") {
                pts.push_back(pts.front());
            }
            for (const auto& [x, y]: pts) {
                box = {std::min(box.x0, x), std::min(box.y0, y), std::max(box.x1, x), std::max(box.y1, y)};
            }
        }
        const Color c = stroke.value_or(*fill);
        if (!stroke) {
            width = 0.5;
        }
        auto s = makeStroke(pts, c, width, StrokeTool::PEN);
        if (!s) {
            return false;
        }
        if (fill && type != "Line" && type != "PolyLine") {
            s->setFill(int(std::lround(opacityOf(a) * 255)));
            s->setFillColor(*fill);
        }
        if (const auto d = dashesOf(a, width); !d.empty()) {
            LineStyle style;
            style.setDashes(std::vector<double>(d));
            s->setLineStyle(style);
        }
        add(std::move(s));
        if (type == "Line" || type == "PolyLine") {
            const std::vector<std::string> ends = [&] {
                std::vector<std::string> e;
                QH le = a.getKey("/LE");
                for (int i = 0; le.isArray() && i < le.getArrayNItems(); ++i) {
                    e.push_back(le.getArrayItem(i).isName() ? le.getArrayItem(i).getName() : "");
                }
                return e;
            }();
            auto head = [&](std::pair<double, double> tip, std::pair<double, double> from, const std::string& kind) {
                if (kind != "/OpenArrow" && kind != "/ClosedArrow" && kind != "/ROpenArrow" &&
                    kind != "/RClosedArrow") {
                    return;
                }
                double dx = tip.first - from.first, dy = tip.second - from.second;
                const double len = std::hypot(dx, dy);
                if (len < 0.01) {
                    return;
                }
                dx /= len, dy /= len;
                if (kind[1] == 'R') {  // (pointing back)
                    dx = -dx, dy = -dy;
                }
                const double size = 3 * width + 6, cs = std::cos(M_PI / 6), sn = std::sin(M_PI / 6);
                const std::pair<double, double> w1{tip.first - size * (dx * cs - dy * sn),
                                                   tip.second - size * (dy * cs + dx * sn)};
                const std::pair<double, double> w2{tip.first - size * (dx * cs + dy * sn),
                                                   tip.second - size * (dy * cs - dx * sn)};
                std::vector<std::pair<double, double>> wing{w1, tip, w2};
                const bool closed = kind.find("Closed") != std::string::npos;
                if (closed) {
                    wing.push_back(w1);
                }
                if (auto h = makeStroke(wing, c, width, StrokeTool::PEN)) {
                    if (closed) {
                        h->setFill(255);
                        h->setFillColor(fill.value_or(c));
                    }
                    add(std::move(h));
                }
            };
            if (!ends.empty()) {
                head(pts.front(), pts[1], ends[0]);
            }
            if (ends.size() > 1) {
                head(pts.back(), pts[pts.size() - 2], ends[1]);
            }
        }
        comment(a, box);
        return true;
    }

    bool note(QH a) {
        const std::string c = trimmed(text(a.getKey("/Contents")));
        const Box b = shownBox(pl, a.getKey("/Rect"));
        if (c.empty()) {
            return true;  // (an empty note: nothing to keep; it goes)
        }
        marks.notes.push_back(makeNote(c, b.x0, b.y0, pl, at));
        return true;
    }

    bool picture(QH a) {
        double pxW = 0, pxH = 0;
        std::string png = pictureOf(a, pl.rotate, pxW, pxH);
        const Box b = shownBox(pl, a.getKey("/Rect"));
        if (png.empty() || b.width() <= 0 || b.height() <= 0) {
            return false;
        }
        auto image = std::make_unique<Image>();
        image->setImage(std::move(png));
        image->setTransformation({b.width() / pxW, 0, 0, b.height() / pxH, {b.x0, b.y0}});
        add(std::move(image));
        comment(a, b);
        return true;
    }

    /// One annotation of another app as elements of ours: by its PDF type, as the standard defines it. App-specific
    /// handling (once real exports show what GoodNotes, Drawboard or Preview write beyond the standard: e.g. pressure
    /// read from an appearance stream) plugs in here, chosen by the app (Scan::app / appOfAnnotation).
    bool convert(QH a) {
        at = timeOf(a);
        const std::string type = subtypeOf(a);
        try {
            if (type == "Ink") {
                return ink(a);
            }
            if (type == "Highlight" || type == "Underline" || type == "StrikeOut" || type == "Squiggly") {
                return markup(a, type);
            }
            if (type == "FreeText") {
                return freeText(a);
            }
            if (type == "Square" || type == "Circle" || type == "Line" || type == "Polygon" || type == "PolyLine") {
                return shape(a, type);
            }
            if (type == "Text") {
                return note(a);
            }
            if (type == "Stamp") {
                return picture(a);
            }
        } catch (const std::exception& e) {
            g_warning("Could not convert a %s annotation: %s", type.c_str(), e.what());
        }
        return false;
    }
};

std::string appOf(QPDF& q, const std::vector<QH>& found) {
    for (QH a: found) {
        if (std::string app = appOfAnnotation(a); !app.empty()) {
            return app;
        }
    }
    return appOfProducer(producerOf(q));
}

}  // namespace

std::string appOfProducer(const std::string& producerAndCreator) {
    const std::string p = lower(producerAndCreator);
    static const std::vector<std::pair<const char*, const char*>> apps{{"goodnotes", "GoodNotes"},
                                                                       {"drawboard", "Drawboard PDF"},
                                                                       {"xodo", "Xodo"},
                                                                       {"pdftron", "Xodo"},
                                                                       {"apryse", "Xodo"},
                                                                       {"zotero", "Zotero"},
                                                                       {"notability", "Notability"},
                                                                       {"pdf expert", "PDF Expert"},
                                                                       {"foxit", "Foxit"},
                                                                       {"okular", "Okular"},
                                                                       {"samsung notes", "Samsung Notes"},
                                                                       {"onenote", "OneNote"},
                                                                       {"acrobat", "Acrobat"},
                                                                       {"quartz pdfcontext", "Preview"},
                                                                       {"preview", "Preview"},
                                                                       {"macos version", "Preview"},
                                                                       {"mac os x", "Preview"},
                                                                       {"ios version", "Preview"},
                                                                       {"ipados", "Preview"}};
    for (const auto& [needle, name]: apps) {
        if (p.find(needle) != std::string::npos) {
            return name;
        }
    }
    return {};
}

std::string layerName(const std::string& app) { return app.empty() ? "From another app" : "From " + app; }

Scan scan(const fs::path& pdf, const std::vector<size_t>& pdfPages) {
    Scan s;
    try {
        QPDF q;
        q.setSuppressWarnings(true);
        PdfEncryption::openQpdf(q, pdf);
        auto pages = QPDFPageDocumentHelper(q).getAllPages();
        std::vector<QH> found;
        for (size_t p: pagesOf(pages.size(), pdfPages)) {
            QH annots = pages[p].getObjectHandle().getKey("/Annots");
            for (int i = 0; annots.isArray() && i < annots.getArrayNItems(); ++i) {
                QH a = annots.getArrayItem(i);
                if (fateOf(a) == Fate::Convert) {
                    ++s.count;
                    ++s.kinds[subtypeOf(a)];
                    found.push_back(a);
                }
            }
        }
        s.app = s.count ? appOf(q, found) : std::string();
        s.ok = true;
    } catch (const std::exception& e) {
        s.error = e.what();
    }
    return s;
}

Prepared prepare(const fs::path& pdf, const std::vector<size_t>& pdfPages, const fs::path& copy,
                 std::optional<MergedPdf::Kind> mark) {
    Prepared r;
    try {
        QPDF q;
        q.setSuppressWarnings(true);
        PdfEncryption::openQpdf(q, pdf);
        auto pages = QPDFPageDocumentHelper(q).getAllPages();
        std::vector<QH> found;
        for (size_t p: pagesOf(pages.size(), pdfPages)) {
            QH page = pages[p].getObjectHandle();
            QH annots = page.getKey("/Annots");
            if (!annots.isArray()) {
                continue;
            }
            const Placement pl = placementOf(pages[p]);
            PageMarks marks;
            std::set<QPDFObjGen> gone;
            std::vector<QH> keep;
            for (int i = 0; i < annots.getArrayNItems(); ++i) {
                QH a = annots.getArrayItem(i);
                const Fate fate = fateOf(a);
                if (fate == Fate::Keep) {
                    ++r.kept;
                }
                if (fate == Fate::Convert) {
                    found.push_back(a);
                    Converter c{pl, marks};
                    if (c.convert(a)) {
                        ++marks.converted;
                        if (a.isIndirect()) {
                            gone.insert(a.getObjGen());
                        }
                        if (QH popup = a.getKey("/Popup"); popup.isIndirect()) {
                            gone.insert(popup.getObjGen());
                        }
                        continue;
                    }
                    ++r.kept;
                }
                keep.push_back(a);
            }
            if (marks.converted == 0) {
                continue;
            }
            // The popups of converted annotations go with them (also those only pointing at their parent)
            std::vector<QH> left;
            for (QH a: keep) {
                QH parent = a.isDictionary() ? a.getKey("/Parent") : QH::newNull();
                if ((a.isIndirect() && gone.count(a.getObjGen())) ||
                    (parent.isIndirect() && gone.count(parent.getObjGen()))) {
                    continue;
                }
                left.push_back(a);
            }
            if (left.empty()) {
                page.removeKey("/Annots");
            } else {
                page.replaceKey("/Annots", QH::newArray(left));
            }
            r.converted += marks.converted;
            for (auto& e: marks.elements) {
                e->getBoundingBox();  // (sizes worked out here, not on the UI thread)
            }
            for (auto& l: marks.notes) {
                for (const auto& e: l->getElementsView()) {
                    e->getBoundingBox();
                }
            }
            r.pages[p] = std::move(marks);
        }
        r.app = appOf(q, found);
        if (r.converted > 0) {
            if (mark) {
                MergedPdf::mark(q, *mark);
            }
            std::error_code ec;
            fs::create_directories(copy.parent_path(), ec);
            MergedPdf::writeAtomically(q, copy);
            PdfEncryption::derive(copy, pdf);  // (a protected PDF: the copy has its password)
            r.copy = copy;
        }
        r.ok = true;
    } catch (const std::exception& e) {
        r = Prepared{};
        r.error = e.what();
    }
    return r;
}

}  // namespace xqt::adopt
