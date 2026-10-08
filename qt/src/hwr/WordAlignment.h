/*
 * xournal-qt: a recogniser's readings of a piece of a line, put on the word boxes the layout found.
 *
 * An image recogniser reads a piece of a line as one text ("This is a dumb test"), in a few variants (the beams of
 * its search, each with its log-probability). The search needs readings per word box (InkText.h), so each beam's
 * words are put on the boxes:
 *  - as many words as boxes: one to one, in order;
 *  - else by position, and a word goes to the box it overlaps most in x (several words of one box are joined with a
 *    space; a box may get none). Where the word is:
 *     - where the model read it, if the beam says so (a CTC model: from the frames of its characters, CtcDecode.h);
 *       a word beside every box goes to the nearest;
 *     - else estimated (TrOCR): the words share the piece's width by their letters (a word of 6 letters takes twice
 *       the room of one of 3), the boxes by their widths.
 *   The letter shares misplace short words next to long ones ("a wonderful": the "a" gets a twelfth of the width)
 *   and anything where the boxes are not as wide as their letters; the frames do not.
 * Then per box the readings of all beams are merged: a reading's share is the share of the beams that read it there
 * (the softmax of the beams' log-probabilities, summed over the beams with the same letters), best first. The
 * confidence of a word is that of the best beam: exp of its mean log-probability per token.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <vector>

#include <QRectF>
#include <QString>

#include "session/InkText.h"

namespace xqt::hwr {

/// Where a word was read: its left and right x, in the boxes' coordinates.
struct WordSpan {
    double left = 0;
    double right = 0;
};

struct Beam {
    QString text;
    double logProb = 0;       ///< of the whole reading
    double tokenLogProb = 0;  ///< the mean per token
    /// Per word of `text` (split at spaces), where the model read it; empty: not known (then estimated)
    std::vector<WordSpan> spans;
};

/// The words of `reading` per box (boxes left to right; "" for a box without one). `spans`: where each word was read
/// (one per word, else ignored).
std::vector<QString> align(const QString& reading, const std::vector<QRectF>& boxes,
                           const std::vector<WordSpan>& spans = {});

/// The words of a piece from its beams (best first): one per box that got a reading, at most `topK` readings each.
std::vector<ink::Word> wordsOf(const std::vector<Beam>& beams, const std::vector<QRectF>& boxes, int topK = 5);

}  // namespace xqt::hwr
