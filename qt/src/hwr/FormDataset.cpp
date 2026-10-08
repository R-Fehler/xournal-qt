#include "FormDataset.h"

#include <cmath>
#include <shared_mutex>

#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "model/Document.h"
#include "model/XojPage.h"
#include "session/DocumentSession.h"

#include "FormManifest.h"
#include "LineDataset.h"
#include "Recognizer.h"

namespace xqt::hwr {

namespace {
/// The strokes of a box as one line upright: turned by -angle, relative to their top-left, all in one word
LineInput lineOfBox(const std::vector<InkStroke>& page, const std::vector<uint32_t>& which, double angle) {
    LineInput line;
    QRectF box;
    for (const uint32_t i: which) {
        InkStroke s = angle == 0 ? page[i] : turned(page[i], -angle);
        box = line.strokes.empty() ? s.box : box.united(s.box);
        line.strokes.push_back(std::move(s));
    }
    InkWordBox word;
    for (uint32_t k = 0; k < line.strokes.size(); ++k) {
        InkStroke& s = line.strokes[k];
        for (QPointF& p: s.points) {
            p -= box.topLeft();
        }
        s.box.translate(-box.topLeft());
        word.strokes.push_back(k);
    }
    word.box = QRectF(QPointF(0, 0), box.size());
    line.words.push_back(std::move(word));
    line.size = box.size();
    return line;
}
}  // namespace

FormExportResult exportForm(const FormExport& job) {
    FormExportResult r;
    auto loaded = DocumentSession::loadFile(fs::path(job.document.toStdString()));
    if (!loaded.document) {
        r.error = QString::fromStdString(loaded.error.empty() ? "Cannot open " + job.document.toStdString()
                                                              : loaded.error);
        return r;
    }
    const FormManifest manifest = manifestOf(*loaded.document, fs::path(job.document.toStdString()), job.manifest);
    if (!manifest.valid()) {
        r.error = manifest.error;
        return r;
    }
    return exportForm(job, *loaded.document, manifest);
}

FormExportResult exportForm(const FormExport& job, Document& doc, const FormManifest& manifest) {
    FormExportResult r;
    r.form = manifest.form;
    const QDir out(job.out);
    if (!QDir().mkpath(out.filePath(QStringLiteral("images"))) ||
        !QDir().mkpath(out.filePath(QStringLiteral("strokes")))) {
        r.error = QStringLiteral("Cannot create %1").arg(job.out);
        return r;
    }
    const QString writer = job.writer.isEmpty() ? QStringLiteral("me") : job.writer;
    QStringList languages;
    QByteArray jsonl;
    std::vector<bool> seen(manifest.items.size(), false);  ///< items on a page of the document
    std::shared_lock lock(doc);
    for (size_t p = 0; p < doc.getPageCount(); ++p) {
        const XojPage& page = *doc.getPage(p);
        const int formPage = formPageOf(page, p);
        const std::vector<const FormItem*> items = manifest.itemsOn(formPage);
        if (items.empty()) {
            continue;
        }
        if (!manifest.pageSizeMm.isEmpty() &&
            (std::abs(page.getWidth() - manifest.pageSizeMm.width() * PT_PER_MM) > 2 ||
             std::abs(page.getHeight() - manifest.pageSizeMm.height() * PT_PER_MM) > 2)) {
            r.warnings << QStringLiteral("page %1 is %2 x %3 pt, the form's %4 x %5 mm: the boxes may not fit")
                                  .arg(p + 1)
                                  .arg(page.getWidth())
                                  .arg(page.getHeight())
                                  .arg(manifest.pageSizeMm.width())
                                  .arg(manifest.pageSizeMm.height());
        }
        const std::vector<InkStroke> strokes = strokesOf(page);
        const std::vector<int> owner = assign(strokes, items);
        std::vector<std::vector<uint32_t>> ofBox(items.size());
        for (uint32_t s = 0; s < owner.size(); ++s) {
            if (owner[s] < 0) {
                ++r.outside;
            } else {
                ofBox[static_cast<size_t>(owner[s])].push_back(s);
            }
        }
        for (size_t b = 0; b < items.size(); ++b) {
            const FormItem& item = *items[b];
            seen[static_cast<size_t>(&item - manifest.items.data())] = true;
            if (!item.forTraining()) {
                ++r.left;
                continue;
            }
            if (ofBox[b].empty()) {
                ++r.empty;
                continue;
            }
            const LineInput line = lineOfBox(strokes, ofBox[b], item.angle);
            const QString id = safeName(writer) + u'-' + safeName(manifest.form) + u'-' + safeName(item.id);
            const QString imagePath = QStringLiteral("images/") + id + QStringLiteral(".png");
            const QString strokesPath = QStringLiteral("strokes/") + id + QStringLiteral(".json");
            const QImage image = lineImage(line);
            if (image.isNull() || !image.save(out.filePath(imagePath), "PNG") ||
                !writeDatasetFile(out.filePath(strokesPath),
                                  QJsonDocument(strokesJson(line)).toJson(QJsonDocument::Compact))) {
                r.error = QStringLiteral("Cannot write into %1").arg(job.out);
                return r;
            }
            const QString lang = manifest.languageOf(item);
            if (!languages.contains(lang)) {
                languages << lang;
            }
            QJsonObject entry{{QStringLiteral("id"), id},
                              {QStringLiteral("image"), imagePath},
                              {QStringLiteral("text"), item.text},
                              {QStringLiteral("lang"), lang},
                              {QStringLiteral("writer"), writer},
                              {QStringLiteral("strokes"), strokesPath},
                              {QStringLiteral("angle"), item.angle},
                              {QStringLiteral("kind"), item.kind},
                              {QStringLiteral("box_id"), item.id},
                              {QStringLiteral("form"), manifest.form},
                              {QStringLiteral("page"), item.page}};
            if (item.kind == QLatin1String("math")) {
                entry.insert(QStringLiteral("math"), true);
                ++r.math;
            }
            jsonl += QJsonDocument(entry).toJson(QJsonDocument::Compact) + '\n';
            ++r.lines;
        }
    }
    int missing = 0;
    for (size_t i = 0; i < seen.size(); ++i) {
        missing += seen[i] ? 0 : 1;
    }
    if (missing > 0) {
        r.warnings << QStringLiteral("%1 boxes of the form are on pages the document does not have").arg(missing);
    }
    if (languages.isEmpty()) {
        languages << (manifest.language.isEmpty() ? QStringLiteral("en") : manifest.language);
    }
    QJsonObject dataset{{QStringLiteral("name"), safeName(writer) + u'-' + safeName(manifest.form)},
                        {QStringLiteral("version"), 1},
                        {QStringLiteral("languages"), QJsonArray::fromStringList(languages)},
                        {QStringLiteral("licence"), job.licence},
                        {QStringLiteral("source"), QFileInfo(job.document).fileName()},
                        {QStringLiteral("kind"), QStringLiteral("ink")}};
    if (job.noncommercial) {
        dataset.insert(QStringLiteral("noncommercial"), true);
    }
    if (!writeDatasetFile(out.filePath(QStringLiteral("lines.jsonl")), jsonl) ||
        !writeDatasetFile(out.filePath(QStringLiteral("dataset.json")), QJsonDocument(dataset).toJson())) {
        r.error = QStringLiteral("Cannot write into %1").arg(job.out);
        return r;
    }
    r.ok = true;
    return r;
}

}  // namespace xqt::hwr
