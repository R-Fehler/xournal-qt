#include "BeamSearch.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace xqt::hwr {

std::vector<BeamResult> beamSearch(const StepFn& step, int64_t start, int64_t end, int k, int maxLength) {
    k = std::max(1, k);
    struct Beam {
        std::vector<int64_t> seq;  ///< with the start token
        double score = 0;
    };
    std::vector<Beam> beams{{{start}, 0.0}};
    std::vector<Beam> done;
    std::vector<int64_t> last(static_cast<size_t>(k), start);
    std::vector<size_t> parents;
    std::vector<std::vector<float>> logits;
    for (int s = 0; s < maxLength; ++s) {
        if (!step(last, parents, logits) || logits.size() < beams.size()) {
            return {};
        }
        struct Cand {
            double score;
            size_t beam;
            int64_t token;
        };
        std::vector<Cand> cands;
        for (size_t b = 0; b < beams.size(); ++b) {
            const std::vector<float>& row = logits[b];
            if (row.empty()) {
                return {};
            }
            // log-softmax
            const float top = *std::max_element(row.begin(), row.end());
            double sum = 0;
            for (const float v: row) {
                sum += std::exp(static_cast<double>(v - top));
            }
            const double norm = static_cast<double>(top) + std::log(sum);
            std::vector<size_t> idx(row.size());
            for (size_t i = 0; i < idx.size(); ++i) {
                idx[i] = i;
            }
            const size_t take = std::min<size_t>(static_cast<size_t>(k), idx.size());
            std::partial_sort(idx.begin(), idx.begin() + static_cast<std::ptrdiff_t>(take), idx.end(),
                              [&](size_t a, size_t c) { return row[a] > row[c]; });
            for (size_t i = 0; i < take; ++i) {
                cands.push_back({beams[b].score + (static_cast<double>(row[idx[i]]) - norm), b,
                                 static_cast<int64_t>(idx[i])});
            }
        }
        std::stable_sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) { return a.score > b.score; });
        std::vector<Beam> next;
        std::vector<size_t> rows;
        for (const Cand& c: cands) {
            Beam nb{beams[c.beam].seq, c.score};
            nb.seq.push_back(c.token);
            if (c.token == end) {
                done.push_back(std::move(nb));
            } else {
                next.push_back(std::move(nb));
                rows.push_back(c.beam);
            }
            if (next.size() == static_cast<size_t>(k)) {
                break;
            }
        }
        if (done.size() >= static_cast<size_t>(k) || next.empty()) {
            beams.clear();
            break;
        }
        const size_t live = next.size();
        while (next.size() < static_cast<size_t>(k)) {  // (the batch keeps its size)
            next.push_back(next.back());
            rows.push_back(rows.back());
        }
        beams = std::move(next);
        beams.resize(live);
        parents = rows;
        for (size_t r = 0; r < rows.size(); ++r) {
            last[r] = (r < live ? beams[r] : beams[live - 1]).seq.back();
        }
    }
    for (Beam& b: beams) {
        done.push_back(std::move(b));
    }
    std::stable_sort(done.begin(), done.end(), [](const Beam& a, const Beam& b) {
        return a.score / static_cast<double>(a.seq.size()) > b.score / static_cast<double>(b.seq.size());
    });
    std::vector<BeamResult> out;
    std::set<std::vector<int64_t>> seen;
    for (const Beam& b: done) {
        std::vector<int64_t> tokens(b.seq.begin() + 1, b.seq.end());
        std::vector<int64_t> key = tokens;
        if (!key.empty() && key.back() == end) {
            key.pop_back();
        }
        if (!seen.insert(key).second) {
            continue;
        }
        const double n = std::max<double>(1.0, static_cast<double>(tokens.size()));
        out.push_back({std::move(tokens), b.score, b.score / n});
        if (out.size() == static_cast<size_t>(k)) {
            break;
        }
    }
    return out;
}

// --- tokens to text ---------------------------------------------------------------------------------------------

bool Tokenizer::load(const QString& file, QString* error) {
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = QStringLiteral("cannot read %1").arg(file);
        }
        return false;
    }
    return parse(f.readAll(), error);
}

