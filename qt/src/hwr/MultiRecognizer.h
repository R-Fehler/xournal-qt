/*
 * xournal-qt: several handwriting models read the same lines (qt/docs/handwriting-search.md, "Languages and models").
 *
 * With "English and German" the search runs an English and a German model; their readings of a line are put together
 * per ink word, so a word is found through either. Nothing is transcribed: the search only needs every likely
 * reading. The members are the recognisers of the models in use, in a fixed order (HandwritingSearch: English first);
 * member i is bit i of ink::Candidate::models and ink::LineResult::models, so each reading keeps the models (and
 * through Capabilities::languagesOf their languages) that read it. The id is the members' joined with "+" (results of
 * another set of models are read again).
 *
 * Which models read a line: those the document's plan (LanguagePlan.h) wants first, and the others too when the
 * first ones are unsure of it; each model's score of the line (the mean confidence of its words) goes back to the
 * plan. A line read before by some of the models (context.before, the worker's cache or the library's) is read only
 * by those that are missing.
 *
 * Putting readings together (merge): the members align their readings to the same word boxes of the layout, so the
 * words are matched by their box. Per word, every reading of every model goes into one list; a reading that several
 * models gave (the same letters, folded) is there once, with the probability that at least one of them is right,
 * 1 - (1 - p1)(1 - p2) ("noisy-OR"): two models that agree on "Kalman" with 0.6 and 0.5 make it 0.8, more than either
 * alone, so the search ranks it above a word only one model read with 0.6 (InkText.h's matching and ranking are
 * unchanged). The maximum would ignore that two independent models agree; a sum would go over 1. The word's confidence
 * is the highest of the models', its text the best reading as the model that gave it wrote it. Merging is associative:
 * a line read by one model and later by the other (LanguagePlan.h) gets the same list as when both read it at once.
 *
 * The worker's thread, like any recogniser.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <memory>
#include <vector>

#include "Recognizer.h"

namespace xqt::hwr {

class MultiRecognizer final: public Recognizer {
public:
    explicit MultiRecognizer(std::vector<std::shared_ptr<Recognizer>> members);

    Capabilities capabilities() const override;
    /// Ready when one of them is (the others' lines are read when they are).
    bool ready(QString* why = nullptr) const override;
    /// The models the document's plan wants (LanguagePlan.h; all without one) that did not read it yet
    /// (context.before), and the others too when the first ones are unsure; their readings and the line's before put
    /// together.
    std::optional<ink::LineResult> recognizeLine(const LineInput& line, const Context& context) override;
    /// Every model the plan wants first (and that can read now) read the line.
    bool enough(const ink::LineResult& known, const Context& context) const override;
    void unload() override;
    void interrupt() override;

    const std::vector<std::shared_ptr<Recognizer>>& members() const { return all; }

    /// A member's reading of a line, marked as member `member`'s (its bit on the line and every candidate).
    static ink::LineResult tagged(ink::LineResult result, size_t member);
    /// Readings of one line put together per word box (see above); null entries are left out. Each word keeps at most
    /// `topK` readings per model that read it.
    static ink::LineResult merge(const std::vector<const ink::LineResult*>& results, int topK = 5);
    /// A line's score: the mean confidence of its words (0 without any).
    static float scoreOf(const ink::LineResult& result);

private:
    std::vector<std::shared_ptr<Recognizer>> all;
};

}  // namespace xqt::hwr
