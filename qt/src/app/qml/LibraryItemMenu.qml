// xournal-qt: the menu of a card of the library or of Recent (or of the selection it is part of): open, as
// reference, favourite, tags, rename, copy, move, share, trash. It acts on `target` (HomeView's menuTarget).
// Part of HomeView.qml (the home screen, qt/docs/features/library.md), instantiated once there: it reads the home
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
    /// What it acts on: a row of the library or of Recent (HomeView's menuTarget, set by showMenu)
    required property QtObject target
    title: target.many ? home.countText(target.paths.length) : target.name  // (at the top of the sheet on phones)
    AdaptiveMenuItem {
        text: target.many ? qsTr("Open %1").arg(home.countText(target.paths.length))
                            : target.kind === "library" ? qsTr("Open library")
                            : target.folder ? qsTr("Open folder") : qsTr("Open")
        onTriggered: home.openAll(target.paths, target.model)
    }
    AdaptiveMenuItem {
        objectName: "openAsReferenceItem"
        text: qsTr("Open as reference")
        // Beside the document open now (without one it is simply opened)
        offered: !target.many && !target.folder && app.tabs.count > 0
                 && ["notes", "pdf", "md", "image", "text"].indexOf(target.kind) >= 0
        onTriggered: app.openAsReference(target.path)
    }
    AdaptiveMenuItem {
        id: favouriteItemRef
        objectName: "favouriteItem"
        // (asked when the menu opens: the star is kept beside the document)
        property bool starred: false
        text: starred ? qsTr("Remove from favourites") : qsTr("Add to favourites")
        icon.source: app.iconUrl(starred ? "xqt-star-filled" : "xqt-star")
        icon.color: "transparent"
        offered: !target.many && !target.folder && ["notes", "pdf", "md", "image", "text"].indexOf(target.kind) >= 0
        onTriggered: app.setFavouriteFile(target.path, !starred)
        Connections {
            target: itemMenu
            function onAboutToShow() { favouriteItemRef.starred = app.isFavouriteFile(target.path) }
        }
    }
    // Its tags: a PDF's keywords, written into the file; the #tags typed in it (qt/docs/features/tags.md)
    AdaptiveMenuItem {
        objectName: "documentTagsItem"
        text: qsTr("Tags…")
        icon.source: app.iconUrl("xqt-tag")
        offered: !target.many && !target.folder && ["notes", "pdf", "md"].indexOf(target.kind) >= 0
        onTriggered: homeTagsDialog.openFor(target.path)
    }
    AdaptiveMenuItem {
        objectName: "openAsLibraryItem"
        text: app.libraryWindows ? qsTr("Open as library (new window)") : qsTr("Open as library")
        offered: !target.many && target.folder && target.model === app.library
        onTriggered: app.openLibraryAt(target.path)
    }
    AdaptiveMenuItem {
        objectName: "selectItem"
        text: qsTr("Select")
        offered: !target.many && target.model && target.model.selectionCount === 0 && target.kind !== "library"
        onTriggered: target.model.toggleSelected(target.row)
    }
    AdaptiveMenuItem {
        objectName: "renameItem"
        text: qsTr("Rename…")
        offered: !target.many && target.kind !== "library"
        onTriggered: libraryDialogs.renameDialog.open()
    }
    AdaptiveMenuItem {
        objectName: "copyToItem"
        text: qsTr("Copy to…")
        enabled: app.library.available
        offered: target.kind !== "library"
        onTriggered: home.askTransfer(target.paths, true)
    }
    AdaptiveMenuItem {
        objectName: "moveToItem"
        text: qsTr("Move to…")
        enabled: app.library.available
        offered: target.kind !== "library"
        onTriggered: home.askTransfer(target.paths, false)
    }
    AdaptiveMenuItem {
        text: qsTr("Show in its folder")
        offered: !target.many && target.model === app.library && (home.searching || app.library.flat) && !target.folder
        onTriggered: {
            searchGroup.searchField.text = ""
            app.library.searchQuery = ""
            app.library.flat = false
            app.library.folder = app.library.relativeFolder(target.path.substring(0, target.path.lastIndexOf("/")))
        }
    }
    AdaptiveMenuItem {
        objectName: "openWithSystemAppItem"
        text: qsTr("Open externally")
        // Markdown, text and other files, images: in the app the system has for them (not notes and PDFs)
        readonly property string file: !target.many && ["md", "image", "text", "other"].indexOf(target.kind) >= 0
                                       ? app.externalFileOf(target.path) : ""
        offered: file !== ""
        onTriggered: app.openWithSystemApp(file)
    }
    AdaptiveMenuItem {
        objectName: "copyLinkItem"
        text: qsTr("Copy link")
        // A link to the document, to paste into notes (qt/docs/features/links.md)
        offered: !target.many && !target.folder && target.kind !== "library" && target.kind !== "other"
        onTriggered: app.copyDocumentLink(target.path)
    }
    AdaptiveMenuItem {
        objectName: "shareFolderItem"
        text: qsTr("Share folder…")
        offered: !target.many && target.folder && target.model === app.library
        enabled: !app.libraryShare.running
        onTriggered: libraryDialogs.shareZip.openFor(app.library.relativeFolder(target.path))
    }
    AdaptiveMenuItem {
        objectName: "shareCardItem"
        text: qsTr("Share…")
        offered: !target.many && !target.folder && ["pdf", "notes", "md", "text"].indexOf(target.kind) >= 0
        onTriggered: home.shareRequested(target.path)
    }
    AdaptiveMenuItem {
        objectName: "showInFileManagerItem"
        text: qsTr("Show in file manager")
        offered: !target.many && app.canShowInFileManager
        onTriggered: app.showInFileManager(target.path)
    }
    MenuSeparator {}
    AdaptiveMenuItem {
        text: qsTr("Remove from this list")
        offered: target.model === app.recent
        onTriggered: app.recent.removePaths(target.paths)
    }
    AdaptiveMenuItem {
        objectName: "trashItem"
        text: qsTr("Move to trash…")
        offered: target.kind !== "library"  // (a library is never trashed from the Recent grid)
        onTriggered: home.askTrash(target.model, target.paths)
    }
}
