#include "HwrInfo.h"

#include <cmath>

#include <QElapsedTimer>
#include <QStringList>

#include "CtcRecognizer.h"
#include "HandwritingSearch.h"
#include "ModelInfo.h"
#include "OrtRuntime.h"
#include "TrocrRecognizer.h"

namespace xqt::hwr {

std::shared_ptr<Recognizer> onnxRecognizerFor(const QString& dir) {
    if (ModelInfo::read(dir).kind == QLatin1String("ctc")) {
        return std::make_shared<CtcRecognizer>(dir);
    }
    return std::make_shared<TrocrRecognizer>(dir);
}

namespace {
/// A stroke of points.
InkStroke stroke(std::vector<QPointF> points) { return InkStroke::of(std::move(points), 1.2f); }

/// A circle (an o, the bowl of an a) around (cx, cy), drawn anticlockwise from its right top as a hand does.
InkStroke bowl(double cx, double cy, double rx, double ry) {
    std::vector<QPointF> points;
    for (int i = 0; i <= 24; ++i) {
        const double a = -M_PI / 4 - 2 * M_PI * i / 24.0;
        points.emplace_back(cx + rx * std::cos(a), cy + ry * std::sin(a));
    }
    return stroke(std::move(points));
}
}  // namespace

LineInput sampleLine() {
    // Baseline at y 20, small letters 10 high, tall ones 20 (y down)
    LineInput line;
    line.strokes = {
            stroke({{0, 0}, {0, 20}}),           // H
            stroke({{11, 0}, {11, 20}}),         //
            stroke({{0, 10.5}, {11, 10.5}}),     //
            bowl(20, 15, 4.5, 5),                // a
            stroke({{24.5, 10}, {24.5, 20}}),    //
            stroke({{30, 0}, {30, 20}}),         // l
            stroke({{35, 0}, {35, 20}}),         // l
            bowl(44, 15, 4.5, 5),                // o
    };
    QRectF box;
    std::vector<uint32_t> all;
    for (uint32_t i = 0; i < line.strokes.size(); ++i) {
        box = box.united(line.strokes[i].box);
        all.push_back(i);
    }
    line.words.push_back({box, all});
    line.size = box.size();
    line.h = 10;
    line.u = 1.2;
    line.hash = 0x58515448616c6c6full;  // (any: no cache sees it)
    return line;
}

HwrReport describe(const QString& bundledDir) {
    HwrReport r;
    QStringList out;
    QString why;
    const bool runtime = ort::api(&why) != nullptr;
    if (runtime) {
        out << QStringLiteral("onnxruntime: found (%1, version %2)").arg(ort::loadedPath(), ort::version());
    } else {
        out << QStringLiteral("onnxruntime: not found (%1)").arg(why);
    }
    const std::vector<ModelInfo> models = HandwritingSearch::bundledModels(bundledDir);
    out << (models.empty() ? QStringLiteral("models: none in %1").arg(bundledDir)
                           : QStringLiteral("models: %1").arg(bundledDir));
    bool allRead = !models.empty();
    for (const ModelInfo& m: models) {
        out << QStringLiteral("model: %1 (%2; %3; version %4%5) in %6")
                       .arg(m.name, m.kind, m.languages.join(QStringLiteral(", ")), m.version,
                            m.noncommercial ? QStringLiteral("; non-commercial") : QString(), m.folder);
        const std::shared_ptr<Recognizer> rec = onnxRecognizerFor(m.folder);
        QString notReady;
        if (!rec->ready(&notReady)) {
            out << QStringLiteral("  cannot read the sample line (%1)").arg(notReady);
            allRead = false;
            continue;
        }
        Context context;
        context.language = m.languages.value(0, QStringLiteral("en"));
        QElapsedTimer timer;
        timer.start();
        const auto result = rec->recognizeLine(sampleLine(), context);
        if (!result) {
            // (the files' sha256 are checked when the model is loaded: that is said again here)
            rec->ready(&notReady);
            out << QStringLiteral("  cannot read the sample line (%1)")
                           .arg(notReady.isEmpty() ? QStringLiteral("the model failed to read it") : notReady);
            allRead = false;
            continue;
        }
        QStringList words;
        for (const ink::Word& w: result->words) {
            words << w.text;
        }
        out << QStringLiteral("  reads the sample line: \"%1\" (%2 ms)").arg(words.join(u' ')).arg(timer.elapsed());
    }
    r.ok = runtime && allRead;
    out << (r.ok ? QStringLiteral("hwr: ready") : QStringLiteral("hwr: not ready"));
    r.text = (out.join(u'\n') + u'\n').toStdString();
    return r;
}

}  // namespace xqt::hwr
