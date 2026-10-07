/*
 * xournal-qt: which handwriting models read the lines of one document (qt/docs/handwriting-search.md, "Which language
 * a document is in"), to save work when English and German models are both in use.
 *
 * Reading a line costs about 0.2 s per model; most documents are in one language. With both models in use and the
 * document's choice "Automatic" (the default):
 *  1. both models read the first PROBE_LINES lines; each line gives each model a score, the mean confidence of its
 *     words (TrOCR: the probability per token, CTC: per character, Recognizer.h);
 *  2. when one model's mean is MARGIN or more above the other's, the document is decided for its language: from then
 *     on that model reads every line first, and the other one only the lines it is unsure of (score below UNSURE);
 *     else (a mixed document, or both unsure) both keep reading every line, and the decision is tried again after
 *     each further PROBE_LINES lines both read;
 *  3. the decision is revisited when the preferred model's confidence drops: when half of the last WINDOW lines it
 *     read were unsure, both read again from the next line on (step 1).
 * The library's vocabulary per language is not used: the library has one dictionary of all its words, not one per
 * language, so it would not tell English from German.
 *
 * The user may choose for a document (⋮ → Document → Handwriting language): English or German (only the models that
 * read it, if any is in use), Both (every model, every line) or Automatic. The choice and the decision are kept in
 * the library's handwriting cache (InkTextStore: "langChoice", "lang"), not in the document's file.
 *
 * A plan belongs to one document (InkTextIndexer, LibraryInkJob) and goes with its pages to the recognition worker
 * (InkRecognitionService::Job, Context::plan), where MultiRecognizer asks it which models read a line and tells it
 * the scores. Thread-safe.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <vector>

#include <QString>
#include <QStringList>

namespace xqt::hwr {

class LanguagePlan {
public:
    enum class Choice { Automatic, English, German, Both };
    static constexpr int PROBE_LINES = 6;
    static constexpr float MARGIN = 0.15f;
    static constexpr float UNSURE = 0.5f;
    static constexpr int WINDOW = 12;

    explicit LanguagePlan(Choice choice = Choice::Automatic, QString decided = {});

    /// "auto", "en", "de", "both" (as stored)
    static QString nameOf(Choice choice);
    static Choice choiceNamed(const QString& name);

    Choice choice() const;
    void setChoice(Choice choice);
    /// The language decided for the document ("en", "de"; "": none yet, both read).
    QString decided() const;

    // --- the worker (MultiRecognizer): `models` are the languages of each model, by its bit ---
    /// The models that read a line first.
    uint32_t first(const std::vector<QStringList>& models) const;
    /// The other models read the line too: the first ones' score is below UNSURE and the document is decided
    /// automatically.
    bool othersToo(float firstScore) const;
    /// A line was read: the score of each model that read it now (nullopt: it did not).
    void observe(const std::vector<std::optional<float>>& scores, const std::vector<QStringList>& models);

private:
    uint32_t membersOf(const QString& language, const std::vector<QStringList>& models) const;
    void decide(const std::vector<QStringList>& models);

    mutable std::mutex mtx;
    Choice chosen;
    QString language;
    std::vector<double> sums;   ///< per model: its scores of the lines both read since the last decision
    std::vector<int> counts;
    std::deque<bool> recent;    ///< the preferred model's last lines: unsure
};

}  // namespace xqt::hwr
