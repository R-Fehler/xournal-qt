/*
 * xournal-qt: what the windows of the process share - the settings and tools (AppContext), the color palette, the
 * toolbox, the shortcuts, the library and its lists, the recent files, the copied pages, the handwriting search, the
 * open documents of every window (OpenDocuments) and the work off the UI thread (BackgroundJobs).
 *
 * One per process, made before the first window and gone after the last one (main() holds it; an AppController made
 * without one, as the tests do, makes its own and deletes its windows before it). A window (AppController) gets it
 * at construction; the main window is the first one made on it, the windows of undocked documents come after.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <functional>
#include <memory>

#include "BackgroundJobs.h"
#include "OpenDocuments.h"

class AppController;
class Palette;

namespace xqt::hwr {
class HandwritingSearch;
}

namespace xqt {

class AppContext;
class HandwritingSettings;
class LibraryBookmarksModel;
class LibraryInkJob;
class LibraryModel;
class LibraryTagsModel;
class LibraryTodosModel;
class PageClipboard;
class RecentFiles;
class SettingsModel;
class ShortcutsModel;
class ToolboxModel;

class AppServices {
public:
    AppServices();
    /// Waits for the background jobs, then the rest goes (the windows are gone by then).
    ~AppServices();
    AppServices(const AppServices&) = delete;
    AppServices& operator=(const AppServices&) = delete;

    /// The settings, tools and rendering of the process (shared with every document)
    const std::shared_ptr<AppContext>& context() const { return app; }
    const std::shared_ptr<Palette>& colors() const { return palette; }
    SettingsModel& settingsView() const { return *settings; }
    ToolboxModel& toolbox() const { return *tools; }
    ShortcutsModel& shortcuts() const { return *keys; }
    LibraryModel& library() const { return *lib; }
    LibraryBookmarksModel& libraryBookmarks() const { return *bookmarks; }
    LibraryTagsModel& libraryTags() const { return *tags; }
    LibraryTodosModel& libraryTodos() const { return *todos; }
    RecentFiles& recent() const { return *recentFiles; }
    /// Copied pages can be pasted in any window
    PageClipboard& pageClipboard() const { return *pages; }
    hwr::HandwritingSearch& handwriting() const { return *ink; }
    /// Reads the handwriting of the rest of the library
    LibraryInkJob& libraryInk() const { return *libraryInkJob; }
    HandwritingSettings& handwritingView() const { return *inkSettings; }
    OpenDocuments& openDocuments() { return documents; }
    const OpenDocuments& openDocuments() const { return documents; }
    BackgroundJobs& jobs() { return background; }

    /// Makes the window of a controller of undocked documents (main(); nothing: no window, as in the tests)
    void setWindowFactory(std::function<void(AppController*)> factory) { windowFactory = std::move(factory); }
    void makeWindow(AppController* window) const;
    /// Windows open maximized (people make them smaller with the tiling of their desktop). Set by main().
    void setStartMaximized(bool on) { maximized = on; }
    bool startMaximized() const { return maximized; }

private:
    // (in the order they are made; they go in the opposite order)
    std::shared_ptr<AppContext> app;
    std::shared_ptr<Palette> palette;
    std::unique_ptr<PageClipboard> pages;
    std::unique_ptr<SettingsModel> settings;
    std::unique_ptr<ToolboxModel> tools;
    std::unique_ptr<ShortcutsModel> keys;
    std::unique_ptr<hwr::HandwritingSearch> ink;
    std::unique_ptr<LibraryModel> lib;
    std::unique_ptr<LibraryBookmarksModel> bookmarks;
    std::unique_ptr<LibraryTagsModel> tags;
    std::unique_ptr<LibraryTodosModel> todos;
    std::unique_ptr<LibraryInkJob> libraryInkJob;
    std::unique_ptr<HandwritingSettings> inkSettings;
    std::unique_ptr<RecentFiles> recentFiles;
    OpenDocuments documents;
    std::function<void(AppController*)> windowFactory;
    bool maximized = false;
    /// (last: destroyed first, waiting for the jobs that may still use the rest)
    BackgroundJobs background;
};

}  // namespace xqt
