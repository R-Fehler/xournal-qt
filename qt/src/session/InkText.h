/*
 * xournal-qt: the words recognised in handwriting, for the search (qt/docs/features/handwriting-search.md).
 *
 * Handwriting is never turned into text here: a recogniser (qt/src/hwr) reads each line of ink and gives, per word of
 * the line, its box and a few readings ("candidates") with the share of the recogniser's guesses each got. The search
 * matches its terms against all readings that are likely enough, so a word is found even when the best guess is
 * wrong ("Kalmar" for "Kalman"):
 *  - a reading takes part when it is the best one or has at least MIN_P of the guesses;
 *  - a term of 3 or more letters matches a reading as text matches a word (WordMatch.h): the reading contains it, or
 *    it is the term with a typo - also in the plain search (the typo tolerance applies to handwriting even with the
 *    fuzzy search off, the recogniser makes such errors), and with the fuzzy search its letters in order too;
 *  - a term of 1 or 2 letters only matches the whole best reading of a word the recogniser is sure of (SHORT_CONF):
 *    "a", "to" would otherwise be found in every second word of a page;
 *  - a term of several words (the plain search "dumb test") matches consecutive ink words, word by word;
 *  - one hit per ink word, whatever matched it; hits do not overlap;
 *  - a hit is exact (as sure as typed text) only when the best reading has the term and EXACT_P of the guesses;
 *    else it is fuzzy: the library lists a document whose hits are all fuzzy after those with exact ones, and the
 *    canvas marks hits below WEAK_P lighter.
 * Readings are kept by their numbers in the dictionary of words (Vocabulary.h: words::idOf), so a term is matched once
 * per distinct word (words::Matches), not per reading.
 *
 * Several models may read the same handwriting (English and German, hwr/MultiRecognizer.h): their readings of a word
 * are one list of candidates, each with the models that read it; the matching does not look at them.
 *
 * LineResult is what a recogniser gives for one line, with boxes relative to the line's origin (its top-left): a line
 * moved by the lasso keeps its result. PageText is a page's lines put together (page points), as DocumentTextIndex
 * keeps it per page next to the PDF text and the text elements.
 *
 * A line written at an angle (a margin note written upwards, a label along an arrow; hwr/InkLayout.h) was read in its
 * own frame, upright: its result's boxes are relative to its origin in that frame, and it is placed on the page with
 * its angle (PlacedLine). Its words on the page keep the angle: Word::box is then the box upright with its middle
 * where the word's middle is, turned by Word::angle around that middle (quadOf); 0, the common case, is a plain box.
 *
 * Thread-safe (the types do not change once made).
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include <QPointF>
#include <QRectF>
#include <QString>

#include "TextMatch.h"
#include "Vocabulary.h"

namespace xqt::ink {

/// A reading below this share of the guesses takes part only when it is the best one.
constexpr float MIN_P = 0.05f;
/// A hit is exact only when the best reading, with at least this share, has the term; hits below it are marked lighter.
constexpr float EXACT_P = 0.5f;
constexpr float WEAK_P = 0.5f;
/// A term of 1-2 letters matches only the whole best reading of a word whose confidence is at least this.
constexpr float SHORT_CONF = 0.7f;
/// How much bigger than the word's box a hit is marked (page points).
constexpr double MARK_MARGIN = 2.0;

struct Candidate {
    /// Its letters and digits, case folded and joined (TextMatch::words): its number in the dictionary (NO_WORD: none)
    words::Id word = words::NO_WORD;
    float p = 0;  ///< its share of the recogniser's guesses for this word (0..1)
    /// The models that read it, when several read the line (bit i: model i of the set, hwr/MultiRecognizer.h; their
    /// languages say which language it is); 0: one model read the line
    uint8_t models = 0;
};

struct Word {
    /// Page points (PageText), relative to the line's origin (LineResult). With an angle: the box upright, around the
    /// word's middle (its corners on the page: quadOf)
    QRectF box;
    float conf = 0;  ///< how sure the recogniser is of the best reading (0..1)
    QString text;    ///< the best reading as recognised (case, punctuation): for text layers and snippets
    std::vector<Candidate> candidates;  ///< best first
    /// The direction of its line on the page (degrees, clockwise: 0 left to right, 90 downwards, -90 upwards; PageText)
    float angle = 0;
    size_t bytes() const;
};

/// A point turned by `degrees` around (0, 0), clockwise on the page (y down): (1, 0) turned by 90 is (0, 1). Exact for
/// multiples of 90 degrees.
QPointF turned(QPointF p, double degrees);
/// Four corners on the page, clockwise from the top-left of a box upright: a box turned with its line. Its own type,
/// not QPolygonF, so that the session stays free of Qt GUI (the canvas makes polygons of them).
struct Quad {
    std::array<QPointF, 4> corners;
    const QPointF& operator[](int i) const { return corners[static_cast<size_t>(i)]; }
    QPointF& operator[](int i) { return corners[static_cast<size_t>(i)]; }
    static constexpr int size() { return 4; }
    auto begin() { return corners.begin(); }
    auto end() { return corners.end(); }
    auto begin() const { return corners.begin(); }
    auto end() const { return corners.end(); }
    QRectF boundingRect() const;
};
/// The corners of a word's box on the page (its box for angle 0), clockwise from the top-left of the word upright.
Quad quadOf(const Word& w);
Quad quadOf(const QRectF& box, double angle);
/// The box around a word's corners on the page.
QRectF boundsOf(const Word& w);

/// A reading as a candidate: its words folded and joined, numbered in the dictionary.
Candidate candidate(QStringView text, float p);
/// Its letters and digits, case folded and joined ("Don't" -> "dont").
QString folded(QStringView text);

/// What a recogniser read in one line of ink.
struct LineResult {
    std::vector<Word> words;  ///< left to right; boxes relative to the line's origin
    /// The models that read the line, when several may (bit i: model i of the set); 0: one model
    uint32_t models = 0;
    size_t bytes() const;
};

struct PlacedLine {
    QPointF origin;  ///< where the line's origin is on the page
    std::shared_ptr<const LineResult> result;
    double angle = 0;  ///< the line's direction (Word::angle): its words are turned by it around its origin
};

/// The handwriting of a page.
struct PageText {
    std::vector<Word> words;           ///< in reading order; boxes in page points
    std::vector<uint32_t> lineStarts;  ///< the first word of each line
    /// The lines put together (lines without a result are left out).
    static std::shared_ptr<const PageText> assemble(const std::vector<PlacedLine>& lines);
    /// The best readings: words by spaces, lines by '\n' (snippets).
    QString text() const;
    bool empty() const { return words.empty(); }
    size_t bytes() const;
};

struct Hit {
    uint32_t first = 0;  ///< its first and last word
    uint32_t last = 0;
    bool exact = false;  ///< the best reading (with EXACT_P or more) has the term
    float p = 0;         ///< the share of the readings that matched (a phrase: its weakest word)
};
/// The hits of `terms` (prepare()d, as DocumentSearch counts and marks them) on a page, in reading order. `typos`: the
/// typo tolerance of terms that are not fuzzy terms (WordMatch.h; -1: FuzzyQuery::typoTolerance()).
std::vector<Hit> find(const PageText& ink, const std::vector<textmatch::Term>& terms, int typos = -1);
/// The term is on the page.
bool contains(const PageText& ink, const textmatch::Term& term, int typos = -1);
/// Where a hit is marked: the box of its words, MARK_MARGIN bigger; one per line (a phrase may go on in the next). A
/// line at an angle: the box around its marked quad.
std::vector<QRectF> rectsOf(const PageText& ink, const Hit& hit);
/// The same as quads, turned like their lines (four corners each; plain boxes for lines at angle 0).
std::vector<Quad> quadsOf(const PageText& ink, const Hit& hit);
/// The hit has words at an angle (marked by quadsOf).
bool atAnAngle(const PageText& ink, const Hit& hit);

}  // namespace xqt::ink
