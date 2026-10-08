/*
 * xournal-qt: reading a CTC model's output (qt/research/hwr/train/FORMATS.md, "kind": "ctc"): the prefix beam search
 * and the model's alphabet. No model runtime here (CtcRecognizer.h runs the model; the tests script the logits).
 *
 * A CTC model gives, per frame t (a slice of the line's width), the log-probability of each class: class 0 (or the
 * manifest's `blank`) is the blank, the others are the alphabet's characters. A reading is a path through the frames
 * with repeats merged and blanks dropped ("aa-b" -> "ab"; "a-a" -> "aa"). The prefix beam search keeps the `width`
 * likeliest prefixes (texts so far) with the probability of all paths that lead to each, split into those ending in
 * a blank and those ending in its last character (so "a" then "a" again is told apart from "aa"); per frame only the
 * likeliest PRUNE classes and the blank are tried. The result: the `topK` likeliest texts with their log-probability.

Each reading also says where its characters are: per character the first and the last frame it was read on, on the
likeliest path that reads the text (per prefix, the paths ending in a blank and those ending in its last character
each keep the frames of their likeliest contribution). A frame is a slice of the picture's width (the model's
downsampling: frame t of T covers t / T to (t + 1) / T of it), so CtcRecognizer puts the words on the ink's word
boxes by where they were read (WordAlignment.h).
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <cstddef>
#include <vector>

#include <QByteArray>
#include <QString>
#include <QStringList>

namespace xqt::hwr {

/// The frames a character was read on: first to last (both included).
struct CtcSpan {
    int first = 0;
    int last = 0;
    bool operator==(const CtcSpan& o) const { return first == o.first && last == o.last; }
};

struct CtcReading {
    std::vector<int> labels;     ///< the classes read (no blanks, repeats merged)
    double logProb = 0;          ///< of all paths that read it
    std::vector<CtcSpan> spans;  ///< per label: its frames on the likeliest path that reads it
};

/// `logits`: frames x classes (row by row), log-probabilities or scores (each frame is log-softmax'ed here again, so
/// either is fine). Best first; at most `topK`, never an empty list for frames > 0 (the empty reading counts).
std::vector<CtcReading> ctcBeamSearch(const std::vector<float>& logits, size_t frames, size_t classes, int blank = 0,
                                      int width = 8, int topK = 5);

/// How sure the model is of what it read: the mean log-probability of the characters on the likeliest path (each
/// character's best frame), as TrOCR's is per token (0: sure; very negative: no character read).
double ctcConfidence(const std::vector<float>& logits, size_t frames, size_t classes, int blank = 0);

/// The characters of a CTC model (alphabet.txt: one per line, UTF-8, NFC; class i + 1 is line i, a line holding only a
/// space is the space).
class CtcAlphabet {
public:
    bool load(const QString& file, QString* error = nullptr);
    bool parse(const QByteArray& text, QString* error = nullptr);
    /// The text of a reading's classes (`blank`: the blank's class; the others are the alphabet's in order around it).
    QString text(const std::vector<int>& labels, int blank = 0) const;
    /// The text of one class ("" for the blank or a class out of range).
    QString charOf(int label, int blank = 0) const;
    size_t size() const { return static_cast<size_t>(chars.size()); }

private:
    QStringList chars;
};

}  // namespace xqt::hwr
