/*
 * xournal-qt: the handwriting of a page as lines and words, from the strokes' geometry and order alone (no model).
 *
 * The first step of the handwriting search (qt/docs/features/handwriting-search.md); a port of the trials in
 * qt/research/hwr/segment.py, with the stroke order added (the research found that geometry alone fails where lines
 * are written close together or over each other):
 *  1. units: the median height of the strokes (`h`; cursive splits words into strokes of about the x-height, dots
 *     and accents make it smaller) and the median of the heights above it (`u`, about a line's letters);
 *  2. drawings are left out: strokes taller than 2.5 u, long straight strokes (underlines, arrows, frames: longer than
 *     4 u and almost straight), filled strokes. Highlighter and eraser strokes never take part (strokesOf);
 *  3. lines from the order of writing: a stroke starts a new line when it jumps back left by more than 2 u and down by
 *     more than half a u from the one before (the pen went to the next line), or when it is more than 1.5 u above or
 *     below the line it would join (the middle of the last 8 strokes of it that are not small);
 *  4. pieces of lines that lie at the same height are merged (overlapping by half the smaller height): a word added
 *     later, i-dots and crosses written after the line, a line written in two goes. Lines are sorted top to bottom;
 *  5. words: the strokes of a line by their left edge; a new word starts at a gap above a threshold found in the
 *     line's gaps (two-class Otsu split), clamped to 0.6 to 2 h. Small strokes (dots, accents, umlaut marks) do not
 *     split and join the word nearest to them;
 *  6. a line's hash: of its strokes' points (0.1 pt) and widths relative to the line's origin (its top-left), so a
 *     line moved with the lasso keeps its hash and its recognised words (InkText.h: boxes relative to the origin).
 * Lines whose strokes are all small are left out.
 *
 * Handwriting at an angle (a note written upwards along the margin, a label along an arrow, a line written steeply
 * uphill) is laid out in its own frame (framesOf): before step 3 the strokes are taken in runs, consecutive strokes in
 * the order of writing that are close to each other (within 3 `s`, the median of the strokes' smaller side; dots and
 * accents aside). A run long and narrow enough (6 s, and 3 times as long as wide along its points' principal axis;
 * not two strokes alone, an i and its dot), whose strokes go one after the other along that axis (a single stroke:
 * from its start to its end) and do not lie across it (words one below the other, a list), has a direction. More
 * than 20 degrees off left-to-right (and not leftwards: beyond 110 degrees it is not taken for writing) it is turned
 * upright: 70 to 110 degrees count as 90 exactly (downwards, -90 upwards), 20 to 70 keep their angle. Runs of the same
 * angle (within 8 degrees) are one frame: their strokes, and the dots and short runs inside the box of one of them, are
 * turned by -angle around (0, 0) and laid out by steps 1-6 as a page of their own (its own units). Their lines keep
 * the angle (InkLine::angle) and their box in the frame (`upright`); the hash is of the turned strokes, relative to
 * that box. The other strokes are laid out as before, with their own units (not counting those at an angle, which
 * are as tall as they are long): a page without writing at an angle comes out exactly as it did.
 *
 * Measured on test/files/benchmark/handwritten-text.xopp (13,064 strokes): a few ms per page.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <cstdint>
#include <vector>

#include <QPointF>
#include <QRectF>

class Element;
class XojPage;

namespace xqt::hwr {

/// A pen stroke of a page, copied (the layout runs without the document's lock).
struct InkStroke {
    std::vector<QPointF> points;
    std::vector<float> widths;  ///< per point (pressure); empty: `width` everywhere
    float width = 1;
    QRectF box;  ///< of its points (without the width)
    bool filled = false;
    /// Made from points, with the box.
    static InkStroke of(std::vector<QPointF> points, float width, std::vector<float> widths = {});
};

/// The pen strokes of the visible layers of a page, in the order they were written (layer by layer). The caller holds
/// the document's lock (shared).
std::vector<InkStroke> strokesOf(const XojPage& page);
/// The pen strokes among these elements (a selection's), in their order.
std::vector<InkStroke> strokesOf(const std::vector<const Element*>& elements);

struct InkWordBox {
    QRectF box;                     ///< page points; a line at an angle: in its frame (InkLine::upright)
    std::vector<uint32_t> strokes;  ///< indices into the page's strokes
};

struct InkLine {
    QRectF box;                     ///< page points, of all its strokes
    std::vector<uint32_t> strokes;  ///< indices into the page's strokes, in the order they were written
    std::vector<InkWordBox> words;  ///< left to right (in its frame)
    quint64 hash = 0;               ///< of its strokes relative to its origin (in its frame)
    /// The direction it was written in: degrees clockwise on the page, 0 left to right (the common case: its frame is
    /// the page), 90 downwards, -90 upwards, others between 20 and 70 or -70 and -20
    double angle = 0;
    /// Its box in its frame, the page turned by -angle around (0, 0) (`box` for angle 0)
    QRectF upright;
    /// Where its origin (the top-left of its box upright) is on the page
    QPointF origin() const;
};

struct Layout {
    std::vector<InkLine> lines;  ///< top to bottom
    double h = 0;                ///< the units of the page (see above; of the strokes not at an angle)
    double u = 0;
    std::vector<uint32_t> drawings;  ///< strokes left out
};

/// Lines and words of a page's strokes.
Layout layout(const std::vector<InkStroke>& strokes);

/// The hash of these strokes relative to `origin`.
quint64 hashOf(const std::vector<InkStroke>& strokes, const std::vector<uint32_t>& which, QPointF origin);

/// The groups of strokes written at an angle (see above), each with its angle; the rest are laid out as on a page.
/// `h`, `u`: the page's units. Exposed for tests.
struct Frame {
    double angle = 0;
    std::vector<uint32_t> strokes;  ///< in the order of writing
};
std::vector<Frame> framesOf(const std::vector<InkStroke>& strokes, double h, double u);

/// A stroke turned by `degrees` around (0, 0) (ink::turned), with its box.
InkStroke turned(const InkStroke& s, double degrees);

/// The words of a line's strokes (step 5), with `h` the page's median stroke height. Exposed for tests.
std::vector<InkWordBox> wordsOf(const std::vector<InkStroke>& strokes, const std::vector<uint32_t>& line, double h);

}  // namespace xqt::hwr
