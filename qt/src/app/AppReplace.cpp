/*
 * xournal-qt: the window's side of find and replace (qt/docs/md-editor.md, "Find and replace"): the replace row's
 * options, which the search takes while the row is shown, and Replace / Replace all on the current document
 * (FindReplace.h). The same in the source beside the page: replaceInSource, AppMarkdownFormat.cpp.
 *
 * @license GNU GPLv2 or later
 */
#include "AppController.h"
#include "CanvasView.h"
#include "FindReplace.h"
#include "session/DocumentSearch.h"
#include "session/DocumentSession.h"
#include "shell/LibraryModel.h"

using namespace xqt;

textmatch::Options AppController::searchOptions() const { return replaceRow ? replaceOptions : textmatch::Options{}; }

void AppController::applySearchOptions() {
    if (DocumentSession* s = session(); s && !s->search().query().isEmpty()) {
        // (the replace row: plain text with its options; without it the search is as the setting says again)
        s->search().setQuery(s->search().query(), true, !replaceRow && library->fuzzySearch(), searchOptions());
    }
    Q_EMIT searchOptionsChanged();
    Q_EMIT searchFuzzyChanged();
}

void AppController::setReplacing(bool on) {
    if (on != replaceRow) {
        replaceRow = on;
        applySearchOptions();
    }
}

void AppController::setSearchCaseSensitive(bool on) {
    if (on != replaceOptions.caseSensitive) {
        replaceOptions.caseSensitive = on;
        applySearchOptions();
    }
}

void AppController::setSearchWholeWord(bool on) {
    if (on != replaceOptions.wholeWord) {
        replaceOptions.wholeWord = on;
        applySearchOptions();
    }
}

void AppController::setSearchRegex(bool on) {
    if (on != replaceOptions.regex) {
        replaceOptions.regex = on;
        applySearchOptions();
    }
}

bool AppController::canReplace() const { return session() && replace::canReplace(*session()); }

QString AppController::replaceCurrent(const QString& with) {
    DocumentSession* s = session();
    if (!s || s->search().query().isEmpty() || !canReplace()) {
        return {};
    }
    switch (replace::replaceCurrent(*s, canvas(), s->search().query(), with, searchOptions())) {
        case replace::Step::Replaced:
            return QStringLiteral("replaced");
        case replace::Step::Skipped:
            return QStringLiteral("skipped");
        case replace::Step::Shown:
            return QStringLiteral("shown");
        case replace::Step::None:
            break;
    }
    return {};
}

int AppController::replaceAll(const QString& with) {
    DocumentSession* s = session();
    if (!s || s->search().query().isEmpty() || !canReplace()) {
        return 0;
    }
    return replace::replaceAll(*s, canvas(), s->search().query(), with, searchOptions());
}
