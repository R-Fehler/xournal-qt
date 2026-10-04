// The color chooser (qt/docs/color-palettes.md): tabs for the colors one has (the tool bar's or the pen pill's, with
// "Add a color…", the picker with the hex field) and one per color palette (ColorPalettes.h). A palette's tab shows
// only the roles it defines, each with its name; with the highlighter in hand their highlight colors, as they look on
// this page (0.5 on light paper, 0.8 on dark), else their ink. A color taken from a palette remembers its role
// (app.setPaletteColor), and its palette becomes the chosen one. In the phone classes a bottom sheet.
// The content of the first tab ("Colors") is what is put inside (its default property).
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

Popup {
    id: chooser
    /// What it opens beside, and where: "top" (below), "bottom" (above), "left", "right" (beside)
    property Item anchorItem: null
    property string side: "top"
    /// The first tab's content
    default property alias colorsContent: colorsPage.data

    readonly property var palettes: app.colorPalettes
    /// The palette whose tab is shown (null: the first tab)
    readonly property var shownPalette: tabs.currentIndex > 0 && tabs.currentIndex <= palettes.length
                                        ? palettes[tabs.currentIndex - 1] : null
    /// The highlighter is in hand: the palettes' highlight colors
    readonly property bool highlighter: app.tool === "highlighter"
    /// The current page's paper and the highlighter's opacity on it (read when it opens)
    property color paper: "#ffffff"
    property real highlightOpacity: 0.5
    readonly property bool darkPaper: highlightOpacity > 0.5

    /// In the phone classes a bottom sheet (qt/docs/adaptive-layout.md, "The phone chrome")
    readonly property bool asSheet: typeof win !== "undefined" && win !== null && win.phoneLayout
    /// Swatches per row in the first tab: six, as many as fit across a sheet
    readonly property int columns: asSheet ? Math.max(6, Math.floor((availableWidth - 0) / 48)) : 6
    /// A palette's roles per row: four, as many as fit across a sheet
    readonly property int roleColumns: asSheet ? Math.max(4, Math.floor(availableWidth / 76)) : 4

    function tabOf(paletteId) {
        for (let i = 0; i < palettes.length; ++i) if (palettes[i].id === paletteId) return i + 1
        return -1
    }
    /// Shows the tab of a palette ("" or "colors": the first)
    function showTab(paletteId) {
        tabs.currentIndex = Math.max(0, tabOf(paletteId))
    }

    onAboutToShow: {
        paper = app.paperColor()
        highlightOpacity = app.highlighterOpacity()
        // A color taken from a palette: that palette's tab; else the tab shown last
        const role = app.colorRole
        if (role !== "") showTab(role.split(":")[0])
    }

    focus: true  // (Esc closes it)
    background: Rectangle {
        radius: chooser.asSheet ? 16 : 12
        color: "#ffffff"
        border.width: chooser.asSheet ? 0 : 1
        border.color: "#d5d8dc"
    }
    modal: asSheet
    dim: asSheet
    width: asSheet && parent ? win.sheetWidth : implicitWidth
    bottomPadding: asSheet ? 10 + 8 + win.sheetBottomPadding : 10
    parent: asSheet ? Overlay.overlay : anchorItem
    // (margins: kept inside the window)
    x: asSheet ? win.sheetX
       : side === "left" ? (parent ? parent.width : 0) + 4 : side === "right" ? -width - 4 : 0
    y: asSheet ? win.sheetBottom - height
       : side === "top" ? (parent ? parent.height : 0) + 4 : side === "bottom" ? -height - 4 : 0
    margins: asSheet ? 0 : 8
    padding: 10
    topPadding: 4
    leftPadding: asSheet ? 16 : 10
    rightPadding: asSheet ? 16 : 10

    Column {
        width: chooser.asSheet ? chooser.availableWidth : 4 * 74
        spacing: 6
        Shortcut { sequence: "Back"; enabled: chooser.opened; onActivated: chooser.close() }  // (Android's back key)

        TabBar {
            id: tabs
            objectName: chooser.objectName + "Tabs"
            width: parent.width
            TabButton {
                objectName: "colorTab_colors"
                text: qsTr("Colors")
                width: implicitWidth
                font.pixelSize: 13
            }
            Repeater {
                model: chooser.palettes
                delegate: TabButton {
                    required property var modelData
                    objectName: "colorTab_" + modelData.id
                    text: modelData.name
                    width: implicitWidth
                    font.pixelSize: 13
                }
            }
        }

        Column {
            id: colorsPage
            objectName: chooser.objectName + "Colors"
            visible: tabs.currentIndex === 0
            spacing: 6
        }

        Column {
            id: palettePage
            objectName: chooser.objectName + "Roles"
            visible: chooser.shownPalette !== null
            width: parent.width
            spacing: 4
            Label {
                objectName: "paletteHint"
                width: parent.width
                wrapMode: Text.Wrap
                font.pixelSize: 12
                color: "#5f6368"
                text: {
                    const p = chooser.shownPalette
                    const made = p && p.dark !== chooser.darkPaper
                                 ? (p.dark ? qsTr(" Made for dark paper.") : qsTr(" Made for light paper.")) : ""
                    return (chooser.highlighter
                            ? qsTr("Highlight colors, at %1 % on this page.").arg(Math.round(chooser.highlightOpacity * 100))
                            : qsTr("Ink colors.")) + made
                }
            }
            Grid {
                columns: chooser.roleColumns
                Repeater {
                    model: chooser.shownPalette ? chooser.shownPalette.roles : []
                    delegate: AbstractButton {
                        id: roleCell
                        required property var modelData
                        objectName: "paletteRole_" + modelData.key
                        readonly property string paletteId: chooser.shownPalette ? chooser.shownPalette.id : ""
                        readonly property string roleName: modelData.name
                        readonly property color roleColor: chooser.highlighter ? modelData.highlight : modelData.ink
                        /// The color in hand is this role's
                        readonly property bool current: app.colorRole !== ""
                                                        ? app.colorRole === paletteId + ":" + modelData.key
                                                        : Qt.colorEqual(app.color, roleColor)
                        width: chooser.asSheet ? Math.floor(palettePage.width / chooser.roleColumns) : 74
                        height: 66
                        focusPolicy: Qt.NoFocus
                        Accessible.name: roleName
                        ToolTip.visible: hovered
                        ToolTip.text: roleName
                        ToolTip.delay: 600
                        onClicked: {
                            app.setPaletteColor(paletteId, modelData.key)
                            chooser.close()
                        }
                        contentItem: Item {
                            Rectangle {  // a piece of this page's paper with the color on it
                                anchors.horizontalCenter: parent.horizontalCenter
                                y: 4
                                width: 52
                                height: 32
                                radius: 8
                                color: chooser.paper
                                border.width: roleCell.current ? 3 : 1
                                border.color: roleCell.current ? Material.accentColor : "#c9ccd1"
                                Rectangle {  // ink: a dot; highlight: a stroke of the marker, as see-through as it is
                                    anchors.centerIn: parent
                                    width: chooser.highlighter ? 38 : 20
                                    height: chooser.highlighter ? 14 : 20
                                    radius: chooser.highlighter ? 3 : 10
                                    color: roleCell.roleColor
                                    opacity: chooser.highlighter ? chooser.highlightOpacity : 1
                                }
                            }
                            Label {
                                y: 40
                                width: parent.width
                                horizontalAlignment: Text.AlignHCenter
                                text: roleCell.roleName
                                elide: Text.ElideRight
                                font.pixelSize: 11
                                color: roleCell.current ? Material.accentColor : "#3c4043"
                            }
                        }
                    }
                }
            }
            Label {  // where its colors come from
                objectName: "paletteSource"
                visible: text !== ""
                width: parent.width
                wrapMode: Text.Wrap
                font.pixelSize: 11
                color: "#80868b"
                text: chooser.shownPalette ? chooser.shownPalette.source : ""
            }
        }
    }
}
