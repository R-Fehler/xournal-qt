// The library's Tags view (home screen, qt/docs/tags.md): the tags of the library's documents (#tags typed in them,
// keywords of PDFs) with how many documents have each; nested tags (#course/math) folded under their parent. A tap on a
// tag shows the library's documents with it (the library's tag filter: with the Show filter, the Favourites chip and
// the folder). It follows the library's "Show" filter and its Favourites chip, and with "Only in …" its current
// folder; the list comes from the library's index (no document is opened to make it).
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

FocusScope {
    id: view
    objectName: "tagsView"
    /// Shown now: the list follows the index while it is
    property bool shown: false
    readonly property var tags: app.libraryTags
    /// Room below the last tag (the home screen's floating "+" on a phone)
    property real bottomSpace: 0
    /// A phone class: taller rows
    readonly property bool phone: typeof win !== "undefined" && win && win.adaptive
                                  ? ["phonePortrait", "phoneShort", "tiny"].indexOf(win.adaptive.layoutClass) >= 0 : false
    readonly property int rowHeight: phone ? 52 : 40
    /// A tag was chosen: the library shows its documents
    signal tagChosen(string tag)

    Binding { target: view.tags; property: "active"; value: view.shown }

    function choose(tag) {
        // (counted in the whole library: its documents are shown from the library's top)
        if (!view.tags.folderOnly)
            app.library.folder = ""
        app.library.tagFilter = tag
        view.tagChosen(tag)
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 6

        // --- a text, this folder, unfold all ---
        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            TextField {
                id: queryField
                objectName: "tagsQueryField"
                Layout.preferredWidth: view.phone ? 170 : 240
                Layout.fillWidth: true
                Layout.maximumWidth: 360
                placeholderText: qsTr("Filter tags")
                text: view.tags.query
                selectByMouse: true
                onTextEdited: view.tags.query = text
                Keys.onEscapePressed: { text = ""; view.tags.query = "" }
            }
            ToolButton {
                id: folderOnly
                objectName: "tagsFolderOnly"
                enabled: app.library.folder !== ""
                checkable: true
                checked: view.tags.folderOnly
                onToggled: view.tags.folderOnly = checked
                text: app.library.folder !== "" ? qsTr("Only in %1").arg(app.library.folder.split("/").pop())
                                                : qsTr("Only in this folder")
                font.pixelSize: 13
                implicitHeight: view.phone ? 44 : 36
                background: Rectangle {
                    radius: height / 2
                    color: folderOnly.checked ? "#d2e3fc" : (folderOnly.pressed ? "#d5d8dc" : "#e8eaed")
                }
            }
            Item { Layout.fillWidth: true }
            ToolButton {
                id: unfoldAll
                objectName: "tagsUnfoldAll"
                property bool unfolded: false
                text: unfolded ? qsTr("Fold all") : qsTr("Unfold all")
                font.pixelSize: 13
                implicitHeight: view.phone ? 44 : 36
                onClicked: {
                    unfolded = !unfolded
                    view.tags.expandAll(unfolded)
                }
                background: Rectangle {
                    radius: height / 2
                    color: unfoldAll.pressed ? "#d5d8dc" : "#e8eaed"
                }
            }
        }

        ListView {
            id: list
            objectName: "tagsList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            focus: true
            model: view.tags
            bottomMargin: view.bottomSpace
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            TouchpadMomentum { flickable: list }
            delegate: ItemDelegate {
                id: row
                required property int index
                required property string tag
                required property string name
                required property int depth
                required property int count
                required property bool children
                required property bool expanded
                objectName: "tagRow"
                width: list.width
                height: view.rowHeight
                Accessible.name: "#" + tag
                onClicked: view.choose(tag)
                contentItem: RowLayout {
                    spacing: 4
                    Item { Layout.preferredWidth: row.depth * 24 }
                    // Fold / unfold the tags inside it
                    ToolButton {
                        objectName: "tagFold"
                        visible: row.children
                        Layout.preferredWidth: view.phone ? 44 : 32
                        Layout.preferredHeight: view.phone ? 44 : 32
                        icon.source: app.iconUrl(row.expanded ? "xqt-chevron-down" : "xqt-chevron-right")
                        icon.width: 18
                        icon.height: 18
                        Accessible.name: row.expanded ? qsTr("Fold") : qsTr("Unfold")
                        onClicked: view.tags.setExpanded(row.tag, !row.expanded)
                    }
                    Item { visible: !row.children; Layout.preferredWidth: view.phone ? 44 : 32 }
                    Label {
                        objectName: "tagName"
                        text: "#" + (row.depth > 0 ? row.name : row.tag)
                        font.pixelSize: 15
                        color: "#1a73e8"
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                    Label {
                        objectName: "tagCount"
                        text: row.count === 1 ? qsTr("1 document") : qsTr("%1 documents").arg(row.count)
                        font.pixelSize: 12
                        color: "#80868b"
                    }
                }
            }
        }
    }

    ColumnLayout {
        anchors.centerIn: parent
        visible: list.count === 0
        spacing: 10
        width: Math.min(parent.width - 40, 460)
        Image {
            Layout.alignment: Qt.AlignHCenter
            source: app.iconUrl("xqt-tag")
            sourceSize.width: 56; sourceSize.height: 56
            opacity: 0.5
        }
        Label {
            objectName: "tagsEmpty"
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            font.pixelSize: 16
            color: "#5f6368"
            text: view.tags.query !== "" || app.library.favouritesOnly || view.tags.folderOnly ? qsTr("No tags match")
                                                                                              : qsTr("No tags yet")
        }
        Label {
            visible: view.tags.query === "" && !app.library.favouritesOnly
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            color: "#80868b"
            text: qsTr("Type #tag in a document's text, a Markdown box or a sticky note (#course/math for a tag inside "
                       + "another), or give a PDF tags with \"Tags…\" in its menu. Keywords of PDFs are tags too.")
        }
    }
}
