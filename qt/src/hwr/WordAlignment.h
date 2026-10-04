/*
 * xournal-qt: a recogniser's readings of a piece of a line, put on the word boxes the layout found.
 *
 * An image recogniser reads a piece of a line as one text ("This is a dumb test"), in a few variants (the beams of
 * its search, each with its log-probability). The search needs readings per word box (InkText.h), so each beam's
 * words are put on the boxes:
 *  - as many words as boxes: one to one, in order;
 *  - else by position: the words share the piece's width by their letters (a word of 6 letters takes twice the room
 *    of one of 3), the boxes by their widths, and a word goes to the box it overlaps most (several words of one box
 *    are joined with a space; a box may get none).
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

struct Beam {
    QString text;
    double logProb = 0;       ///< of the whole reading
    double tokenLogProb = 0;  ///< the mean per token
};

/// The words of `reading` per box (boxes left to right; "" for a box without one).
std::vector<QString> align(const QString& reading, const std::vector<QRectF>& boxes);

/// The words of a piece from its beams (best first): one per box that got a reading, at most `topK` readings each.
std::vector<ink::Word> wordsOf(const std::vector<Beam>& beams, const std::vector<QRectF>& boxes, int topK = 5);

}  // namespace xqt::hwr
