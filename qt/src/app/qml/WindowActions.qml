// xournal-qt: what the window's parts ask the window to do (Main.qml's part, `win.actions`): the searches of selected
// text and the look-ups of papers and web addresses, pages as files and templates, and the dialogs `app` asks for.
// The menus and sheets call these instead of naming the window's dialogs and overlays.
import QtQuick

Item {
    id: actions
    visible: false

    // --- pages as files and templates (the page menus, the page grid, the template picker) ------------------------------
    /// "Save page as template…" for page index `page` (qt/docs/features/templates.md)
    function openTemplateSave(page) { templateSaveDialog.openForPage(page) }
    /// Pages as files (PageFiles.qml, qt/docs/features/page-files.md): "insert" (from a file, after page `pages[0]`),
    /// "extract", "split", "images" (the pages, or the selection)
    function openPageFiles(what, pages) {
        if (what === "insert") pageFiles.chooseFile(pages.length > 0 ? pages[pages.length - 1] : app.pageNumber - 1, true)
        else if (what === "extract") pageFiles.openExtract(pages)
        else if (what === "split") pageFiles.openSplit(pages)
        else if (what === "images") pageFiles.openImages(pages)
    }

    // --- the look-up menu (qt/docs/features/citations.md) -------------------------------------------------------------
    /// A web address chosen in the look-up menu: asked first with the whole address, unless that was turned off (the
    /// menu showed it)
    function openWebAddress(url, purpose) {
        if (url === "") return
        if ((app.settings.revision, app.settings.get("webConfirm"))) {
            webConfirm.ask(url, purpose)
            return
        }
        if (app.citations.openWeb(url)) snackbar.show(qsTr("Opened %1 in the browser").arg(app.citations.hostOf(url)), false)
    }
    /// The searches of selected text: the document's search bar with the text, run (the bar follows a search set from
    /// elsewhere); the open tabs' search in their overview; the library's search on the home screen, in the library
    /// shown.
    function searchInDocument(text) {
        if (text === "") return
        pageGrid.close()
        tabOverview.close()
        searchBar.openBar()
        app.searchQuery = text
    }
    function searchOpenTabs(text) {
        if (text === "") return
        pageGrid.close()
        app.homeVisible = false
        tabOverview.searchFor(text)
    }
    function searchLibraryFor(text) {
        if (text === "") return
        tabOverview.close()
        pageGrid.close()
        app.homeVisible = true
        Qt.callLater(function() { homeView.searchFor(text) })  // (after the home screen is shown, as searchLibrary())
    }
    /// The library's search field (its keys, the tab overview's button)
    function searchLibrary() {
        tabOverview.close()
        pageGrid.close()
        app.homeVisible = true
        Qt.callLater(homeView.focusSearch)  // (after the home screen is shown: it puts the focus on its grid)
    }
    /// "Find this paper": the library searched for the title of a bibliography entry
    function findPaper(text) { findPaperSheet.openFor(text) }
    /// arXiv: a search by title, or one paper by its ID
    function arxivSearch(title) { arxivSheet.openSearch(title) }
    function arxivPaper(id) { arxivSheet.openId(id) }

    // --- the dialogs `app` asks for -------------------------------------------------------------------------------------
    Connections {
        target: app
        function onWebImageRequested(url, host, access) { webImageConfirm.ask(url, host, access) }
        function onChapterRequested(page) { chapterDialog.openFor(page) }
        function onPrintRequested(pages) { printDialog.openFor(pages) }
        function onPageSizeRequested(pages) { pageSizeDialog.openFor(pages) }
        function onNoteSpaceRequested(pages, allPages) { noteSpaceDialog.openFor(pages, allPages) }
        function onPageBackgroundRequested(pages) { backgroundDialog.openFor(pages) }
        function onInsertPagesRequested(position) { insertPagesDialog.openAt(position) }
    }
}
