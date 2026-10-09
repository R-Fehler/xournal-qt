/*
 * xournal-qt: several undo steps as one (a plugin command is one undo step, qt/docs/decisions/0008-js-plugins.md).
 *
 * SequenceUndoAction holds steps that depend on each other ("a new layer, then strokes into it"): undone in reverse
 * order, redone in order. (Upstream's GroupUndoAction undoes in the order of redo, which is right only for independent
 * steps of one kind.)
 *
 * UndoGathering collects what is pushed onto a session's undo stack while it lives (the stack's sink, a seam in
 * upstream's UndoRedoHandler, ADR 0002): every existing command and upstream's own undo actions can be used, and the
 * result is still one step. commit() pushes them as one SequenceUndoAction; rollback() (and the destructor, when not
 * committed) undoes them in reverse order, so the document is as it was.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "model/PageRef.h"
#include "undo/UndoAction.h"

namespace xqt {

class DocumentSession;

class SequenceUndoAction final: public UndoAction {
public:
    SequenceUndoAction(std::vector<UndoActionPtr> steps, std::string text);

    bool undo(Control* control) override;
    bool redo(Control* control) override;
    std::string getText() override { return text; }
    /// The pages of all steps (each once)
    std::vector<PageRef> getPages() override;
    size_t size() const { return steps.size(); }

private:
    std::vector<UndoActionPtr> steps;
    std::string text;
};

class UndoGathering {
public:
    /// Starts gathering the undo steps of `session` (one at a time: false from active() when another one gathers)
    UndoGathering(DocumentSession& session, std::string text);
    /// Rolls back what was not committed
    ~UndoGathering();
    UndoGathering(const UndoGathering&) = delete;
    UndoGathering& operator=(const UndoGathering&) = delete;

    /// It gathers (no other gathering was running on the session when it started)
    bool active() const { return gathering; }
    size_t count() const { return steps.size(); }
    /// The steps as one undo step on the session's stack (nothing when none was gathered); gathering ends. True if a
    /// step was pushed.
    bool commit();
    /// The steps undone, last first; gathering ends. The pages they touched are told (their revisions change).
    void rollback();

private:
    void stop();

    DocumentSession& session;
    std::string text;
    std::vector<UndoActionPtr> steps;
    bool gathering = false;
};

}  // namespace xqt
