/*
 * xournal-qt: a recogniser for small CTC line models in ONNX Runtime (qt/research/hwr/train/FORMATS.md, "kind":
 * "ctc"): a convolutional + recurrent network that reads a line's picture left to right, as the project trains them
 * for German (qt/research/hwr/train).
 *
 * The model is a folder with a manifest "model.json":
 *   { "kind": "ctc", "name": "crnn-de", "languages": ["de"], "version": "2026.10.1", "model": "model_int8.onnx",
 *     "alphabet": "alphabet.txt", "blank": 0, "input_height": 64, "max_width": 2048,
 *     "files": { "<path>": { "sha256": "...", "size": n }, ... } }
 * Its input "image" is float32 [1, 1, input_height, W] (W <= max_width; ink 1 on paper 0: LineImage.h inkOf, the same
 * picture TrOCR gets, scaled to the height), its output "logits" float32 [T, 1, C] (log-softmax over the blank and the
 * alphabet's characters, CtcDecode.h). A line is read whole; a line too wide for max_width at that height is cut at
 * word gaps into pieces that fit (as few as possible). Each piece is decoded with the CTC prefix beam search (BEAMS
 * prefixes, the TOP_K likeliest texts with their probabilities), its texts split into words at spaces and put on the
 * ink's word boxes (WordAlignment.h): one to one when there are as many words as boxes, else each to the box it
 * overlaps most where the model read it. A word's place: from the first frame of its first character to the last
 * of its last (the frames are equal slices of the picture's width: T = W / 4 for the training's CRNN), mapped back
 * through the picture's scale and margin to the line (LineImage.h xAt). The confidence of a word is the model's per
 * character: exp of the mean log-probability of the characters on the likeliest path (ctcConfidence), as TrOCR's is
 * per token.
 *
 * The id: the model's name, the first 12 hex digits of the manifest's sha256 and the version of this reading
 * ("crnn-de/0123456789ab/ctc2"; ctc2: words placed where they were read, so results of ctc1 are read again). Files
 * are checked against the manifest's sizes when asked whether it is ready, and against their sha256 when the model
 * is loaded (once). Two threads; the model is loaded on the worker the first time a line is read and unloaded when
 * the worker has been idle for a minute; interrupt() stops a run at once.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <map>
#include <memory>
#include <mutex>

#include "CtcDecode.h"
#include "OrtRuntime.h"
#include "Recognizer.h"
#include "WordAlignment.h"

namespace xqt::hwr {

struct CtcManifest {
    QString name;
    QStringList languages;
    QString model, alphabet;
    int blank = 0;
    int inputHeight = 64;
    int maxWidth = 2048;
    struct File {
        QString sha256;
        qint64 size = -1;
    };
    std::map<QString, File> files;
    QString id;     ///< the recogniser's id (see above)
    QString error;  ///< why it is not usable ("" if it is)
    bool valid() const { return error.isEmpty(); }
    static CtcManifest read(const QString& dir);
};

/// Where the words of a reading are, as fractions of the picture's width (frame t of `frames` is t / frames to
/// (t + 1) / frames of it); one per word of its text (simplified(), split at spaces), empty without its spans.
std::vector<WordSpan> wordSpansOf(const CtcReading& reading, size_t frames, const CtcAlphabet& alphabet, int blank = 0);

class CtcRecognizer final: public Recognizer {
public:
    static constexpr int BEAMS = 8;
    static constexpr int TOP_K = 5;
    static constexpr int THREADS = 2;
    static constexpr const char* VERSION = "ctc2";

    explicit CtcRecognizer(QString modelDir);
    ~CtcRecognizer() override;

    Capabilities capabilities() const override;
    bool ready(QString* why = nullptr) const override;
    std::optional<ink::LineResult> recognizeLine(const LineInput& line, const Context& context) override;
    void unload() override;
    void interrupt() override;

    /// The readings of one picture (inkOf: height x width, ink 1), best first (tests); their words' spans as
    /// fractions of the picture's width (0 to 1).
    std::optional<std::vector<Beam>> readPicture(const std::vector<float>& ink, int width, const Context& context);

    bool loaded() const;

private:
    bool load(QString* why);

    QString dir;
    CtcManifest manifest;
    mutable std::mutex mtx;
    std::unique_ptr<ort::Session> session;
    std::unique_ptr<ort::RunOptions> runOptions;
    CtcAlphabet alphabet;
    bool checked = false;
    bool damaged = false;
    QString damage;
};

}  // namespace xqt::hwr
