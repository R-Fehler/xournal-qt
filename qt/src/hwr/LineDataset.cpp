#include "LineDataset.h"

#include <cmath>
#include <limits>
#include <shared_mutex>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include "model/Document.h"
#include "model/XojPage.h"
#include "session/DocumentSession.h"

#include "InkLayout.h"
#include "LineImage.h"
#include "Recognizer.h"

namespace xqt::hwr {

QStringList transcriptsOf(const QByteArray& text) {
    QStringList out;
    for (QString line: QString::fromUtf8(text).split(u'\n')) {
        line.remove(u'\r');
        const QString t = line.trimmed();
        if (t.isEmpty() || t.startsWith(u'#')) {
            continue;
        }
        out << t.normalized(QString::NormalizationForm_C);
    }
    return out;
}

bool writeDatasetFile(const QString& path, const QByteArray& data) {
    QSaveFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(data) == data.size() && f.commit();
}

namespace {
double round2(double v) { return std::round(v * 100.0) / 100.0; }
}  // namespace

QJsonObject strokesJson(const LineInput& line) {
    QJsonArray strokes;
    for (const InkStroke& s: line.strokes) {
        QJsonArray points;
        for (size_t i = 0; i < s.points.size(); ++i) {
            const double pressure =
                    i < s.widths.size() && s.width > 0 ? static_cast<double>(s.widths[i]) / s.width : 1.0;
            points.append(QJsonArray{round2(s.points[i].x()), round2(s.points[i].y()), round2(pressure)});
        }
        strokes.append(QJsonObject{{QStringLiteral("points"), points}, {QStringLiteral("width"), round2(s.width)}});
    }
    return QJsonObject{{QStringLiteral("width"), round2(line.size.width())},
                       {QStringLiteral("height"), round2(line.size.height())},
                       {QStringLiteral("strokes"), strokes}};
}

QString safeName(const QString& s) {
    QString out;
    for (const QChar c: s) {
        out += c.isLetterOrNumber() && c.unicode() < 128 ? c : u'_';
    }
    return out.isEmpty() ? QStringLiteral("doc") : out;
}

QImage lineImage(const LineInput& line) {
    // The whole line as one piece, as the recognisers draw it
    const std::vector<LinePiece> pieces = piecesOf(line, std::numeric_limits<int>::max());
    if (pieces.empty()) {
        return {};
    }
    int w = 0, h = 0;
    const std::vector<unsigned char> grey = greyOf(line, pieces.front(), w, h);
    QImage image(w, h, QImage::Format_Grayscale8);
    for (int y = 0; y < h; ++y) {
        std::copy(grey.begin() + static_cast<std::ptrdiff_t>(y * w),
                  grey.begin() + static_cast<std::ptrdiff_t>((y + 1) * w), image.scanLine(y));
    }
    return image;
}

LineExportResult exportLines(const LineExport& job) {
    auto loaded = DocumentSession::loadFile(fs::path(job.document.toStdString()));
    if (!loaded.document) {
        LineExportResult r;
        r.error = QString::fromStdString(loaded.error.empty() ? "Cannot open " + job.document.toStdString()
                                                              : loaded.error);
        return r;
    }
    return exportLines(job, *loaded.document);
}

LineExportResult exportLines(const LineExport& job, Document& doc) {
    LineExportResult r;
    QStringList texts;
    if (!job.texts.isEmpty()) {
        QFile f(job.texts);
        if (!f.open(QIODevice::ReadOnly)) {
            r.error = QStringLiteral("Cannot read %1").arg(job.texts);
            return r;
        }
        texts = transcriptsOf(f.readAll());
        r.transcripts = static_cast<int>(texts.size());
    }
    const QDir out(job.out);
    if (!QDir().mkpath(out.filePath(QStringLiteral("images"))) || !QDir().mkpath(out.filePath(QStringLiteral("strokes")))) {
        r.error = QStringLiteral("Cannot create %1").arg(job.out);
        return r;
    }
    const QString base = safeName(QFileInfo(job.document).completeBaseName());
    const QString writer = job.writer.isEmpty() ? QStringLiteral("me") : job.writer;
    QByteArray jsonl;
    std::shared_lock lock(doc);
    for (size_t p = 0; p < doc.getPageCount(); ++p) {
        const std::vector<InkStroke> strokes = strokesOf(*doc.getPage(p));
        if (strokes.empty()) {
            continue;
        }
        const Layout layout = hwr::layout(strokes);
        for (size_t l = 0; l < layout.lines.size(); ++l) {
            const LineInput line = LineInput::of(strokes, layout, layout.lines[l]);
            if (line.words.empty()) {
                continue;
            }
            const QString id = QStringLiteral("%1-%2-p%3-l%4")
                                       .arg(safeName(writer), base)
                                       .arg(p + 1, 2, 10, QChar(u'0'))
                                       .arg(l + 1, 3, 10, QChar(u'0'));
            const QImage image = lineImage(line);
            const QString imagePath = QStringLiteral("images/") + id + QStringLiteral(".png");
            const QString strokesPath = QStringLiteral("strokes/") + id + QStringLiteral(".json");
            if (!image.save(out.filePath(imagePath), "PNG") ||
                !writeDatasetFile(out.filePath(strokesPath), QJsonDocument(strokesJson(line)).toJson(QJsonDocument::Compact))) {
                r.error = QStringLiteral("Cannot write into %1").arg(job.out);
                return r;
            }
            const QString text = r.lines < texts.size() ? texts[r.lines] : QString();
            const QJsonObject entry{{QStringLiteral("id"), id},
                                    {QStringLiteral("image"), imagePath},
                                    {QStringLiteral("text"), text},
                                    {QStringLiteral("lang"), job.language},
                                    {QStringLiteral("writer"), writer},
                                    {QStringLiteral("strokes"), strokesPath},
                                    {QStringLiteral("page"), static_cast<int>(p + 1)},
                                    {QStringLiteral("line"), static_cast<int>(l + 1)}};
            jsonl += QJsonDocument(entry).toJson(QJsonDocument::Compact) + '\n';
            ++r.lines;
        }
    }
    if (!texts.isEmpty() && texts.size() != r.lines) {
        r.warnings << QStringLiteral("%1 lines of ink, %2 lines of text: they are matched in order, so check that each "
                                     "sentence was written on one line (lines.jsonl has the page and line of each)")
                              .arg(r.lines)
                              .arg(texts.size());
    }
    QJsonObject dataset{{QStringLiteral("name"), safeName(writer) + u'-' + base},
                        {QStringLiteral("version"), 1},
                        {QStringLiteral("languages"), QJsonArray{job.language}},
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
