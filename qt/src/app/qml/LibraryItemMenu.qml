// xournal-qt: the menu of a card of the library or of Recent (or of the selection it is part of): open, as
// reference, favourite, tags, rename, copy, move, share, trash. It acts on home.menu*.
// Part of HomeView.qml (the home screen, qt/docs/library.md), instantiated once there: it reads the home
// screen's state through `home`, and the other parts by their ids (HomeView.qml's context).
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Dialogs
import QtQuick.Layouts
import "Popups.js" as Popups

AdaptiveMenu {
    id: itemMenu
    objectName: "homeItemMenu"
    title: home.menuMany ? home.countText(home.menuPaths.length) : home.menuName  // (at the top of the sheet on phones)
    AdaptiveMenuItem {
        text: home.menuMany ? qsTr("Open %1").arg(home.countText(home.menuPaths.length))
                            : home.menuKind === "library" ? qsTr("Open library")
                            : home.menuFolder ? qsTr("Open folder") : qsTr("Open")
        onTriggered: home.openAll(home.menuPaths, home.menuModel)
    }
    AdaptiveMenuItem {
        objectName: "openAsReferenceItem"
        text: qsTr("Open as reference")
        // Beside the document open now (without one it is simply opened)
        offered: !home.menuMany && !home.menuFolder && app.tabs.count > 0
                 && ["notes", "pdf", "md", "image", "text"].indexOf(home.menuKind) >= 0
        onTriggered: app.openAsReference(home.menuPath)
    }
    AdaptiveMenuItem {
        id: favouriteItemRef
        objectName: "favouriteItem"
        // (asked when the menu opens: the star is kept beside the document)
        property bool starred: false
        text: starred ? qsTr("Remove from favourites") : qsTr("Add to favourites")
        icon.source: app.iconUrl(starred ? "xqt-star-filled" : "xqt-star")
        icon.color: "transparent"
        offered: !home.menuMany && !home.menuFolder && ["notes", "pdf", "md", "image", "text"].indexOf(home.menuKind) >= 0
        onTriggered: app.setFavouriteFile(home.menuPath, !starred)
        Connections {
            target: itemMenu
            function onAboutToShow() { favouriteItemRef.starred = app.isFavouriteFile(home.menuPath) }
        }
    }
    // Its tags: a PDF's keywords, written into the file; the #tags typed in it (qt/docs/tags.md)
    AdaptiveMenuItem {
        objectName: "documentTagsItem"
        text: qsTr("Tags…")
        icon.source: app.iconUrl("xqt-tag")
        offered: !home.menuMany && !home.menuFolder && ["notes", "pdf", "md"].indexOf(home.menuKind) >= 0
        onTriggered: homeTagsDialog.openFor(home.menuPath)
    }
    AdaptiveMenuItem {
        objectName: "openAsLibraryItem"
        text: app.libraryWindows ? qsTr("Open as library (new window)") : qsTr("Open as library")
        offered: !home.menuMany && home.menuFolder && home.menuModel === app.library
        onTriggered: app.openLibraryAt(home.menuPath)
    }
    AdaptiveMenuItem {
        objectName: "selectItem"
        text: qsTr("Select")
        offered: !home.menuMany && home.menuModel && home.menuModel.selectionCount === 0 && home.menuKind !== "library"
        onTriggered: home.menuModel.toggleSelected(home.menuRow)
    }
    AdaptiveMenuItem {
        objectName: "renameItem"
        text: qsTr("Rename…")
        offered: !home.menuMany && home.menuKind !== "library"
        onTriggered: libraryDialogs.renameDialog.open()
    }
    AdaptiveMenuItem {
        objectName: "copyToItem"
        text: qsTr("Copy to…")
        enabled: app.library.available
        offered: home.menuKind !== "library"
        onTriggered: home.askTransfer(home.menuPaths, true)
    }
    AdaptiveMenuItem {
        objectName: "moveToItem"
        text: qsTr("Move to…")
        enabled: app.library.available
        offered: home.menuKind !== "library"
        onTriggered: home.askTransfer(home.menuPaths, false)
    }
    AdaptiveMenuItem {
        text: qsTr("Show in its folder")
        offered: !home.menuMany && home.menuModel === app.library && (home.searching || app.library.flat) && !home.menuFolder
        onTriggered: {
            searchGroup.searchField.text = ""
            app.library.searchQuery = ""
            app.library.flat = false
            app.library.folder = app.library.relativeFolder(home.menuPath.substring(0, home.menuPath.lastIndexOf("/")))
        }
    }
    AdaptiveMenuItem {
        objectName: "openWithSystemAppItem"
        text: qsTr("Open externally")
        // Markdown, text and other files, images: in the app the system has for them (not notes and PDFs)
        readonly property string file: !home.menuMany && ["md", "image", "text", "other"].indexOf(home.menuKind) >= 0
                                       ? app.externalFileOf(home.menuPath) : ""
        offered: file !== ""
        onTriggered: app.openWithSystemApp(file)
    }
    AdaptiveMenuItem {
        objectName: "copyLinkItem"
        text: qsTr("Copy link")
        // A link to the document, to paste into notes (qt/docs/links.md)
        offered: !home.menuMany && !home.menuFolder && home.menuKind !== "library" && home.menuKind !== "other"
        onTriggered: app.copyDocumentLink(home.menuPath)
    }
    AdaptiveMenuItem {
        objectName: "shareFolderItem"
        text: qsTr("Share folder…")
        offered: !home.menuMany && home.menuFolder && home.menuModel === app.library
        enabled: !app.libraryShare.running
        onTriggered: libraryDialogs.shareZip.openFor(app.library.relativeFolder(home.menuPath))
    }
    AdaptiveMenuItem {
        objectName: "shareCardItem"
        text: qsTr("Share…")
        offered: !home.menuMany && !home.menuFolder && ["pdf", "notes", "md", "text"].indexOf(home.menuKind) >= 0
        onTriggered: home.shareRequested(home.menuPath)
    }
    AdaptiveMenuItem {
        objectName: "showInFileManagerItem"
        text: qsTr("Show in file manager")
        offered: !home.menuMany && app.canShowInFileManager
        onTriggered: app.showInFileManager(home.menuPath)
    }
    MenuSeparator {}
    AdaptiveMenuItem {
        text: qsTr("Remove from this list")
        offered: home.menuModel === app.recent
        onTriggered: app.recent.removePaths(home.menuPaths)
    }
    AdaptiveMenuItem {
        objectName: "trashItem"
        text: qsTr("Move to trash…")
        offered: home.menuKind !== "library"  // (a library is never trashed from the Recent grid)
        onTriggered: home.askTrash(home.menuModel, home.menuPaths)
    }
}
