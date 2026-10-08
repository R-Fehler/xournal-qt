/*
 * xournal-qt: a line of ink as the picture an image recogniser reads (TrOCR: 384 x 384 RGB).
 *
 * As in the trials (qt/research/hwr/segment.py, trocr_onnx.py): black ink with round caps on white, a piece drawn
 * LINE_PX pixels high with a margin of a quarter of its height, the strokes as wide as they are (at least a pixel). TrOCR squeezes any
 * picture to 384 x 384, so a long line is cut at word gaps into pieces of at most MAX_WORDS words (pieces of about
 * equal numbers of words), each read on its own. The picture is drawn with cairo into an A8 surface (one per thread,
 * kept) and scaled to the model's size (averaged when shrinking, linear when growing), then turned into the model's
 * input: three equal channels, (v / 255 - 0.5) / 0.5. A CTC model (CtcRecognizer.h) gets the same picture scaled to
 * its input height instead, ink as 1 (inkOf).
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <vector>

#include <QRectF>

#include "Recognizer.h"

namespace xqt::hwr {

constexpr int LINE_PX = 128;
constexpr int MAX_WORDS = 8;
constexpr int MODEL_PX = 384;

/// A piece of a line: words [first, last] of the line's words, and the box around them (line coordinates).
struct LinePiece {
    size_t first = 0;
    size_t last = 0;
    QRectF box;
};
std::vector<LinePiece> piecesOf(const LineInput& line, int maxWords = MAX_WORDS);

/// The piece drawn as grey values (0 ink .. 255 paper), LINE_PX high; width x height in `width`,
/// `height` (tests, the benchmark's pictures).
std::vector<unsigned char> greyOf(const LineInput& line, const LinePiece& piece, int& width, int& height);
/// The model's input for a piece: 3 x size x size floats (channel by channel).
std::vector<float> pixelsOf(const LineInput& line, const LinePiece& piece, int size = MODEL_PX);
/// A CTC model's input for a piece (qt/research/hwr/train/FORMATS.md, "kind": "ctc"): the picture greyOf() draws,
/// scaled to `height` pixels keeping its aspect ratio (at most `maxWidth` wide: a longer one is squeezed), ink 1 on
/// paper 0, row by row; its width in `width`.
std::vector<float> inkOf(const LineInput& line, const LinePiece& piece, int height, int maxWidth, int& width);
/// How wide the piece's picture is when drawn `height` pixels high (before any squeezing).
double widthAt(const LinePiece& piece, int height);
/// The line's x at `fraction` (0: left edge, 1: right edge) of the piece's picture (greyOf, pixelsOf, inkOf: the
/// margin included, at any scale); how a CTC model's frames find their place on the ink.
double xAt(const LinePiece& piece, double fraction);

}  // namespace xqt::hwr
