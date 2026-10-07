// xournal-qt: part of the main window (Main.qml): ⋮ and its menu. Main places it (its parent and y: the end of the
// top bar, of a text document's format bar, or of the phone's app bar).
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import "Popups.js" as Popups

// ⋮: pinned at the very end of the top bar (a text document: of its format bar; the phone chrome: of the app bar)
Row {
    id: toolEnd
    objectName: "toolEnd"
    spacing: 2
    /// An entry of ⋮ that does what a button of the bars does (`slot`: its name; qt/top-bar: ⋮ is complete)
    component CommandItem: AdaptiveMenuItem {
        property string slot
        readonly property Item button: toolArea.slots[slot] || null
        objectName: "moreCmd_" + slot
        offered: button !== null && button.offered !== false
        text: button ? (button.label !== "" ? button.label : button.name) : ""
        icon.source: button && button.iconName !== "" ? app.iconUrl(button.iconName) : ""
        checkable: ["hand", "select", "snip", "pdfText", "geometry", "touchDrawing", "write"].indexOf(slot) >= 0
        checked: button !== null && button.checked === true
        onTriggered: {
            const b = button
            // (a checkable entry toggles itself: it follows its button again)
            checked = Qt.binding(function() { return b !== null && b.checked === true })
            toolboxMenus.afterMenus(function() { b.clicked() })
        }
    }
    IconButton {
        objectName: "moreButton"
        iconName: "xqt-more"
        label: qsTr("More")
        tip: qsTr("More")
        onClicked: Popups.openAt(moreMenu)
        // The ⋮ menu (qt/docs/adaptive-layout.md, "Menus"): complete since qt/top-bar - every command, whether
        // a bar shows it or not (the bars are the user's to arrange; ⋮ is not), the commands of the bars in
        // its submenus; a sheet with drill-in on phones.
        AdaptiveMenu {
            id: moreMenu
            objectName: "moreMenu"
            AdaptiveMenuItem { objectName: "saveAsItem"; offered: !win.textDoc; text: qsTr("Save as…"); icon.source: app.iconUrl("xopp-document-save"); onTriggered: openSaveDialog(null) }
            AdaptiveMenuItem { objectName: "shareItem"; text: qsTr("Share…"); icon.source: app.iconUrl("xqt-share"); onTriggered: shareFlow.shareDialog.openFor("") }
            AdaptiveMenuItem { objectName: "printItem"; text: qsTr("Print…") + win.keyNote("print"); icon.source: app.iconUrl("xopp-document-print"); onTriggered: printDialog.open() }
            // Find and replace: where text can be written (the search itself: View → Search, and the bars)
            AdaptiveMenuItem { objectName: "replaceItem"; offered: app.canReplace && !win.reading; text: qsTr("Find and replace") + win.keyNote("replace"); icon.source: app.iconUrl("xqt-replace"); onTriggered: searchBar.openReplace() }
            MenuSeparator {}
            // The document as a file: new, open, save, its name, other ways of editing it, links, its bookmark
            // and star
            AdaptiveMenu {
                objectName: "moreDocumentMenu"
                title: qsTr("Document")
                iconName: "xqt-file-text"
                CommandItem { slot: "new" }
                CommandItem { slot: "open" }
                CommandItem { slot: "save" }
                CommandItem { slot: "editAsNotes" }
                CommandItem { slot: "openExternally" }
                AdaptiveMenuItem {
                    objectName: "bookmarkPageItem"
                    readonly property bool marked: (app.bookmarks, app.isBookmarked(app.pageNumber - 1))
                    offered: app.canBookmark
                    text: marked ? qsTr("Remove the bookmark of this page") : qsTr("Bookmark this page")
                    icon.source: app.iconUrl(marked ? "xqt-bookmark-filled" : "xqt-bookmark")
                    icon.color: "transparent"
                    onTriggered: app.toggleBookmark(app.pageNumber - 1)
                }
                // A favourite: a star kept beside the file, never in it (qt/docs/bookmarks.md)
                AdaptiveMenuItem {
                    objectName: "favouriteDocumentItem"
                    offered: app.canFavourite
                    text: app.favourite ? qsTr("Remove from favourites") : qsTr("Add to favourites")
                    icon.source: app.iconUrl(app.favourite ? "xqt-star-filled" : "xqt-star")
                    icon.color: "transparent"
                    onTriggered: app.favourite = !app.favourite
                }
                MenuSeparator {}
                // Quick note (qt/docs/quick-note.md): a new note in the library's Inbox, or a line in today's
                // Markdown note there (Settings → Documents). Here, not at the top of ⋮ (at most 10 entries
                // there): a new document, as the shortcut sheet's group "Document" has it
                AdaptiveMenuItem {
                    objectName: "documentQuickNoteItem"
                    readonly property var keys: win.keysOf("quickNote")
                    text: keys.length > 0 ? qsTr("Quick note (%1)").arg(keys[0]) : qsTr("Quick note")
                    icon.source: app.iconUrl("xqt-zap")
                    onTriggered: app.quickNote()
                }
                MenuSeparator {}
                // Its name (qt/rename): the file, and what belongs to it, as the library renames it
                AdaptiveMenuItem { objectName: "renameDocumentItem"; text: qsTr("Rename…"); icon.source: app.iconUrl("xqt-pencil"); onTriggered: renameDocumentDialog.openFor(app.currentTab) }
                // A password to open it (qt/docs/hybrid-pdf.md, "Encrypted PDFs"): AES-256, its PDF only
                AdaptiveMenuItem { objectName: "protectDocumentItem"; offered: app.canProtect && !app.protectedDocument; text: qsTr("Protect with a password…"); icon.source: app.iconUrl("xqt-lock"); onTriggered: protectionDialogs.protectDialog.openFor(false) }
                AdaptiveMenuItem { objectName: "changePasswordItem"; offered: app.canProtect && app.protectedDocument; text: qsTr("Change or remove the password…"); icon.source: app.iconUrl("xqt-lock-open"); onTriggered: protectionDialogs.protectDialog.openFor(true) }
                // Version history (qt/docs/hybrid-pdf.md): the sidebar's History panel (off by default; what it
                // is and the switch are there)
                AdaptiveMenuItem { objectName: "versionHistoryItem"; offered: !win.textDoc; text: qsTr("Version history…"); icon.source: app.iconUrl("xqt-history"); onTriggered: win.showHistory() }
                AdaptiveMenuItem {
                    objectName: "saveWithMessageItem"
                    offered: !win.textDoc
                    readonly property var keys: win.keysOf("saveWithMessage")
                    text: keys.length > 0 ? qsTr("Save with a message… (%1)").arg(keys[0]) : qsTr("Save with a message…")
                    icon.source: app.iconUrl("xqt-flag")
                    onTriggered: versionMessageDialog.openFor(-1)
                }
                AdaptiveMenuItem {
                    objectName: "editAnywayItem"
                    offered: app.canEditAnyway
                    text: qsTr("Edit anyway (as plain text)…")
                    icon.source: app.iconUrl("xqt-file-pen")
                    onTriggered: app.editAnyway(false)
                }
                // Text documents as PDF (qt/docs/md-pdf.md): a .md as a new PDF text document
                AdaptiveMenuItem {
                    objectName: "openAsPdfDocumentItem"
                    offered: app.textDocument === "markdown"
                    text: qsTr("Open as PDF document")
                    icon.source: app.iconUrl("xopp-document-export-pdf")
                    onTriggered: app.openAsPdfDocument()
                }
                AdaptiveMenuItem {
                    objectName: "unusedImagesItem"
                    offered: app.textDocument === "markdown"
                    text: qsTr("Remove unused images…")
                    icon.source: app.iconUrl("xqt-image-off")
                    onTriggered: unusedImagesDialog.show()
                }
                // Its tags: a PDF's keywords, without typing into it (qt/docs/tags.md)
                AdaptiveMenuItem {
                    objectName: "documentTagsMenuItem"
                    text: qsTr("Tags…")
                    icon.source: app.iconUrl("xqt-tag")
                    onTriggered: documentTagsDialog.openFor(app.currentDocumentPath())
                }
                // Which handwriting models read this document (qt/docs/handwriting-search.md): found from its
                // first lines, or chosen; kept in the library's cache
                AdaptiveMenu {
                    id: handwritingLanguageMenu
                    objectName: "handwritingLanguageMenu"
                    title: qsTr("Handwriting language")
                    iconName: "xqt-pencil"
                    offered: app.handwriting.enabled && !win.textDoc
                    readonly property string current: (app.currentTab, app.handwritingLanguage)
                    AdaptiveMenuItem { objectName: "handwritingLanguageAuto"; checkable: true; checked: handwritingLanguageMenu.current === "auto"; text: qsTr("Automatic"); onTriggered: app.handwritingLanguage = "auto" }
                    AdaptiveMenuItem { objectName: "handwritingLanguageEn"; checkable: true; checked: handwritingLanguageMenu.current === "en"; text: qsTr("English"); onTriggered: app.handwritingLanguage = "en" }
                    AdaptiveMenuItem { objectName: "handwritingLanguageDe"; checkable: true; checked: handwritingLanguageMenu.current === "de"; text: qsTr("German"); onTriggered: app.handwritingLanguage = "de" }
                    AdaptiveMenuItem { objectName: "handwritingLanguageBoth"; checkable: true; checked: handwritingLanguageMenu.current === "both"; text: qsTr("Both"); onTriggered: app.handwritingLanguage = "both" }
                }
                // Marks other apps put into the PDF made editable (qt/docs/adopt-annotations.md)
                AdaptiveMenuItem {
                    objectName: "adoptAnnotationsItem"
                    offered: !win.textDoc
                    enabled: !app.adopting
                    text: app.adoptableCount > 0 ? qsTr("Adopt annotations from other apps (%1)…").arg(app.adoptableCount)
                                                 : qsTr("Adopt annotations from other apps…")
                    icon.source: app.iconUrl("xopp-tool-highlighter")
                    onTriggered: app.adoptableCount > 0 ? documentNotices.adoptDialog.openFor(app.adoptableCount, app.adoptableApp, false)
                                                        : app.adoptAnnotations()
                }
                AdaptiveMenuItem { objectName: "linkedFromItem"; text: qsTr("Linked from…"); icon.source: app.iconUrl("xqt-link"); onTriggered: documentNotices.backlinksDialog.show() }
                AdaptiveMenuItem { objectName: "copyPageLinkItem"; text: qsTr("Copy link to this page"); icon.source: app.iconUrl("xqt-copy"); onTriggered: app.copyPageLink(-1) }
            }
            AdaptiveMenu {
                objectName: "moreExportMenu"
                title: qsTr("Export")
                iconName: "xqt-file-output"
                // A plain PDF: the notes drawn into the pages (a PDF with notes that stays editable is a type
                // of Save as)
                AdaptiveMenuItem { objectName: "exportPdfItem"; text: qsTr("Export as plain PDF…"); icon.source: app.iconUrl("xopp-document-export-pdf"); onTriggered: exportFlow.openExportDialog() }
                // Pages as PNG or JPEG pictures (qt/docs/page-files.md)
                AdaptiveMenuItem { objectName: "exportImagesItem"; offered: !win.textDoc; text: qsTr("Export pages as pictures…"); icon.source: app.iconUrl("xqt-file-image"); onTriggered: pageFiles.openImages(app.pages.selectionCount > 0 ? app.pages.selectedPages() : []) }
                // A PDF/A for keeping: the ink merged into the pages, the Xournal data inside
                AdaptiveMenuItem {
                    objectName: "exportArchiveItem"
                    offered: !win.textDoc
                    text: qsTr("Export for the archive…")
                    icon.source: app.iconUrl("xqt-archive")
                    onTriggered: shareFlow.archiveDialog.openFor("")
                }
                // The Markdown of the document's page texts as a .md (qt/docs/md-pdf.md)
                AdaptiveMenuItem {
                    objectName: "exportMarkdownItem"
                    offered: !win.textDoc && app.hasMarkdownText
                    text: qsTr("Export as Markdown")
                    icon.source: app.iconUrl("xqt-markdown")
                    onTriggered: exportFlow.exportMarkdown()
                }
            }
            // The pages (a text file has none to add; an image and a sticky note are buttons of the tool bar)
            AdaptiveMenu {
                objectName: "morePageMenu"
                title: qsTr("Page")
                iconName: "xqt-file"
                offered: !win.textDoc
                AdaptiveMenuItem { objectName: "insertPagesItem"; text: qsTr("Insert pages…"); icon.source: app.iconUrl("xopp-page-add"); onTriggered: insertPagesDialog.openAt(app.pageNumber) }
                // Pages as files (qt/docs/page-files.md): from a file, into a new document, split
                AdaptiveMenuItem { objectName: "insertFromFileItem"; offered: app.canInsertTemplate; text: qsTr("Insert pages from a file…"); icon.source: app.iconUrl("xqt-import"); onTriggered: pageFiles.chooseFile(app.pageNumber - 1, true) }
                AdaptiveMenuItem { objectName: "copyPageImageItem"; text: qsTr("Copy page as image"); icon.source: app.iconUrl("xqt-copy"); onTriggered: app.copyPagesAsImage(app.pages.selectionCount > 0 ? app.pages.selectedPages() : []) }
                AdaptiveMenuItem { objectName: "extractPagesItem"; text: qsTr("Extract to a new document…"); icon.source: app.iconUrl("xqt-file-output"); onTriggered: pageFiles.openExtract(app.pages.selectionCount > 0 ? app.pages.selectedPages() : []) }
                AdaptiveMenuItem { objectName: "splitDocumentItem"; text: qsTr("Split the document…"); icon.source: app.iconUrl("xqt-page-break"); onTriggered: pageFiles.openSplit(app.pages.selectionCount > 0 ? app.pages.selectedPages() : []) }
                // Page templates (qt/docs/templates.md): this page saved to be added again; one added
                AdaptiveMenuItem { objectName: "saveTemplateItem"; text: qsTr("Save page as template…"); icon.source: app.iconUrl("xqt-file-plus"); onTriggered: templateSaveDialog.openForPage(app.pageNumber - 1) }
                AdaptiveMenuItem { objectName: "insertTemplateItem"; offered: app.canInsertTemplate; text: qsTr("Add a page from a template…"); icon.source: app.iconUrl("xopp-page-add"); onTriggered: templatePicker.openToInsert(app.pageNumber) }
                AdaptiveMenuItem { objectName: "pageBackgroundItem"; text: qsTr("Background of this page…"); icon.source: app.iconUrl("xqt-palette"); onTriggered: backgroundDialog.openFor([app.pageNumber - 1]) }
                // Another paper size for this page, the selected pages or all of them (PageSizeDialog)
                AdaptiveMenuItem { objectName: "pageSizeItem"; text: qsTr("Page size…"); icon.source: app.iconUrl("xqt-scaling"); onTriggered: pageSizeDialog.openFor([app.pageNumber - 1]) }
                // Writing space beside the slides of all pages (qt/docs/note-space.md)
                AdaptiveMenuItem { objectName: "noteSpaceItem"; text: qsTr("Space for notes…"); icon.source: app.iconUrl("xqt-note-space"); onTriggered: noteSpaceDialog.openFor([app.pageNumber - 1], true) }
                AdaptiveMenuItem { objectName: "chapterItem"; text: qsTr("Start a chapter here…"); icon.source: app.iconUrl("xqt-toc"); onTriggered: chapterDialog.openFor(app.pageNumber - 1) }
                // A quarter turn of this page or of all pages (qt/docs/page-rotation.md); PDF pages of a .xopp
                // stay, and the menu says why
                AdaptiveMenu {
                    id: rotateMenu
                    objectName: "rotatePagesMenu"
                    title: qsTr("Rotate")
                    iconName: "xqt-rotate-right"
                    readonly property var thisPage: (app.pageNumber, app.pageCount, app.currentTab, app.modified, app.rotationOf([app.pageNumber - 1]))
                    readonly property var everyPage: (app.pageNumber, app.pageCount, app.currentTab, app.modified, app.rotationOf(app.allPages()))
                    AdaptiveMenuItem { objectName: "rotatePageLeftItem"; text: qsTr("This page left"); icon.source: app.iconUrl("xqt-rotate-left"); enabled: rotateMenu.thisPage.possible === true; onTriggered: app.rotatePages([app.pageNumber - 1], false) }
                    AdaptiveMenuItem { objectName: "rotatePageRightItem"; text: qsTr("This page right"); icon.source: app.iconUrl("xqt-rotate-right"); enabled: rotateMenu.thisPage.possible === true; onTriggered: app.rotatePages([app.pageNumber - 1], true) }
                    AdaptiveMenuItem { objectName: "rotateAllLeftItem"; text: qsTr("All pages left"); icon.source: app.iconUrl("xqt-rotate-left"); enabled: rotateMenu.everyPage.possible === true; onTriggered: app.rotatePages(app.allPages(), false) }
                    AdaptiveMenuItem { objectName: "rotateAllRightItem"; text: qsTr("All pages right"); icon.source: app.iconUrl("xqt-rotate-right"); enabled: rotateMenu.everyPage.possible === true; onTriggered: app.rotatePages(app.allPages(), true) }
                    AdaptiveMenuItem {
                        objectName: "rotateReasonItem"
                        offered: (rotateMenu.everyPage.reason || "") !== ""
                        enabled: false
                        text: rotateMenu.everyPage.reason || ""
                    }
                }
            }
            // The app's tools and what is put on the page (the buttons of the bars, wherever they are)
            AdaptiveMenu {
                objectName: "moreToolsMenu"
                title: qsTr("Tools")
                iconName: "xqt-tools-more"
                offered: !win.textDoc
                CommandItem { slot: "hand" }
                CommandItem { slot: "select" }
                CommandItem { slot: "snip" }
                CommandItem { slot: "pdfText" }
                CommandItem { slot: "write" }
                CommandItem { slot: "geometry" }
                CommandItem { slot: "touchDrawing" }
                MenuSeparator {}
                CommandItem { slot: "image" }
                CommandItem { slot: "sticker" }
                CommandItem { slot: "addPage" }
                CommandItem { slot: "record" }
            }
            // How the document is shown (all pages are the view pill's; hiding the top bar is the tab on its
            // edge)
            AdaptiveMenu {
                objectName: "moreViewMenu"
                title: qsTr("View")
                iconName: "xqt-eye"
                CommandItem { slot: "search" }
                CommandItem { slot: "fullScreen" }
                CommandItem { slot: "present" }
                // (the phone chrome: its tab count)
                AdaptiveMenuItem { objectName: "allDocumentsItem"; offered: !win.phoneChrome; text: qsTr("All open documents"); icon.source: app.iconUrl("xqt-tabs-grid"); onTriggered: tabOverview.open() }
                // Phones: the view pill has no room for the page layout button
                AdaptiveMenuItem {
                    objectName: "pageLayoutItem"
                    offered: !viewPill.layoutShown
                    text: qsTr("Page layout…")
                    icon.source: app.iconUrl("xqt-book-open")
                    onTriggered: toolboxMenus.openAfterMenus(viewPill.layoutMenu)
                }
                // A black sheet over part of the page, for teaching and presenting (qt/docs/curtain.md)
                AdaptiveMenuItem { objectName: "curtainItem"; offered: !win.textDoc; checkable: true; checked: app.curtain === "curtain"; text: qsTr("Curtain (B)"); icon.source: app.iconUrl("xqt-curtain"); onTriggered: app.toggleCurtain("curtain") }
                AdaptiveMenuItem { objectName: "spotlightItem"; offered: !win.textDoc; checkable: true; checked: app.curtain === "spotlight"; text: qsTr("Spotlight") + win.keyNote("spotlight"); icon.source: app.iconUrl("xqt-spotlight"); onTriggered: app.toggleCurtain("spotlight") }
                AdaptiveMenuItem { objectName: "presentCleanItem"; text: qsTr("Present without controls") + win.keyNote("presentClean"); icon.source: app.iconUrl("xopp-presentation-mode"); onTriggered: win.startPresenting(true) }
                // Zen: only the page and a faint dot (qt/docs/zen.md)
                AdaptiveMenuItem {
                    objectName: "zenItem"
                    checkable: true
                    checked: win.zenShown
                    text: win.withKeys(qsTr("Zen (only the page)"), "zen")
                    icon.source: app.iconUrl("xqt-zen")
                    onTriggered: win.setZen(!win.zenShown)
                }
                // Read only: the pen does not write, the edges turn the pages
                AdaptiveMenuItem {
                    objectName: "readOnlyItem"
                    offered: win.readOnlyOffered
                    checkable: true
                    checked: win.readOnlyOn
                    text: qsTr("Read only")
                    icon.source: app.iconUrl("xqt-lock")
                    onTriggered: win.readOnly = !win.readOnlyOn
                }
                // Read: Zen and read only, in full screen
                AdaptiveMenuItem {
                    objectName: "readItem"
                    offered: !win.textDoc
                    text: win.withKeys(qsTr("Read (Zen, read only)"), "read")
                    icon.source: app.iconUrl("xqt-book-open")
                    onTriggered: win.startReading()
                }
                // Pages shown dark, the document unchanged (qt/docs/dark-pages.md)
                AdaptiveMenu {
                    objectName: "darkPagesMenu"
                    offered: !win.textDoc
                    title: qsTr("Dark pages")
                    iconName: "xqt-moon"
                    component DarkItem: AdaptiveMenuItem {
                        property string mode
                        checkable: true
                        checked: app.darkPagesMode === mode
                        onTriggered: app.darkPagesMode = mode
                    }
                    DarkItem { objectName: "darkPagesOffItem"; text: qsTr("Off"); mode: "off" }
                    DarkItem { objectName: "darkPagesOnItem"; text: qsTr("On"); mode: "on" }
                    DarkItem { objectName: "darkPagesSystemItem"; text: qsTr("With the system's dark mode"); mode: "system" }
                }
                // The document's timeline: how it was written, with its recordings (qt/docs/timeline.md)
                AdaptiveMenuItem { objectName: "replayItem"; offered: !win.textDoc; text: qsTr("Replay the writing"); icon.source: app.iconUrl("xqt-replay"); onTriggered: app.timeline.start() }
                MenuSeparator {}
                // The toolbox's edge in this size class (qt/docs/toolbox.md); the phone classes have their dock
                AdaptiveMenu {
                    objectName: "toolboxPositionMenu"
                    offered: !win.phoneLayout
                    title: qsTr("Toolbox position")
                    iconName: "xqt-panel-top"
                    component EdgeItem: AdaptiveMenuItem {
                        property string edge
                        checkable: true
                        checked: win.toolboxEdge === edge
                        onTriggered: win.chooseToolboxEdge(edge)
                    }
                    EdgeItem { objectName: "toolboxRightItem"; text: qsTr("Right"); edge: "right" }
                    EdgeItem { objectName: "toolboxLeftItem"; text: qsTr("Left"); edge: "left" }
                    EdgeItem { objectName: "toolboxTopItem"; text: qsTr("Top"); edge: "top" }
                    EdgeItem { objectName: "toolboxBottomItem"; text: qsTr("Bottom"); edge: "bottom" }
                    MenuSeparator {}
                    AdaptiveMenuItem {
                        objectName: "toolboxAutoItem"
                        text: qsTr("Automatic for this window size")
                        checkable: true
                        checked: win.toolboxChoice === ""
                        onTriggered: win.chooseLayout("toolbox", "")
                    }
                }
            }
            // Help (qt/docs/onboarding.md): the introduction of the first start, the tutorial, the keyboard
            // shortcuts
            AdaptiveMenu {
                objectName: "moreHelpMenu"
                title: qsTr("Help")
                iconName: "xqt-help"
                AdaptiveMenuItem { objectName: "helpIntroItem"; text: qsTr("Introduction"); icon.source: app.iconUrl("xqt-book-open"); onTriggered: startupFlow.introDialog.show() }
                AdaptiveMenuItem { objectName: "helpTutorialItem"; text: qsTr("Tutorial"); icon.source: app.iconUrl("xqt-notebook-pen"); onTriggered: app.openTutorial() }
                AdaptiveMenuItem { objectName: "helpRestartTutorialItem"; offered: app.tutorialExists; text: qsTr("Start the tutorial again…"); icon.source: app.iconUrl("xopp-edit-undo"); onTriggered: startupFlow.restartTutorialDialog.open() }
                AdaptiveMenuItem { objectName: "helpShortcutsItem"; text: qsTr("Keyboard shortcuts") + win.keyNote("shortcuts"); icon.source: app.iconUrl("xqt-keyboard"); onTriggered: shortcutSheet.open() }
            }
            CommandItem { slot: "settings" }
        }
    }
}
