/*
 * xournal-qt: what a handwriting recogniser is to the search (qt/docs/handwriting-search.md).
 *
 * A recogniser reads one line of ink at a time (InkLayout.h found the lines and their words) and returns, per word
 * box of the line, a few readings with their shares (InkText.h). It is called on the recognition worker
 * (InkRecognitionService.h), one line after the other, and may take a while (a model in ONNX Runtime: about 0.2 s per
 * line). Backends:
 *  - TrocrRecognizer: TrOCR-small (handwritten, int8) in ONNX Runtime, on a rendered image of the line (Linux; the
 *    fallback elsewhere);
 *  - FakeRecognizer: scripted readings, for the tests;
 *  - later the platforms' own (Windows' ink recogniser, Apple's Vision).
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <functional>
#include <optional>
#include <vector>

#include <QRectF>
#include <QSizeF>
#include <QString>
#include <QStringList>

#include "InkLayout.h"
#include "session/InkText.h"

namespace xqt::hwr {

/// One line of ink as a recogniser gets it: its strokes relative to the line's origin (its top-left on the page).
struct LineInput {
    quint64 hash = 0;
    QSizeF size;                     ///< of the line's box
    double h = 0;                    ///< the page's units (InkLayout.h)
    double u = 0;
    std::vector<InkStroke> strokes;  ///< relative to the origin, in the order they were written
    std::vector<InkWordBox> words;   ///< the word boxes the layout found (relative), left to right; strokes: into `strokes`
    /// A line of a page's layout.
    static LineInput of(const std::vector<InkStroke>& page, const Layout& layout, const InkLine& line);
};

struct Capabilities {
    bool strokes = false;  ///< reads strokes (else an image of the line)
    QStringList languages;
    int topK = 5;          ///< readings per word at most
    /// The recogniser and its model ("trocr-small-hw-int8/<hash of the model files>/seg1"): results of another one are
    /// read again
    QString id;
    /// Several models (MultiRecognizer): their ids and languages, by their bit in ink::Candidate::models
    QStringList models;
    std::vector<QStringList> modelLanguages;
    /// The languages of the models with these bits (all languages for 0)
    QStringList languagesOf(uint32_t bits) const;
};

struct Context {
    QString language = QStringLiteral("en");
    /// Asked between the steps of a line: stop (the document closed, the app quits); the line has no result then.
    std::function<bool()> cancelled;
};

class Recognizer {
public:
    virtual ~Recognizer() = default;
    virtual Capabilities capabilities() const = 0;
    /// It can read lines (its model is there); else why not, for the settings (`why`).
    virtual bool ready(QString* why = nullptr) const = 0;
    /// The words of a line (boxes relative to its origin); nullopt if it failed or was cancelled. The worker's thread.
    virtual std::optional<ink::LineResult> recognizeLine(const LineInput& line, const Context& context) = 0;
    /// Give back the memory of the model (nothing was read for a while); it is loaded again when needed. The worker's
    /// thread.
    virtual void unload() {}
    /// Any thread: a recognition that runs stops soon (its line has no result).
    virtual void interrupt() {}
};

}  // namespace xqt::hwr
