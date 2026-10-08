#include "FormManifest.h"

#include <cmath>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <qpdf/Buffer.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFEFStreamObjectHelper.hh>
#include <qpdf/QPDFEmbeddedFileDocumentHelper.hh>

#include "model/Document.h"
#include "model/XojPage.h"
#include "session/InkText.h"
#include "session/TextMatch.h"
#include "session/WordMatch.h"

namespace xqt::hwr {

bool FormItem::textual() const {
    return kind != QLatin1String("drawing") && kind != QLatin1String("mark") && kind != QLatin1String("free");
}

QRectF FormItem::box() const {
    return {boxMm.x() * PT_PER_MM, boxMm.y() * PT_PER_MM, boxMm.width() * PT_PER_MM, boxMm.height() * PT_PER_MM};
}

QPointF FormItem::framed(QPointF p) const {
    const QPointF c = centre();
    return c + ink::turned(p - c, -angle);
}

bool FormItem::contains(QPointF p) const { return box().contains(framed(p)); }

QString FormManifest::languageOf(const FormItem& item) const {
    return !item.lang.isEmpty() ? item.lang : !language.isEmpty() ? language : QStringLiteral("en");
}

std::vector<const FormItem*> FormManifest::itemsOn(int page) const {
    std::vector<const FormItem*> out;
    for (const FormItem& i: items) {
        if (i.page == page) {
            out.push_back(&i);
        }
    }
    return out;
}

QStringList searchWords(const QString& text) {
    QStringList out;
    QStringList folded;
    textmatch::words(text, [&](qsizetype start, qsizetype end, QStringView word) {
        if (word.size() >= wordmatch::MIN_LETTERS && !folded.contains(word.toString())) {
            folded << word.toString();
            out << text.mid(start, end - start);
        }
    });
    return out;
}

namespace {
QStringList stringsOf(const QJsonValue& v) {
    QStringList out;
    for (const QJsonValue& s: v.toArray()) {
        out << s.toString().normalized(QString::NormalizationForm_C);
    }
    return out;
}
}  // namespace

FormManifest FormManifest::parse(const QByteArray& json) {
    FormManifest m;
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(json, &err);
    if (!doc.isObject()) {
        m.error = QStringLiteral("not a form manifest (%1)").arg(err.errorString());
        return m;
    }
    const QJsonObject o = doc.object();
    m.form = o.value(QStringLiteral("form")).toString();
    m.version = o.value(QStringLiteral("version")).toInt();
    m.language = o.value(QStringLiteral("language")).toString();
    m.pages = o.value(QStringLiteral("pages")).toInt();
    const QJsonArray size = o.value(QStringLiteral("page_size_mm")).toArray();
    if (size.size() == 2) {
        m.pageSizeMm = QSizeF(size.at(0).toDouble(), size.at(1).toDouble());
    }
    if (m.form.isEmpty() || !o.value(QStringLiteral("items")).isArray()) {
        m.error = QStringLiteral("not a form manifest (no \"form\" or \"items\")");
        return m;
    }
    for (const QJsonValue& v: o.value(QStringLiteral("items")).toArray()) {
        const QJsonObject it = v.toObject();
        FormItem i;
        i.id = it.value(QStringLiteral("id")).toString();
        i.page = it.value(QStringLiteral("page")).toInt(1);
        i.section = it.value(QStringLiteral("section")).toString();
        i.kind = it.value(QStringLiteral("kind")).toString(QStringLiteral("line"));
        i.text = it.value(QStringLiteral("text")).toString().normalized(QString::NormalizationForm_C);
        i.lang = it.value(QStringLiteral("lang")).toString();
        i.latex = it.value(QStringLiteral("latex")).toString();
        const QJsonArray box = it.value(QStringLiteral("box_mm")).toArray();
        if (i.id.isEmpty() || box.size() != 4) {
            m.error = QStringLiteral("item %1: no id or no box_mm").arg(m.items.size() + 1);
            return m;
        }
        i.boxMm = QRectF(box.at(0).toDouble(), box.at(1).toDouble(), box.at(2).toDouble(), box.at(3).toDouble());
        i.angle = it.value(QStringLiteral("angle")).toDouble();
        i.xHeightMm = it.value(QStringLiteral("x_height_mm")).toDouble();
        i.guides = it.value(QStringLiteral("guides")).toBool();
        i.tags = stringsOf(it.value(QStringLiteral("tags")));
        i.search = it.contains(QStringLiteral("search")) ? stringsOf(it.value(QStringLiteral("search")))
                                                          : (i.textual() ? searchWords(i.text) : QStringList());
        i.context = it.value(QStringLiteral("context")).toString();
        m.items.push_back(std::move(i));
    }
    return m;
}

QByteArray embeddedManifest(const fs::path& pdf, QString* name) {
    std::error_code ec;
    if (pdf.empty() || !fs::is_regular_file(pdf, ec)) {
        return {};
    }
    try {
        QPDF q;
        q.setSuppressWarnings(true);
        q.processFile(pdf.string().c_str());
        QPDFEmbeddedFileDocumentHelper efdh(q);
        for (const auto& [key, spec]: efdh.getEmbeddedFiles()) {
            const std::string file = spec->getFilename().empty() ? key : spec->getFilename();
            const std::string suffix = ".manifest.json";
            if (file.size() < suffix.size() || file.compare(file.size() - suffix.size(), suffix.size(), suffix) != 0) {
                continue;
            }
            auto buffer = spec->getEmbeddedFileStream().getStreamData(qpdf_dl_all);
            if (name) {
                *name = QString::fromStdString(file);
            }
            return {reinterpret_cast<const char*>(buffer->getBuffer()), static_cast<qsizetype>(buffer->getSize())};
        }
    } catch (const std::exception&) {
        return {};
    }
    return {};
}

FormManifest manifestOf(const Document& document, const fs::path& file, const QString& manifestFile) {
    if (!manifestFile.isEmpty()) {
        QFile f(manifestFile);
        if (!f.open(QIODevice::ReadOnly)) {
            FormManifest m;
            m.error = QStringLiteral("Cannot read %1").arg(manifestFile);
            return m;
        }
        return FormManifest::parse(f.readAll());
    }
    for (const fs::path& pdf: {document.getPdfFilepath(), file}) {
        if (QByteArray json = embeddedManifest(pdf); !json.isEmpty()) {
            return FormManifest::parse(json);
        }
    }
    FormManifest m;
    m.error = QStringLiteral("No form manifest: the background PDF has no attached *.manifest.json (give one with "
                             "--manifest)");
    return m;
}

int formPageOf(const XojPage& page, size_t index) {
    if (page.getBackgroundType().isPdfPage()) {
        return static_cast<int>(page.getPdfPageNr()) + 1;
    }
    return static_cast<int>(index) + 1;
}

std::vector<int> assign(const std::vector<InkStroke>& strokes, const std::vector<const FormItem*>& items) {
    std::vector<int> out(strokes.size(), -1);
    std::vector<QRectF> boxes;
    for (const FormItem* i: items) {
        boxes.push_back(i->box());
    }
    for (size_t s = 0; s < strokes.size(); ++s) {
        const auto& points = strokes[s].points;
        if (points.empty()) {
            continue;
        }
        size_t most = 0;
        int best = -1;
        for (size_t b = 0; b < items.size(); ++b) {
            // (quick: the stroke's box far from the box's circle)
            const QPointF c = boxes[b].center();
            const double r = std::hypot(boxes[b].width(), boxes[b].height()) / 2;
            if (!strokes[s].box.adjusted(-r, -r, r, r).contains(c)) {
                continue;
            }
            size_t n = 0;
            for (const QPointF& p: points) {
                n += boxes[b].contains(items[b]->framed(p)) ? 1 : 0;
            }
            if (n > most) {
                most = n;
                best = static_cast<int>(b);
            }
        }
        if (2 * most > points.size()) {
            out[s] = best;
        }
    }
    return out;
}

}  // namespace xqt::hwr
