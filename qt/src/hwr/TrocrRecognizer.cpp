#include "TrocrRecognizer.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

#include "LineImage.h"
#include "WordAlignment.h"

namespace xqt::hwr {

Manifest Manifest::read(const QString& dir) {
    Manifest m;
    QFile f(QDir(dir).filePath(QStringLiteral("model.json")));
    if (!f.open(QIODevice::ReadOnly)) {
        m.error = QStringLiteral("No model in %1").arg(dir);
        return m;
    }
    const QByteArray bytes = f.readAll();
    const QJsonObject o = QJsonDocument::fromJson(bytes).object();
    m.name = o.value(QStringLiteral("name")).toString();
    m.encoder = o.value(QStringLiteral("encoder")).toString();
    m.decoder = o.value(QStringLiteral("decoder")).toString();
    m.tokenizer = o.value(QStringLiteral("tokenizer")).toString();
    m.start = o.value(QStringLiteral("decoder_start_token_id")).toInteger(2);
    m.end = o.value(QStringLiteral("eos_token_id")).toInteger(2);
    m.imageSize = o.value(QStringLiteral("image_size")).toInt(384);
    const QJsonObject files = o.value(QStringLiteral("files")).toObject();
    for (auto it = files.begin(); it != files.end(); ++it) {
        const QJsonObject e = it.value().toObject();
        m.files[it.key()] = {e.value(QStringLiteral("sha256")).toString(), e.value(QStringLiteral("size")).toInteger(-1)};
    }
    if (m.name.isEmpty() || m.encoder.isEmpty() || m.decoder.isEmpty() || m.tokenizer.isEmpty()) {
        m.error = QStringLiteral("The model's manifest in %1 is incomplete").arg(dir);
        return m;
    }
    const QString hash = QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
    m.id = m.name + u'/' + hash.left(12) + u'/' + QLatin1String(TrocrRecognizer::LAYOUT_VERSION);
    return m;
}

TrocrRecognizer::TrocrRecognizer(QString modelDir): dir(std::move(modelDir)), manifest(Manifest::read(dir)) {}

TrocrRecognizer::~TrocrRecognizer() = default;

Capabilities TrocrRecognizer::capabilities() const {
    Capabilities c;
    c.strokes = false;
    c.languages = {QStringLiteral("en")};
    c.topK = 5;
    c.id = manifest.valid() ? manifest.id : QStringLiteral("trocr/none");
    return c;
}

bool TrocrRecognizer::ready(QString* why) const {
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
    for (const QString& file: {manifest.encoder, manifest.decoder, manifest.tokenizer}) {
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

bool TrocrRecognizer::loaded() const {
    std::lock_guard lock(mtx);
    return encoder && decoder;
}

bool TrocrRecognizer::load(QString* why) {
    std::lock_guard lock(mtx);
    if (encoder && decoder) {
        return true;
    }
    if (!checked) {
        // The files as the manifest says (once: a damaged or changed model reads nothing)
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
    if (!tokenizer.load(QDir(dir).filePath(manifest.tokenizer), why)) {
        return false;
    }
    encoder = ort::Session::open(QDir(dir).filePath(manifest.encoder), THREADS, why);
    decoder = encoder ? ort::Session::open(QDir(dir).filePath(manifest.decoder), THREADS, why) : nullptr;
    if (!encoder || !decoder) {
        encoder.reset();
        decoder.reset();
        return false;
    }
    runOptions = std::make_unique<ort::RunOptions>();
    return true;
}

void TrocrRecognizer::unload() {
    std::lock_guard lock(mtx);
    encoder.reset();
    decoder.reset();
    runOptions.reset();
}

void TrocrRecognizer::interrupt() {
    std::lock_guard lock(mtx);
    if (runOptions) {
        runOptions->terminate();
    }
}

std::optional<std::vector<Beam>> TrocrRecognizer::readPicture(const std::vector<float>& pixels, const Context& context) {
    auto cancelled = [&] { return context.cancelled && context.cancelled(); };
    const int64_t size = manifest.imageSize;
    const ort::Tensor picture = ort::Tensor::floats({1, 3, size, size}, pixels);
    std::vector<ort::Tensor> out;
    QString why;
    OrtRunOptions* options = runOptions ? runOptions->get() : nullptr;
    if (cancelled() || !encoder->run({{encoder->inputs.at(0), &picture}}, {encoder->outputs.at(0)}, out, options, &why) ||
        out.empty() || out[0].shape.size() != 3) {
        return std::nullopt;
    }
    // The encoder's states once per beam
    const int64_t k = BEAMS;
    ort::Tensor states = ort::Tensor::floats({k, out[0].shape[1], out[0].shape[2]});
    for (int64_t b = 0; b < k; ++b) {
        std::copy(out[0].f.begin(), out[0].f.end(), states.f.begin() + static_cast<std::ptrdiff_t>(b * out[0].f.size()));
    }
    // The decoder's inputs and outputs
    std::vector<std::string> pastNames;
    std::vector<std::vector<int64_t>> pastShapes;
    bool hasCacheBranch = false;
    for (size_t i = 0; i < decoder->inputs.size(); ++i) {
        const std::string& n = decoder->inputs[i];
        if (n.rfind("past_key_values", 0) == 0) {
            pastNames.push_back(n);
            pastShapes.push_back(decoder->inputShapes[i]);
        } else if (n == "use_cache_branch") {
            hasCacheBranch = true;
        }
    }
    std::vector<std::string> outNames = decoder->outputs;  // logits first (as exported), then present.*
    std::map<std::string, ort::Tensor> past;
    bool first = true;
    auto step = [&](const std::vector<int64_t>& tokens, const std::vector<size_t>& parents,
                    std::vector<std::vector<float>>& logits) -> bool {
        if (cancelled()) {
            return false;
        }
        const auto rows = static_cast<int64_t>(tokens.size());
        // The beams' caches follow their beams
        if (!parents.empty()) {
            for (auto& [name, t]: past) {
                if (name.find(".encoder.") != std::string::npos || t.shape.empty() || t.shape[0] != rows) {
                    continue;  // (the encoder's: the same for every beam)
                }
                const size_t row = t.f.size() / static_cast<size_t>(rows);
                std::vector<float> moved(t.f.size());
                for (size_t r = 0; r < parents.size() && r < static_cast<size_t>(rows); ++r) {
                    std::copy(t.f.begin() + static_cast<std::ptrdiff_t>(parents[r] * row),
                              t.f.begin() + static_cast<std::ptrdiff_t>((parents[r] + 1) * row),
                              moved.begin() + static_cast<std::ptrdiff_t>(r * row));
                }
                t.f = std::move(moved);
            }
        }
        const ort::Tensor ids = ort::Tensor::ints({rows, 1}, tokens);
        const ort::Tensor branch = ort::Tensor::bools({1}, {static_cast<uint8_t>(first ? 0 : 1)});
        std::vector<ort::Tensor> empties;
        empties.reserve(pastNames.size());
        std::vector<std::pair<std::string, const ort::Tensor*>> in{{"input_ids", &ids},
                                                                    {"encoder_hidden_states", &states}};
        if (hasCacheBranch) {
            in.emplace_back("use_cache_branch", &branch);
        }
        for (size_t i = 0; i < pastNames.size(); ++i) {
            auto it = past.find(pastNames[i]);
            if (!first && it != past.end()) {
                in.emplace_back(pastNames[i], &it->second);
                continue;
            }
            const auto& shape = pastShapes[i];
            const int64_t heads = shape.size() == 4 && shape[1] > 0 ? shape[1] : 8;
            const int64_t dim = shape.size() == 4 && shape[3] > 0 ? shape[3] : 32;
            empties.push_back(ort::Tensor::floats({rows, heads, 0, dim}));
            in.emplace_back(pastNames[i], &empties.back());
        }
        std::vector<ort::Tensor> results;
        QString error;
        if (!decoder->run(in, outNames, results, runOptions ? runOptions->get() : nullptr, &error) ||
            results.size() != outNames.size() || results[0].shape.size() != 3) {
            return false;
        }
        const ort::Tensor& l = results[0];
        const auto length = static_cast<size_t>(l.shape[1]), vocab = static_cast<size_t>(l.shape[2]);
        logits.assign(static_cast<size_t>(rows), {});
        for (size_t r = 0; r < static_cast<size_t>(rows); ++r) {
            const auto from = l.f.begin() + static_cast<std::ptrdiff_t>((r * length + length - 1) * vocab);
            logits[r].assign(from, from + static_cast<std::ptrdiff_t>(vocab));
        }
        for (size_t i = 1; i < outNames.size(); ++i) {
            std::string name = outNames[i];
            if (name.rfind("present", 0) == 0) {
                name = "past_key_values" + name.substr(7);
            }
            if (!first && name.find(".encoder.") != std::string::npos) {
                continue;  // (the merged decoder gives the encoder's cache only at the first step)
            }
            past[name] = std::move(results[i]);
        }
        first = false;
        return true;
    };
    const std::vector<BeamResult> results = beamSearch(step, manifest.start, manifest.end, BEAMS, MAX_TOKENS);
    if (results.empty()) {
        return std::nullopt;
    }
    std::vector<Beam> beams;
    for (const BeamResult& r: results) {
        std::vector<int64_t> tokens = r.tokens;
        if (!tokens.empty() && tokens.back() == manifest.end) {
            tokens.pop_back();
        }
        beams.push_back({tokenizer.decode(tokens), r.logProb, r.meanLogProb});
    }
    return beams;
}

std::optional<ink::LineResult> TrocrRecognizer::recognizeLine(const LineInput& line, const Context& context) {
    QString why;
    if (!load(&why)) {
        return std::nullopt;
    }
    {
        std::lock_guard lock(mtx);
        runOptions->reset();  // (an interrupt of the line before)
    }
    ink::LineResult result;
    for (const LinePiece& piece: piecesOf(line)) {
        if (context.cancelled && context.cancelled()) {
            return std::nullopt;
        }
        const auto beams = readPicture(pixelsOf(line, piece, manifest.imageSize), context);
        if (!beams) {
            return std::nullopt;
        }
        std::vector<QRectF> boxes;
        for (size_t i = piece.first; i <= piece.last; ++i) {
            boxes.push_back(line.words[i].box);
        }
        for (ink::Word& w: wordsOf(*beams, boxes)) {
            result.words.push_back(std::move(w));
        }
    }
    return result;
}

}  // namespace xqt::hwr
