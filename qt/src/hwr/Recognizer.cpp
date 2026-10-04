#include "Recognizer.h"

#include <map>

namespace xqt::hwr {

LineInput LineInput::of(const std::vector<InkStroke>& page, const Layout& layout, const InkLine& line) {
    LineInput in;
    in.hash = line.hash;
    in.size = line.box.size();
    in.h = layout.h;
    in.u = layout.u;
    const QPointF origin = line.origin();
    std::map<uint32_t, uint32_t> index;  ///< page stroke -> stroke of the line
    for (const uint32_t i: line.strokes) {
        InkStroke s = page[i];
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
