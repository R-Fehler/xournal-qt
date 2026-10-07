// Settings: a large modal sheet with sections (pen, touch, stabilizer, documents, display, search, new pages,
// storage, shortcuts). On a desktop or a tablet the sections are tabs; on a phone (qt/docs/features/adaptive-layout.md) the sheet
// takes the whole screen and shows the sections as a list, each opening as a page with a back arrow. Below 600 px the
// rows put their label above the slider or the box.
// The values are upstream Xournal++'s settings (settings.xml keys); they apply immediately and are saved when the
// sheet closes. "Storage" is about the cache of the library of this window (a setting of the library).
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtQuick.Dialogs

Popup {
    id: sheet
    // (open: Android's back key is its; Zen's Back waits, qt/top-bar)
    onOpenedChanged: if (typeof win !== "undefined" && win && win.takeBack !== undefined) win.takeBack(opened)
    modal: true
    focus: true
    parent: Overlay.overlay
    readonly property var win: ApplicationWindow.window
    readonly property var adaptive: win && win.adaptive ? win.adaptive : null
    /// A phone: the whole screen, the sections as a list
    readonly property bool phone: adaptive !== null && adaptive.phoneLayout
    /// The rows put their label above the control
    readonly property bool narrow: width < 600
    /// On a phone: a section is shown (else the list of them)
    property bool sectionShown: false
    /// The window's safe area and the soft keyboard (Main.qml: win.insets, win.insets.keyboardTop)
    readonly property bool inWindow: win !== null && win !== undefined && win.insets !== undefined
    readonly property real keyboardTop: {
        if (inWindow) return win.insets.keyboardOpen ? win.insets.keyboardTop : Infinity
        const r = Qt.inputMethod.keyboardRectangle
        return Qt.inputMethod.visible && r.height > 0 ? r.y / (Qt.platform.os === "android" ? Screen.devicePixelRatio : 1)
                                                      : Infinity
    }
    readonly property real safeTop: inWindow ? win.insets.top : 0
    readonly property real safeLeft: inWindow ? win.insets.left : 0
    readonly property real safeRight: inWindow ? win.insets.right : 0
    /// Above the soft keyboard while it is open, else above the navigation bar
    readonly property real roomBottom: parent ? Math.min(parent.height - (inWindow ? win.insets.bottom : 0), keyboardTop) : 700
    readonly property real parentWidth: parent ? parent.width - safeLeft - safeRight : 900
    x: safeLeft + (phone ? 0 : Math.round((parentWidth - width) / 2))
    y: phone ? safeTop : Math.round(Math.max(safeTop + 24, ((parent ? parent.height : 700) - height) / 2))
    width: phone ? parentWidth : Math.min(parentWidth - 32, 920)
    height: Math.max(0, roomBottom - safeTop - (phone ? 0 : 48))
    padding: 0
    // (on a phone in a section, Esc and the back key go back to the list first: below)
    closePolicy: phone && sectionShown ? Popup.CloseOnPressOutside : Popup.CloseOnEscape | Popup.CloseOnPressOutside
    onOpened: app.settings.begin()
    onClosed: app.settings.end()
    onAboutToShow: sectionShown = false
    onPhoneChanged: if (!phone) sectionShown = false

    /// The sections, in the order of the tabs (Shortcuts near the end: it matters little without a keyboard; Help last)
    readonly property var sectionNames: [qsTr("Pen"), qsTr("Touch"), qsTr("Stabilizer"), qsTr("Documents"),
        qsTr("Display"), qsTr("Search"), qsTr("New pages"), qsTr("Storage"), qsTr("Shortcuts"), qsTr("Help")]
    readonly property int shortcutsSection: 8
    /// Search, with the handwriting search (copying handwriting as text points there when it is off)
    readonly property int searchSection: 5
    function showSection(index) {
        sections.currentIndex = index
        sectionShown = true
    }
    function showShortcuts() { showSection(shortcutsSection) }
    function showSearch() { showSection(searchSection) }
    /// Help (qt/docs/features/onboarding.md): the window shows the introduction, the tutorial, the question whether to
    /// start the tutorial again (the sheet is closed first)
    signal introRequested()
    signal tutorialRequested()
    signal restartTutorialRequested()

    readonly property var s: app.settings
    /// The cache was removed: the window closes (so the app does not build it again at once)
    signal quitRequested()

    function sizeText(bytes) {
        if (bytes >= 1024 * 1024)
            return qsTr("%1 MB").arg((bytes / 1024 / 1024).toFixed(1))
        return qsTr("%1 kB").arg(bytes > 0 ? Math.max(1, Math.round(bytes / 1024)) : 0)
    }

    background: Rectangle { color: "#fafafa"; radius: sheet.phone ? 0 : 14 }

    FuzzyHelp { id: fuzzyHelp; objectName: "settingsFuzzyHelp" }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Header. On a phone: × (the list) or ‹ (a section: back to the list) at the left, as Android's full-screen
        // pages
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: sheet.phone ? 4 : 20
            Layout.rightMargin: 8
            Layout.topMargin: sheet.phone ? 4 : 8
            Layout.bottomMargin: sheet.phone ? 4 : 0
            IconButton {
                objectName: "settingsBackButton"
                visible: sheet.phone
                iconName: sheet.sectionShown ? "xqt-chevron-left" : "xqt-close"
                tip: sheet.sectionShown ? qsTr("Back") : qsTr("Close")
                implicitWidth: Math.max(48, sheet.adaptive ? sheet.adaptive.minTarget : 48)
                implicitHeight: implicitWidth
                onClicked: sheet.sectionShown ? sheet.sectionShown = false : sheet.close()
            }
            Label {
                objectName: "settingsTitle"
                text: sheet.phone && sheet.sectionShown ? sheet.sectionNames[sections.currentIndex] : qsTr("Settings")
                font.pixelSize: sheet.phone ? 20 : 22
                font.weight: Font.DemiBold
                elide: Text.ElideRight
                Layout.fillWidth: true
                Layout.leftMargin: sheet.phone ? 8 : 0
            }
            IconButton { visible: !sheet.phone; iconName: "xqt-close"; tip: qsTr("Close"); onClicked: sheet.close() }
            // Esc and the back key in a section of the phone's list: back to the list; else the back key closes
            Shortcut {
                sequences: [StandardKey.Cancel]
                enabled: sheet.opened && sheet.phone && sheet.sectionShown
                onActivated: sheet.sectionShown = false
            }
            Shortcut {
                sequences: ["Back"]
                enabled: sheet.opened
                onActivated: sheet.phone && sheet.sectionShown ? sheet.sectionShown = false : sheet.close()
            }
        }
        // On a phone: the sections as a list
        ListView {
            id: sectionList
            objectName: "settingsSectionList"
            visible: sheet.phone && !sheet.sectionShown
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: sheet.sectionNames
            ScrollBar.vertical: ScrollBar {}
            delegate: ItemDelegate {
                required property int index
                required property string modelData
                objectName: "settingsSection" + index
                width: ListView.view.width
                height: Math.max(56, sheet.adaptive ? sheet.adaptive.minTarget + 8 : 56)
                leftPadding: 24
                text: modelData
                font.pixelSize: 16
                onClicked: sheet.showSection(index)
                Image {
                    anchors.right: parent.right
                    anchors.rightMargin: 16
                    anchors.verticalCenter: parent.verticalCenter
                    source: app.iconUrl("xqt-chevron-right")
                    sourceSize.width: 20
                    sourceSize.height: 20
                    opacity: 0.6
                }
            }
        }
        TabBar {
            id: sections
            visible: !sheet.phone
            Layout.fillWidth: true
            Layout.leftMargin: 12
            Layout.rightMargin: 12
            Material.background: "transparent"
            TabButton { text: qsTr("Pen"); width: implicitWidth }
            TabButton { objectName: "touchTab"; text: qsTr("Touch"); width: implicitWidth }
            TabButton { text: qsTr("Stabilizer"); width: implicitWidth }
            TabButton { objectName: "documentsTab"; text: qsTr("Documents"); width: implicitWidth }
            TabButton { objectName: "displayTab"; text: qsTr("Display"); width: implicitWidth }
            TabButton { objectName: "searchTab"; text: qsTr("Search"); width: implicitWidth }
            TabButton { text: qsTr("New pages"); width: implicitWidth }
            TabButton { objectName: "storageTab"; text: qsTr("Storage"); width: implicitWidth }
            TabButton { objectName: "shortcutsTab"; text: qsTr("Shortcuts"); width: implicitWidth }
            TabButton { objectName: "helpTab"; text: qsTr("Help"); width: implicitWidth }
        }
        Hairline { Layout.fillWidth: true; color: "#e0e0e0"; visible: !sheet.phone || sheet.sectionShown }

        StackLayout {
            objectName: "settingsSections"
            visible: !sheet.phone || sheet.sectionShown
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: sections.currentIndex

            // --- Pen ---
            SettingsPen {}

            // --- Touch ---
            SettingsTouch {}

            // --- Stabilizer ---
            SettingsStabilizer {}

            // --- Documents ---
            SettingsDocuments {}

            // --- Display: the screen's calibration, so that 100 % is the size of the paper (ScreenCalibration.h) ---
            SettingsDisplay {}

            // --- Search: the fuzzy search (its toggle is the one in the search fields: app.library.fuzzySearch) ---
            SettingsSearch {}

            // --- New pages ---
            SettingsNewPages {}

            // --- Storage: the cache of this library ---
            SettingsStorage {}

            // --- Shortcuts ---
            SettingsShortcuts {}

            // --- Help: the introduction of the first start, the keyboard shortcuts (qt/docs/features/onboarding.md)
            SettingsHelp {}
        }
    }
}
