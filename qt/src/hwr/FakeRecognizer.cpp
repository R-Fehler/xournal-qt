#include "FakeRecognizer.h"

#include <chrono>
#include <thread>

namespace xqt::hwr {

FakeRecognizer::FakeRecognizer(QString id): recId(std::move(id)) {}

void FakeRecognizer::setScript(Script s) {
    std::lock_guard lock(mtx);
    script = std::move(s);
}

void FakeRecognizer::setLine(quint64 hash, std::vector<Readings> words) {
    std::lock_guard lock(mtx);
    lines[hash] = std::move(words);
}

void FakeRecognizer::setReady(bool ready, QString why) {
    std::lock_guard lock(mtx);
    isReady = ready;
    notReady = std::move(why);
}

Capabilities FakeRecognizer::capabilities() const {
    Capabilities c;
    c.languages = {QStringLiteral("en")};
    c.topK = 5;
    c.id = recId;
    return c;
}

bool FakeRecognizer::ready(QString* why) const {
    std::lock_guard lock(mtx);
    if (!isReady && why) {
        *why = notReady;
    }
    return isReady;
}

std::optional<ink::LineResult> FakeRecognizer::recognizeLine(const LineInput& line, const Context& context) {
    interrupted = false;
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(delayMs.load());
    while (std::chrono::steady_clock::now() < until) {
        if (interrupted || (context.cancelled && context.cancelled())) {
            return std::nullopt;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    if (interrupted || (context.cancelled && context.cancelled())) {
        return std::nullopt;
    }
    Script s;
    std::vector<Readings> scripted;
    {
        std::lock_guard lock(mtx);
        s = script;
        if (auto it = lines.find(line.hash); it != lines.end()) {
            scripted = it->second;
        }
    }
    ink::LineResult result;
    for (size_t i = 0; i < line.words.size(); ++i) {
        Readings readings;
        if (i < scripted.size()) {
            readings = scripted[i];
        } else if (s) {
            readings = s(line, i);
        } else if (scripted.empty()) {
            readings = {{QStringLiteral("w%1").arg(i), 1.0f}};
        }
        if (readings.empty()) {
            continue;
        }
        ink::Word w;
        w.box = line.words[i].box;
        w.conf = 0.9f;
        w.text = readings.front().first;
        for (const auto& [text, p]: readings) {
            w.candidates.push_back(ink::candidate(text, p));
        }
        result.words.push_back(std::move(w));
    }
    ++callCount;
    return result;
}

}  // namespace xqt::hwr
