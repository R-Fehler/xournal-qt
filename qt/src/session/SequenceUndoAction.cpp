#include "SequenceUndoAction.h"

#include <algorithm>

#include "undo/UndoRedoHandler.h"

#include "DocumentSession.h"

namespace xqt {

SequenceUndoAction::SequenceUndoAction(std::vector<UndoActionPtr> steps, std::string text):
        UndoAction("SequenceUndoAction"), steps(std::move(steps)), text(std::move(text)) {}

bool SequenceUndoAction::undo(Control* control) {
    bool ok = true;
    for (auto it = steps.rbegin(); it != steps.rend(); ++it) {
        ok = (*it)->undo(control) && ok;  // (the rest still undone: a half undone sequence is worse)
    }
    undone = true;
    return ok;
}

bool SequenceUndoAction::redo(Control* control) {
    bool ok = true;
    for (auto& step: steps) {
        ok = step->redo(control) && ok;
    }
    undone = false;
    return ok;
}

std::vector<PageRef> SequenceUndoAction::getPages() {
    std::vector<PageRef> pages;
    for (auto& step: steps) {
        for (const PageRef& p: step->getPages()) {
            if (p && std::find(pages.begin(), pages.end(), p) == pages.end()) {
                pages.push_back(p);
            }
        }
    }
    return pages;
}

UndoGathering::UndoGathering(DocumentSession& session, std::string text): session(session), text(std::move(text)) {
    UndoRedoHandler* handler = session.getUndoRedoHandler();
    if (handler->sink) {
        return;  // (another gathering runs: this one gathers nothing)
    }
    handler->sink = [this](UndoActionPtr step) { steps.push_back(std::move(step)); };
    gathering = true;
}

UndoGathering::~UndoGathering() {
    if (gathering) {
        rollback();
    }
}

void UndoGathering::stop() {
    if (gathering) {
        session.getUndoRedoHandler()->sink = nullptr;
        gathering = false;
    }
}

bool UndoGathering::commit() {
    stop();
    if (steps.empty()) {
        return false;
    }
    session.getUndoRedoHandler()->addUndoAction(std::make_unique<SequenceUndoAction>(std::move(steps), text));
    steps.clear();
    return true;
}

void UndoGathering::rollback() {
    stop();
    if (steps.empty()) {
        return;
    }
    SequenceUndoAction all(std::move(steps), text);
    steps.clear();
    all.undo(&session);
    // (what the steps touched gets a new revision, as after an undo: thumbnails and sketches follow)
    session.getUndoRedoHandler()->fireUpdateUndoRedoButtons(all.getPages());
}

}  // namespace xqt
