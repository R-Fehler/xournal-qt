/*
 * xournal-qt: several handwriting models read the same lines (qt/docs/handwriting-search.md, "Languages").
 *
 * With "English and German" the search runs an English and a German model; their readings of a line are put together
 * per ink word, so a word is found through either. Nothing is transcribed: the search only needs every likely
 * reading. The members are the recognisers of the models in use, in a fixed order (HandwritingSearch: English first);
 * the id is theirs joined with "+" (results of another set of models are read again).
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
    std::optional<ink::LineResult> recognizeLine(const LineInput& line, const Context& context) override;
    void unload() override;
    void interrupt() override;

    const std::vector<std::shared_ptr<Recognizer>>& members() const { return all; }

    /// The words of one line read by several models, put together per word box (`results`: one per member, null if it
    /// did not read the line).
    static ink::LineResult merge(const std::vector<const ink::LineResult*>& results, int topK = 5);

private:
    std::vector<std::shared_ptr<Recognizer>> all;
};

}  // namespace xqt::hwr
