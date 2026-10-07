/*
 * xournal-qt: the recogniser of the handwriting search on Linux (and the fallback elsewhere): TrOCR-small, handwritten,
 * int8, in ONNX Runtime (qt/research/hwr/research.md: 97 % of the English words found among its
 * readings on IAM, about 0.2 s per line on two threads of a laptop).
 *
 * The model is a folder (HandwritingSearch::modelDir) with the ONNX export's files and a manifest "model.json"
 * (qt/research/hwr/train/FORMATS.md, "kind": "trocr"; ModelInfo.h):
 *   { "kind": "trocr", "name": "trocr-small-hw-int8", "languages": ["en"], "source": ..., "revision": ...,
 *     "encoder": "onnx/encoder_model_quantized.onnx", "decoder": "onnx/decoder_model_merged_quantized.onnx",
 *     "tokenizer": "tokenizer.json", "decoder_start_token_id": 2, "eos_token_id": 2, "image_size": 384,
 *     "files": { "<path>": { "sha256": "...", "size": n }, ... } }
 * written by qt/scripts/hwr-model.sh or the download in Settings. The recogniser's id is the name, the first 12 hex
 * digits of the manifest's sha256 and the layout's version ("trocr-small-hw-int8/0123456789ab/seg1"): another model or
 * another way of cutting lines reads the library's handwriting again. The files are checked against the manifest's
 * sizes when asked whether it is ready (cheap), and against their sha256 when the model is loaded (once).
 *
 * A line is cut into pieces of at most 8 words (LineImage.h), each drawn as TrOCR's 384 x 384 input, read by the
 * encoder once and decoded with beam search (BEAMS beams, BeamSearch.h) on the merged decoder with its key/value
 * cache; the beams' texts are put on the word boxes (WordAlignment.h). Two threads, no spinning, no memory arena; the
 * model (about 250 MB in memory) is loaded on the worker the first time a line is read and unloaded when the worker
 * has been idle for a minute. interrupt() stops a run at once (ONNX Runtime's terminate flag).
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <atomic>
#include <map>
#include <memory>
#include <mutex>

#include "BeamSearch.h"
#include "OrtRuntime.h"
#include "Recognizer.h"
#include "WordAlignment.h"

namespace xqt::hwr {

struct Manifest {
    QString name;
    QStringList languages;  ///< (ModelInfo.h; English if the manifest names none)
    QString encoder, decoder, tokenizer;
    int64_t start = 2, end = 2;
    int imageSize = 384;
    struct File {
        QString sha256;
        qint64 size = -1;
    };
    std::map<QString, File> files;
    QString id;     ///< the recogniser's id (see above)
    QString error;  ///< why it is not usable ("" if it is)
    bool valid() const { return error.isEmpty(); }
    /// Read "model.json" in `dir`.
    static Manifest read(const QString& dir);
};

class TrocrRecognizer final: public Recognizer {
public:
    static constexpr int BEAMS = 4;
    static constexpr int MAX_TOKENS = 48;
    static constexpr int THREADS = 2;
    static constexpr const char* LAYOUT_VERSION = "seg1";

    explicit TrocrRecognizer(QString modelDir);
    ~TrocrRecognizer() override;

    Capabilities capabilities() const override;
    bool ready(QString* why = nullptr) const override;
    std::optional<ink::LineResult> recognizeLine(const LineInput& line, const Context& context) override;
    void unload() override;
    void interrupt() override;

    /// The readings of one piece's picture (the model's input, 3 x size x size): its beams (tests, benchmarks).
    std::optional<std::vector<Beam>> readPicture(const std::vector<float>& pixels, const Context& context);
    bool loaded() const;

private:
    bool load(QString* why);

    QString dir;
    Manifest manifest;
    mutable std::mutex mtx;  ///< the sessions (load, unload, interrupt)
    std::unique_ptr<ort::Session> encoder, decoder;
    std::unique_ptr<ort::RunOptions> runOptions;
    Tokenizer tokenizer;
    bool checked = false;  ///< the files' sha256 were checked
    bool damaged = false;
    QString damage;
};

}  // namespace xqt::hwr
