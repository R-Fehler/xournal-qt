/*
 * xournal-qt: the parts of an image-to-text recogniser that need no model runtime: beam search over a decoder's steps,
 * and turning its tokens back into text (TrOCR's tokenizer.json).
 *
 * Beam search as in the trials (qt/research/hwr/trocr_onnx.py): k beams; each step every beam proposes its k likeliest
 * next tokens, the k best of all continue (a beam that ends is kept as a result); it stops when k have ended or after
 * `maxLength` steps; the results are ranked by their log-probability per token, without duplicates. The decoder is a
 * function (StepFn): the ONNX model in TrocrRecognizer, a scripted one in the tests.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include <QByteArray>
#include <QString>

namespace xqt::hwr {

struct BeamResult {
    std::vector<int64_t> tokens;  ///< without the start token, with the end token if it ended
    double logProb = 0;           ///< of the whole sequence
    double meanLogProb = 0;       ///< per token
};

/// One step of the decoder for `rows` sequences: `tokens` are their last tokens; `parents[r]` is the row (of the step
/// before) whose sequence row r continues (empty at the first step: all rows start anew). Fills `logits` with a row of
/// scores per sequence (all as long as the vocabulary). False: failed or interrupted (no result).
using StepFn = std::function<bool(const std::vector<int64_t>& tokens, const std::vector<size_t>& parents,
                                  std::vector<std::vector<float>>& logits)>;

/// The `k` best sequences (best first; empty if a step failed).
std::vector<BeamResult> beamSearch(const StepFn& step, int64_t start, int64_t end, int k, int maxLength);

/// The text of TrOCR's tokens (tokenizer.json of the Hugging Face tokenizers: a SentencePiece vocabulary, "▁" for a
/// space, or a byte-level BPE one). Special tokens are left out.
class Tokenizer {
public:
    /// False and `error` if the file cannot be read.
    bool load(const QString& file, QString* error = nullptr);
    /// From the JSON's bytes.
    bool parse(const QByteArray& json, QString* error = nullptr);
    QString decode(const std::vector<int64_t>& ids) const;
    size_t size() const { return pieces.size(); }

private:
    std::vector<QByteArray> pieces;  ///< by id (UTF-8)
    std::vector<char> special;
    bool byteLevel = false;
};

}  // namespace xqt::hwr
