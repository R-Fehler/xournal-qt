/*
 * xournal-qt: a recogniser with scripted readings, for the tests of the handwriting search.
 *
 * Every word box of a line gets the readings the script gives for it (by the line's hash and the word's place in it,
 * or by its box); without a script, word i of a line reads "w<i>". It counts its calls (lines read), can be slow
 * (setDelay) to test pausing and cancelling, and can be not ready.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <atomic>
#include <functional>
#include <map>
#include <mutex>

#include "Recognizer.h"

namespace xqt::hwr {

class FakeRecognizer final: public Recognizer {
public:
    using Readings = std::vector<std::pair<QString, float>>;  ///< best first
    /// Readings of word `word` of `line` (empty: no word there).
    using Script = std::function<Readings(const LineInput& line, size_t word)>;

    explicit FakeRecognizer(QString id = QStringLiteral("fake/1"), QStringList languages = {QStringLiteral("en")});

    void setScript(Script script);
    /// The words of the line with this hash read so (one entry per word box; more or fewer are cut or left empty).
    void setLine(quint64 hash, std::vector<Readings> words);
    void setReady(bool ready, QString why = {});
    /// Each line takes this long (checked for cancelling every 5 ms).
    void setDelay(int ms) { delayMs = ms; }
    /// Lines read so far (not counting cancelled ones).
    int calls() const { return callCount.load(); }
    int unloads() const { return unloadCount.load(); }

    Capabilities capabilities() const override;
    bool ready(QString* why = nullptr) const override;
    std::optional<ink::LineResult> recognizeLine(const LineInput& line, const Context& context) override;
    void unload() override { ++unloadCount; }
    void interrupt() override { interrupted = true; }

private:
    QString recId;
    QStringList langs;
    mutable std::mutex mtx;
    Script script;
    std::map<quint64, std::vector<Readings>> lines;
    bool isReady = true;
    QString notReady;
    std::atomic<int> delayMs{0};
    std::atomic<int> callCount{0}, unloadCount{0};
    std::atomic<bool> interrupted{false};
};

}  // namespace xqt::hwr
