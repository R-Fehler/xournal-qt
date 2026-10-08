#include "Recognizer.h"

#include <map>

namespace xqt::hwr {

QStringList Capabilities::languagesOf(uint32_t bits) const {
    if (bits == 0 || modelLanguages.empty()) {
        return languages;
    }
    QStringList out;
    for (size_t i = 0; i < modelLanguages.size() && i < 32; ++i) {
        if (bits & (1u << i)) {
            for (const QString& l: modelLanguages[i]) {
                if (!out.contains(l)) {
                    out << l;
                }
            }
        }
    }
    return out;
}

LineInput LineInput::of(const std::vector<InkStroke>& page, const Layout& layout, const InkLine& line) {
    LineInput in;
    in.hash = line.hash;
    in.h = layout.h;
    in.u = layout.u;
    in.size = line.angle == 0 ? line.box.size() : line.upright.size();
    // (a line at an angle: turned upright, its origin in its frame)
    const QPointF origin = line.angle == 0 ? line.origin() : line.upright.topLeft();
    std::map<uint32_t, uint32_t> index;  ///< page stroke -> stroke of the line
    for (const uint32_t i: line.strokes) {
        InkStroke s = line.angle == 0 ? page[i] : turned(page[i], -line.angle);
        for (QPointF& p: s.points) {
            p -= origin;
        }
        s.box.translate(-origin);
        index[i] = static_cast<uint32_t>(in.strokes.size());
        in.strokes.push_back(std::move(s));
    }
    for (const InkWordBox& w: line.words) {
        InkWordBox word{w.box.translated(-origin), {}};
        for (const uint32_t i: w.strokes) {
            if (auto it = index.find(i); it != index.end()) {
                word.strokes.push_back(it->second);
            }
        }
        in.words.push_back(std::move(word));
    }
    return in;
}

}  // namespace xqt::hwr
