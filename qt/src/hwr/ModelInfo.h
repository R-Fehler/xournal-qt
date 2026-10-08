/*
 * xournal-qt: what a handwriting model folder holds, from its manifest (qt/research/hwr/train/FORMATS.md, §2).
 *
 * A model is a folder with "model.json" and the files it names. The manifest says its kind ("trocr": a vision
 * encoder-decoder, TrocrRecognizer.h; "ctc": a convolutional + recurrent line model, CtcRecognizer.h), its name, the
 * languages it reads (ISO 639-1: "en", "de"; a combined model lists both) and its version. A manifest written before
 * these fields existed (qt/scripts/hwr-model.sh's first version) is TrOCR-small for English.
 *
 * The model's id names the results it read (the recogniser's id, Recognizer.h): its name and the first 12 hex digits of
 * the manifest's sha256 ("trocr-small-hw-int8/0123456789ab"); the recogniser adds the version of the way it cuts
 * lines. Another model, or another version of one, reads the library's handwriting again.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <QString>
#include <QStringList>

namespace xqt::hwr {

struct ModelInfo {
    QString folder;
    QString kind;  ///< "trocr" or "ctc"
    QString name;
    QStringList languages;
    QString version;
    QString licence;
    bool noncommercial = false;  ///< its weights are for non-commercial use only (the manifest's "noncommercial")
    QString hash;   ///< the first 12 hex digits of the manifest's sha256
    QString error;  ///< why it is not a model ("" if it is one)

    bool valid() const { return error.isEmpty(); }
    bool reads(const QString& language) const { return languages.contains(language); }
    /// "<name>/<hash>"
    QString id() const { return name + u'/' + hash; }
    /// The manifest of `folder` (error set if there is none or it is not one this app reads).
    static ModelInfo read(const QString& folder);
};

}  // namespace xqt::hwr
