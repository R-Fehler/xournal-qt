/*
 * xournal-qt: what the handwriting search runs on, for `xournal-qt --hwr-info` and the CI's smoke tests of the packages
 * (qt/docs/features/handwriting-search.md, "The built-in model"), as `--audio-info` is for recording.
 *
 * It says whether ONNX Runtime was found (where, which version), which models come with the app (the folders with a
 * model.json in "<resource dir>/hwr-models/": name, kind, languages) and, per model, whether it reads a sample line
 * built in here (the word "Hallo" written with a few strokes) without an error, and what it read. The models' files
 * are checked against their manifest's sizes and sha256 on the way (the recognisers do that before they read).
 *
 * Only with ONNX Runtime support (XQT_HWR_ONNX).
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <memory>
#include <string>

#include <QString>

#include "Recognizer.h"

namespace xqt::hwr {

/// The recogniser of a model folder by its manifest's kind: CtcRecognizer for "ctc", else TrocrRecognizer (which says
/// what is missing in a folder without a model). The app's factory (HandwritingSearch::setFactory).
std::shared_ptr<Recognizer> onnxRecognizerFor(const QString& modelDir);

/// The sample line: "Hallo", written in the order a hand writes it, about 20 pt high (one word box).
LineInput sampleLine();

struct HwrReport {
    /// One fact per line: "onnxruntime: found (<path>, version <v>)" or "onnxruntime: not found (<why>)"; "models:
    /// <folder>" or "models: none in <folder>"; per model "model: <name> (<kind>; <languages>; version <v>[;
    /// non-commercial]) in <folder>" and "  reads the sample line: \"<text>\" (<ms> ms)" or "  cannot read the
    /// sample line (<why>)"; last "hwr: ready" or "hwr: not ready".
    std::string text;
    /// The runtime is there, at least one model comes with the app, and each reads the sample line.
    bool ok = false;
};
/// `bundledDir`: the models that come with the app (HandwritingSearch::bundledModelsDir).
HwrReport describe(const QString& bundledDir);

}  // namespace xqt::hwr
