#include "CtcRecognizer.h"

#include <algorithm>
#include <cmath>

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

#include "LineImage.h"
#include "ModelInfo.h"

namespace xqt::hwr {

CtcManifest CtcManifest::read(const QString& dir) {
    CtcManifest m;
    const ModelInfo info = ModelInfo::read(dir);
    if (!info.valid()) {
        m.error = info.error;
        return m;
    }
    if (info.kind != QLatin1String("ctc")) {
        m.error = QStringLiteral("The model in %1 is not a CTC model").arg(dir);
        return m;
    }
    QFile f(QDir(dir).filePath(QStringLiteral("model.json")));
    if (!f.open(QIODevice::ReadOnly)) {
        m.error = QStringLiteral("No model in %1").arg(dir);
        return m;
    }
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    m.name = info.name;
    m.languages = info.languages;
    m.model = o.value(QStringLiteral("model")).toString();
    m.alphabet = o.value(QStringLiteral("alphabet")).toString();
    m.blank = o.value(QStringLiteral("blank")).toInt(0);
    m.inputHeight = o.value(QStringLiteral("input_height")).toInt(64);
    m.maxWidth = o.value(QStringLiteral("max_width")).toInt(2048);
    const QJsonObject files = o.value(QStringLiteral("files")).toObject();
    for (auto it = files.begin(); it != files.end(); ++it) {
        const QJsonObject e = it.value().toObject();
        m.files[it.key()] = {e.value(QStringLiteral("sha256")).toString(), e.value(QStringLiteral("size")).toInteger(-1)};
    }
    if (m.model.isEmpty() || m.alphabet.isEmpty() || m.inputHeight < 8 || m.maxWidth < m.inputHeight || m.blank < 0) {
        m.error = QStringLiteral("The model's manifest in %1 is incomplete").arg(dir);
        return m;
    }
    m.id = info.id() + u'/' + QLatin1String(CtcRecognizer::VERSION);
    return m;
}

CtcRecognizer::CtcRecognizer(QString modelDir): dir(std::move(modelDir)), manifest(CtcManifest::read(dir)) {}

CtcRecognizer::~CtcRecognizer() = default;

Capabilities CtcRecognizer::capabilities() const {
    Capabilities c;
    c.strokes = false;
    c.languages = manifest.languages;
    c.topK = TOP_K;
    c.id = manifest.valid() ? manifest.id : QStringLiteral("ctc/none");
    return c;
}

bool CtcRecognizer::ready(QString* why) const {
    std::lock_guard lock(mtx);
    auto no = [&](const QString& reason) {
        if (why) {
            *why = reason;
        }
        return false;
    };
    if (damaged) {
        return no(damage);
    }
    if (!manifest.valid()) {
        return no(manifest.error);
    }
    for (const QString& file: {manifest.model, manifest.alphabet}) {
        const QFileInfo info(QDir(dir).filePath(file));
        auto it = manifest.files.find(file);
        if (!info.exists() || (it != manifest.files.end() && it->second.size >= 0 && info.size() != it->second.size)) {
            return no(QStringLiteral("The model in %1 is incomplete (%2)").arg(dir, file));
        }
    }
    QString runtime;
    if (!ort::api(&runtime)) {
        return no(runtime);
    }
    return true;
}

bool CtcRecognizer::loaded() const {
    std::lock_guard lock(mtx);
    return session != nullptr;
}

bool CtcRecognizer::load(QString* why) {
    std::lock_guard lock(mtx);
    if (session) {
        return true;
    }
    if (!checked) {
        checked = true;
        for (const auto& [name, file]: manifest.files) {
            if (file.sha256.isEmpty()) {
                continue;
            }
            QFile f(QDir(dir).filePath(name));
            QCryptographicHash hash(QCryptographicHash::Sha256);
            if (!f.open(QIODevice::ReadOnly) || !hash.addData(&f) ||
                QString::fromLatin1(hash.result().toHex()) != file.sha256.toLower()) {
                damaged = true;
                damage = QStringLiteral("The model file %1 is not the one its manifest names").arg(name);
                break;
            }
        }
    }
    if (damaged) {
        if (why) {
            *why = damage;
        }
        return false;
    }
    if (!alphabet.load(QDir(dir).filePath(manifest.alphabet), why)) {
        return false;
    }
    session = ort::Session::open(QDir(dir).filePath(manifest.model), THREADS, why);
    if (!session || session->inputs.empty() || session->outputs.empty()) {
        session.reset();
        return false;
    }
    runOptions = std::make_unique<ort::RunOptions>();
    return true;
}

void CtcRecognizer::unload() {
    std::lock_guard lock(mtx);
    session.reset();
    runOptions.reset();
}

void CtcRecognizer::interrupt() {
    std::lock_guard lock(mtx);
    if (runOptions) {
        runOptions->terminate();
    }
}

std::optional<std::vector<Beam>> CtcRecognizer::readPicture(const std::vector<float>& ink, int width,
                                                           const Context& context) {
    if ((context.cancelled && context.cancelled()) || !session) {
        return std::nullopt;
    }
    const int64_t h = manifest.inputHeight;
    const ort::Tensor picture = ort::Tensor::floats({1, 1, h, width}, ink);
    std::vector<ort::Tensor> out;
    QString why;
    if (!session->run({{session->inputs.at(0), &picture}}, {session->outputs.at(0)}, out,
                      runOptions ? runOptions->get() : nullptr, &why) ||
        out.empty() || out[0].shape.size() != 3) {
        return std::nullopt;
    }
    // [T, 1, C] (also [1, T, C] from exporters that put the batch first)
    const ort::Tensor& l = out[0];
    const bool batchFirst = l.shape[0] == 1 && l.shape[1] != 1;
    const auto frames = static_cast<size_t>(batchFirst ? l.shape[1] : l.shape[0]);
    const auto classes = static_cast<size_t>(l.shape[2]);
    if (classes != alphabet.size() + 1) {
        return std::nullopt;  // (a model with another alphabet)
    }
    const std::vector<CtcReading> readings = ctcBeamSearch(l.f, frames, classes, manifest.blank, BEAMS, TOP_K);
    // (the confidence of the words: per character, as TrOCR's per token; not the whole path's, which falls with the
    // number of frames)
    const double sure = ctcConfidence(l.f, frames, classes, manifest.blank);
    std::vector<Beam> beams;
    for (const CtcReading& r: readings) {
        const QString text = alphabet.text(r.labels, manifest.blank).simplified();
        if (!text.isEmpty()) {
            beams.push_back({text, r.logProb, sure, wordSpansOf(r, frames, alphabet, manifest.blank)});
        }
    }
    return beams;
}

std::vector<WordSpan> wordSpansOf(const CtcReading& reading, size_t frames, const CtcAlphabet& alphabet, int blank) {
    // The words as simplified() splits the text: at characters that are spaces
    std::vector<WordSpan> out;
    if (reading.spans.size() != reading.labels.size() || frames == 0) {
        return out;
    }
    const auto T = static_cast<double>(frames);
    bool inWord = false;
    for (size_t k = 0; k < reading.labels.size(); ++k) {
        const QString c = alphabet.charOf(reading.labels[k], blank);
        if (c.isEmpty()) {
            continue;
        }
        if (c.trimmed().isEmpty()) {
            inWord = false;
            continue;
        }
        const double left = reading.spans[k].first / T, right = (reading.spans[k].last + 1) / T;
        if (!inWord) {
            out.push_back({left, right});
            inWord = true;
        } else {
            out.back().right = std::max(out.back().right, right);
        }
    }
    return out;
}

std::optional<ink::LineResult> CtcRecognizer::recognizeLine(const LineInput& line, const Context& context) {
    QString why;
    if (!load(&why)) {
        return std::nullopt;
    }
    {
        std::lock_guard lock(mtx);
        runOptions->reset();  // (an interrupt of the line before)
    }
    // The whole line, else as few pieces as fit into the model's width
    std::vector<LinePiece> pieces;
    const size_t n = line.words.size();
    for (size_t count = 1; count <= std::max<size_t>(1, n); ++count) {
        pieces = piecesOf(line, static_cast<int>((n + count - 1) / count));
        if (std::all_of(pieces.begin(), pieces.end(),
                        [&](const LinePiece& p) { return widthAt(p, manifest.inputHeight) <= manifest.maxWidth; })) {
            break;
        }
    }
    ink::LineResult result;
    for (const LinePiece& piece: pieces) {
        if (context.cancelled && context.cancelled()) {
            return std::nullopt;
        }
        int width = 0;
        const std::vector<float> ink = inkOf(line, piece, manifest.inputHeight, manifest.maxWidth, width);
        auto beams = readPicture(ink, width, context);
        if (!beams) {
            return std::nullopt;
        }
        std::vector<QRectF> boxes;
        for (size_t i = piece.first; i <= piece.last; ++i) {
            boxes.push_back(line.words[i].box);
        }
        for (Beam& b: *beams) {
            for (WordSpan& s: b.spans) {
                s = {xAt(piece, s.left), xAt(piece, s.right)};
            }
        }
        for (ink::Word& w: wordsOf(*beams, boxes, TOP_K)) {
            result.words.push_back(std::move(w));
        }
    }
    return result;
}

}  // namespace xqt::hwr