bool Tokenizer::parse(const QByteArray& json, QString* error) {
    pieces.clear();
    special.clear();
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &pe);
    const QJsonObject root = doc.object();
    const QJsonObject model = root.value(QStringLiteral("model")).toObject();
    if (pe.error != QJsonParseError::NoError || model.isEmpty()) {
        if (error) {
            *error = QStringLiteral("not a tokenizer.json");
        }
        return false;
    }
    auto set = [&](qint64 id, const QByteArray& piece) {
        if (id < 0 || id > 10'000'000) {
            return;
        }
        const auto i = static_cast<size_t>(id);
        if (pieces.size() <= i) {
            pieces.resize(i + 1);
            special.resize(i + 1, 0);
        }
        pieces[i] = piece;
    };
    const QJsonValue vocab = model.value(QStringLiteral("vocab"));
    if (vocab.isArray()) {  // Unigram (SentencePiece): [[piece, score], ...] by id
        const QJsonArray list = vocab.toArray();
        for (qsizetype i = 0; i < list.size(); ++i) {
            set(i, list.at(i).toArray().at(0).toString().toUtf8());
        }
    } else if (vocab.isObject()) {  // BPE / WordPiece: {piece: id}
        const QJsonObject map = vocab.toObject();
        for (auto it = map.begin(); it != map.end(); ++it) {
            set(it.value().toInteger(-1), it.key().toUtf8());
        }
    }
    for (const QJsonValue& v: root.value(QStringLiteral("added_tokens")).toArray()) {
        const QJsonObject t = v.toObject();
        const qint64 id = t.value(QStringLiteral("id")).toInteger(-1);
        set(id, t.value(QStringLiteral("content")).toString().toUtf8());
        if (id >= 0 && static_cast<size_t>(id) < special.size()) {
            special[static_cast<size_t>(id)] = t.value(QStringLiteral("special")).toBool() ? 1 : 0;
        }
    }
    const QJsonObject decoder = root.value(QStringLiteral("decoder")).toObject();
    const QString type = decoder.value(QStringLiteral("type")).toString();
    byteLevel = type == QLatin1String("ByteLevel") ||
                (type == QLatin1String("Sequence") &&
                 QJsonDocument(decoder).toJson().contains("ByteLevel"));
    if (pieces.empty()) {
        if (error) {
            *error = QStringLiteral("the tokenizer has no vocabulary");
        }
        return false;
    }
    return true;
}

namespace {
/// GPT-2's map from the characters of byte-level pieces back to bytes
const std::map<char32_t, unsigned char>& byteDecoder() {
    static const std::map<char32_t, unsigned char> table = [] {
        std::map<char32_t, unsigned char> t;
        std::vector<int> bs;
        for (int b = '!'; b <= '~'; ++b) {
            bs.push_back(b);
        }
        for (int b = 0xA1; b <= 0xAC; ++b) {
            bs.push_back(b);
        }
        for (int b = 0xAE; b <= 0xFF; ++b) {
            bs.push_back(b);
        }
        std::vector<int> cs = bs;
        int n = 0;
        for (int b = 0; b < 256; ++b) {
            if (std::find(bs.begin(), bs.end(), b) == bs.end()) {
                bs.push_back(b);
                cs.push_back(256 + n++);
            }
        }
        for (size_t i = 0; i < bs.size(); ++i) {
            t[static_cast<char32_t>(cs[i])] = static_cast<unsigned char>(bs[i]);
        }
        return t;
    }();
    return table;
}
}  // namespace

QString Tokenizer::decode(const std::vector<int64_t>& ids) const {
    QByteArray utf8;
    for (const int64_t id: ids) {
        if (id < 0 || static_cast<size_t>(id) >= pieces.size() || special[static_cast<size_t>(id)]) {
            continue;
        }
        utf8 += pieces[static_cast<size_t>(id)];
    }
    QString text;
    if (byteLevel) {
        QByteArray bytes;
        for (const char32_t c: QString::fromUtf8(utf8).toUcs4()) {
            const auto& map = byteDecoder();
            auto it = map.find(c);
            if (it != map.end()) {
                bytes += static_cast<char>(it->second);
            }
        }
        text = QString::fromUtf8(bytes);
    } else {
        text = QString::fromUtf8(utf8).replace(QChar(0x2581), QLatin1Char(' '));  // ▁
    }
    return text.simplified();
}

}  // namespace xqt::hwr
