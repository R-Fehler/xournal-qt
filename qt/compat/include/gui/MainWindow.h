/*
 * xournal-qt: shadow of upstream gui/MainWindow.h.
 * Reused upstream code only reaches the document view through `control->getWindow()->getXournal()`.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

class XournalView;

class MainWindow {
public:
    virtual ~MainWindow() = default;
    virtual XournalView* getXournal() const = 0;
};
