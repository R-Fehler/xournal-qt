// Settings: a large modal sheet with sections (pen, touch, stabilizer, documents, new pages). The values are
// upstream Xournal++'s settings (settings.xml keys); they apply immediately and are saved when the sheet closes.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Popup {
    id: sheet
    modal: true
    focus: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(parent ? parent.width - 32 : 900, 920)
    height: parent ? parent.height - 48 : 700
    padding: 0
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    onOpened: app.settings.begin()
    onClosed: app.settings.end()

    readonly property var s: app.settings

    background: Rectangle { color: "#fafafa"; radius: 14 }

    // --- rows -----------------------------------------------------------------------------------------------------
    component SectionTitle: Label {
        Layout.topMargin: 18
        Layout.fillWidth: true
        font.pixelSize: 15
        font.weight: Font.DemiBold
        color: Material.accentColor
    }
    component Hint: Label {
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        color: "#6b6f75"
        font.pixelSize: 13
    }
    component SwitchRow: RowLayout {
        id: row
        property string key
        property alias text: label.text
        Layout.fillWidth: true
        Label { id: label; Layout.fillWidth: true; wrapMode: Text.WordWrap }
        Switch {
            checked: (sheet.s.revision, sheet.s.get(row.key))
            onToggled: sheet.s.set(row.key, checked)
        }
    }
    component SliderRow: RowLayout {
        id: row
        property string key
        property alias text: label.text
        property real from: 0
        property real to: 1
        property real stepSize: 0.01
        property int decimals: 2
        property string suffix: ""
        property real factor: 1  // shown value = stored value * factor
        Layout.fillWidth: true
        Label { id: label; Layout.preferredWidth: 220; wrapMode: Text.WordWrap }
        Slider {
            id: slider
            Layout.fillWidth: true
            from: row.from; to: row.to; stepSize: row.stepSize
            snapMode: Slider.SnapAlways
            value: (sheet.s.revision, sheet.s.get(row.key))
            onMoved: sheet.s.set(row.key, value)
        }
        Label {
            Layout.preferredWidth: 72
            horizontalAlignment: Text.AlignRight
            text: (slider.value * row.factor).toFixed(row.decimals) + row.suffix
        }
    }
    component ComboRow: RowLayout {
        id: row
        property string key
        property alias text: label.text
        property var options: []    // [{ text, value }]; value may be a string or an index
        Layout.fillWidth: true
        Label { id: label; Layout.fillWidth: true; wrapMode: Text.WordWrap }
        ComboBox {
            Layout.preferredWidth: 260
            model: row.options
            textRole: "text"
            valueRole: "value"
            // count: re-evaluate once the model is there
            currentIndex: (sheet.s.revision, count, indexOfValue(sheet.s.get(row.key)))
            onActivated: sheet.s.set(row.key, currentValue)
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Header
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 8
            Layout.topMargin: 8
            Label { text: qsTr("Settings"); font.pixelSize: 22; font.weight: Font.DemiBold; Layout.fillWidth: true }
            IconButton { iconName: "xqt-close"; tip: qsTr("Close"); onClicked: sheet.close() }
        }
        function showShortcuts() { sections.currentIndex = sections.count - 1 }
        TabBar {
            id: sections
            Layout.fillWidth: true
            Layout.leftMargin: 12
            Layout.rightMargin: 12
            Material.background: "transparent"
            TabButton { text: qsTr("Pen"); width: implicitWidth }
            TabButton { objectName: "touchTab"; text: qsTr("Touch"); width: implicitWidth }
            TabButton { text: qsTr("Stabilizer"); width: implicitWidth }
            TabButton { text: qsTr("Documents"); width: implicitWidth }
            TabButton { text: qsTr("New pages"); width: implicitWidth }
            TabButton { objectName: "shortcutsTab"; text: qsTr("Shortcuts"); width: implicitWidth }
        }
        Rectangle { Layout.fillWidth: true; height: 1; color: "#e0e0e0" }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: sections.currentIndex

            // --- Pen ---
            ScrollView {
                contentWidth: availableWidth
                ColumnLayout {
                    width: parent.width - 48
                    x: 24
                    spacing: 10
                    SectionTitle { text: qsTr("Pressure") }
                    SwitchRow { key: "pressureSensitivity"; text: qsTr("Pressure changes the line width") }
                    SliderRow {
                        key: "minimumPressure"; text: qsTr("Minimum pressure")
                        from: 0.01; to: 1; stepSize: 0.01
                    }
                    SliderRow {
                        key: "pressureMultiplier"; text: qsTr("Pressure multiplier")
                        from: 0.5; to: 4; stepSize: 0.05
                    }
                    SwitchRow {
                        key: "pressureGuessing"
                        text: qsTr("Guess pressure from the speed (for pens and mice without pressure)")
                    }
                    SectionTitle { text: qsTr("Buttons and eraser") }
                    ComboRow {
                        key: "eraserButtonTool"; text: qsTr("Side button / eraser end")
                        options: [
                            { text: qsTr("Eraser"), value: "eraser" },
                            { text: qsTr("Hand (scroll)"), value: "hand" },
                            { text: qsTr("Highlighter"), value: "highlighter" },
                            { text: qsTr("Rectangle selection"), value: "selectRect" },
                            { text: qsTr("Lasso selection"), value: "selectRegion" },
                            { text: qsTr("No change (keep the tool)"), value: "none" }
                        ]
                    }
                    ComboRow {
                        key: "stylusButtonTool"; text: qsTr("Lower side button")
                        options: [
                            { text: qsTr("Eraser"), value: "eraser" },
                            { text: qsTr("No change (keep the tool)"), value: "none" },
                            { text: qsTr("Hand (scroll)"), value: "hand" },
                            { text: qsTr("Highlighter"), value: "highlighter" },
                            { text: qsTr("Rectangle selection"), value: "selectRect" },
                            { text: qsTr("Lasso selection"), value: "selectRegion" }
                        ]
                    }
                    ComboRow {
                        key: "stylusButton2Tool"; text: qsTr("Upper side button")
                        options: [
                            { text: qsTr("Eraser"), value: "eraser" },
                            { text: qsTr("No change (keep the tool)"), value: "none" },
                            { text: qsTr("Hand (scroll)"), value: "hand" },
                            { text: qsTr("Highlighter"), value: "highlighter" },
                            { text: qsTr("Rectangle selection"), value: "selectRect" },
                            { text: qsTr("Lasso selection"), value: "selectRegion" }
                        ]
                    }
                    ComboRow {
                        key: "eraserMode"; text: qsTr("Eraser")
                        options: [
                            { text: qsTr("Standard (cuts strokes)"), value: "default" },
                            { text: qsTr("Whole strokes"), value: "deleteStroke" },
                            { text: qsTr("Whiteout"), value: "whiteout" }
                        ]
                    }
                    SectionTitle { text: qsTr("Grid") }
                    SwitchRow { objectName: "snapGridSwitch"; key: "snapGrid"; text: qsTr("Snap to the grid") }
                    Hint {
                        text: qsTr("Selections that are moved and the corners of shapes jump onto the nearest point "
                                   + "of a half-centimetre grid when they come close to it (as in Xournal++). Also in "
                                   + "the shapes menu of the tool bar. Hold Alt to do the opposite for a moment.")
                    }
                    Item { Layout.preferredHeight: 16 }
                }
            }

            // --- Touch ---
            ScrollView {
                contentWidth: availableWidth
                ColumnLayout {
                    width: parent.width - 48
                    x: 24
                    spacing: 10
                    SectionTitle { text: qsTr("Palm rejection") }
                    Hint {
                        text: qsTr("Touch is ignored while the pen is near the screen, so a hand resting on it while "
                                   + "writing does nothing (it stays ignored until it is lifted). Once the pen is away, "
                                   + "touch works again after:")
                    }
                    SliderRow {
                        key: "palmRejectionTimeout"; text: qsTr("Touch waits after the pen")
                        from: 0; to: 2000; stepSize: 50; decimals: 2; factor: 0.001; suffix: " s"
                    }
                    Hint {
                        text: qsTr("For a pen that never tells when it is near, the time counts from the last moment "
                                   + "it was used.")
                    }
                    // Pens that tell how high they are: from which height on the pen counts as away
                    SliderRow {
                        objectName: "palmNearHeightRow"
                        visible: app.penHover.reportsHeight
                        key: "palmNearHeight"; text: qsTr("The pen counts as near up to")
                        from: 5; to: 100; stepSize: 1; decimals: 0; suffix: " %"
                    }
                    RowLayout {
                        visible: app.penHover.reportsHeight
                        spacing: 12
                        Label {
                            Layout.preferredWidth: 220
                            text: app.penHover.inProximity
                                  ? qsTr("Your pen is at %1 % now").arg(Math.round(app.penHover.height * 100))
                                  : qsTr("Your pen is away")
                        }
                        Button {
                            objectName: "takePenHeight"
                            text: takeHeight.running ? qsTr("Hold the pen there … %1").arg(takeHeight.left)
                                                     : qsTr("Take the pen's height")
                            enabled: !takeHeight.running
                            onClicked: { takeHeight.left = 3; takeHeight.start() }
                        }
                        // Three seconds to lift the pen to the height that should count as away
                        Timer {
                            id: takeHeight
                            property int left: 3
                            interval: 1000
                            repeat: true
                            onTriggered: {
                                if (--left > 0) return
                                stop()
                                if (app.penHover.inProximity)
                                    sheet.s.set("palmNearHeight", Math.max(5, Math.round(app.penHover.height * 100)))
                            }
                        }
                    }
                    Hint {
                        text: app.penHover.reportsHeight
                              ? qsTr("100 %: as far up as the pen is noticed at all. Lower: above that height it "
                                     + "counts as away, so a finger can scroll right after writing while the pen stays "
                                     + "close. \"Take the pen's height\": tap it, then hold the pen where it should "
                                     + "start to count as away.")
                              : qsTr("Hover the pen over this page: if it tells how high it is above the screen, you can "
                                     + "also choose here from which height on it counts as away.")
                    }
                    SectionTitle { text: qsTr("Gestures") }
                    SwitchRow { key: "zoomGestures"; text: qsTr("Pinch with two fingers to zoom") }
                    Hint {
                        text: qsTr("One finger scrolls, two fingers pan and zoom. Tap with two fingers to undo, with "
                                   + "three fingers to redo.")
                    }
                    Item { Layout.preferredHeight: 16 }
                }
            }

            // --- Stabilizer ---
            ScrollView {
                contentWidth: availableWidth
                ColumnLayout {
                    width: parent.width - 48
                    x: 24
                    spacing: 10
                    SectionTitle { text: qsTr("Stroke stabilizer") }
                    Hint { text: qsTr("Smooths strokes while writing, as in Xournal++.") }
                    ComboRow {
                        key: "stabilizerAveraging"; text: qsTr("Averaging")
                        options: [
                            { text: qsTr("None"), value: 0 },
                            { text: qsTr("Arithmetic mean"), value: 1 },
                            { text: qsTr("Velocity based Gaussian weights"), value: 2 }
                        ]
                    }
                    SliderRow {
                        visible: (sheet.s.revision, sheet.s.get("stabilizerAveraging")) !== 0
                        key: "stabilizerBuffersize"; text: qsTr("Buffer size")
                        from: 2; to: 100; stepSize: 1; decimals: 0
                    }
                    SliderRow {
                        visible: (sheet.s.revision, sheet.s.get("stabilizerAveraging")) === 2
                        key: "stabilizerSigma"; text: qsTr("Sigma")
                        from: 0.05; to: 5; stepSize: 0.05
                    }
                    ComboRow {
                        key: "stabilizerPreprocessor"; text: qsTr("Preprocessor")
                        options: [
                            { text: qsTr("None"), value: 0 },
                            { text: qsTr("Deadzone"), value: 1 },
                            { text: qsTr("Inertia"), value: 2 }
                        ]
                    }
                    SliderRow {
                        visible: (sheet.s.revision, sheet.s.get("stabilizerPreprocessor")) === 1
                        key: "stabilizerDeadzoneRadius"; text: qsTr("Deadzone radius")
                        from: 0.1; to: 50; stepSize: 0.1; decimals: 1
                    }
                    SwitchRow {
                        visible: (sheet.s.revision, sheet.s.get("stabilizerPreprocessor")) === 1
                        key: "stabilizerCuspDetection"; text: qsTr("Cusp detection")
                    }
                    SliderRow {
                        visible: (sheet.s.revision, sheet.s.get("stabilizerPreprocessor")) === 2
                        key: "stabilizerDrag"; text: qsTr("Drag")
                        from: 0; to: 1; stepSize: 0.05
                    }
                    SliderRow {
                        visible: (sheet.s.revision, sheet.s.get("stabilizerPreprocessor")) === 2
                        key: "stabilizerMass"; text: qsTr("Mass")
                        from: 1; to: 30; stepSize: 0.1; decimals: 1
                    }
                    SwitchRow {
                        key: "stabilizerFinalizeStroke"
                        text: qsTr("Finish the stroke at the pen position (no gap at the end)")
                    }
                    Item { Layout.preferredHeight: 16 }
                }
            }

            // --- Documents ---
            ScrollView {
                contentWidth: availableWidth
                ColumnLayout {
                    width: parent.width - 48
                    x: 24
                    spacing: 10
                    SectionTitle { text: qsTr("Start") }
                    SwitchRow { key: "restoreSession"; text: qsTr("Reopen the documents of the last session") }
                    SectionTitle { text: qsTr("Autosave") }
                    SwitchRow { key: "autosaveEnabled"; text: qsTr("Save a backup of unsaved changes regularly") }
                    SliderRow {
                        enabled: (sheet.s.revision, sheet.s.get("autosaveEnabled"))
                        key: "autosaveMinutes"; text: qsTr("Every")
                        from: 1; to: 30; stepSize: 1; decimals: 0; suffix: " min"
                    }
                    SectionTitle { text: qsTr("File names") }
                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: qsTr("Name for new documents"); Layout.preferredWidth: 220 }
                        TextField {
                            Layout.fillWidth: true
                            text: (sheet.s.revision, sheet.s.get("defaultSaveName"))
                            onEditingFinished: sheet.s.set("defaultSaveName", text)
                        }
                    }
                    Hint { text: qsTr("%F is the date (2026-09-19), %H-%M the time.") }
                    Item { Layout.preferredHeight: 16 }
                }
            }

            // --- New pages ---
            ScrollView {
                contentWidth: availableWidth
                ColumnLayout {
                    width: parent.width - 48
                    x: 24
                    spacing: 10
                    SectionTitle { text: qsTr("New pages") }
                    SwitchRow {
                        key: "copyLastPageSettings"
                        text: qsTr("Use the background of the current page")
                    }
                    SwitchRow {
                        key: "copyLastPageSize"
                        text: qsTr("Use the size of the current page")
                    }
                    ComboRow {
                        key: "pageBackground"; text: qsTr("Background")
                        enabled: !(sheet.s.revision, sheet.s.get("copyLastPageSettings"))
                        options: sheet.s.pageBackgrounds.map(function(name, i) { return { text: name, value: i } })
                    }
                    ComboRow {
                        key: "paperFormat"; text: qsTr("Paper size")
                        enabled: !(sheet.s.revision, sheet.s.get("copyLastPageSize"))
                        options: sheet.s.paperFormats.map(function(name, i) { return { text: name, value: i } })
                    }
                    SwitchRow {
                        key: "landscape"; text: qsTr("Landscape")
                        enabled: !(sheet.s.revision, sheet.s.get("copyLastPageSize"))
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        enabled: !(sheet.s.revision, sheet.s.get("copyLastPageSettings"))
                        Label { text: qsTr("Paper color"); Layout.fillWidth: true }
                        Repeater {
                            model: ["#ffffff", "#fdf6e3", "#f1f3f4", "#e8f0fe", "#fef7e0"]
                            delegate: AbstractButton {
                                required property string modelData
                                implicitWidth: 44
                                implicitHeight: 44
                                onClicked: sheet.s.set("pageColor", modelData)
                                contentItem: Item {
                                    Rectangle {
                                        anchors.centerIn: parent
                                        width: 30; height: 30; radius: 15
                                        color: modelData
                                        border.width: Qt.colorEqual((sheet.s.revision, sheet.s.get("pageColor")),
                                                                    modelData) ? 3 : 1
                                        border.color: border.width > 1 ? Material.accentColor : "#9e9e9e"
                                    }
                                }
                            }
                        }
                    }
                    Item { Layout.preferredHeight: 16 }
                }
            }

            // --- Shortcuts ---
            ColumnLayout {
                spacing: 0
                RowLayout {
                    Layout.fillWidth: true
                    Layout.margins: 16
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        text: qsTr("Tap a shortcut and press the keys. Backspace removes it, Esc keeps it as it is.")
                        color: "#5f6368"
                    }
                    Button {
                        objectName: "resetShortcutsButton"
                        text: qsTr("Default keys")
                        onClicked: app.shortcuts.resetAll()
                    }
                }
                ListView {
                    objectName: "shortcutList"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.leftMargin: 16
                    Layout.rightMargin: 16
                    Layout.bottomMargin: 16
                    clip: true
                    model: app.shortcuts
                    spacing: 2
                    ScrollBar.vertical: ScrollBar {}
                    section.property: "group"
                    section.delegate: Label {
                        required property string section
                        text: section
                        font.weight: Font.DemiBold
                        color: Material.accentColor
                        topPadding: 10
                        bottomPadding: 4
                    }
                    delegate: ItemDelegate {
                        id: shortcutRow
                        required property int index
                        required property string actionId
                        required property string name
                        required property string keys
                        required property bool isDefault
                        required property string conflict
                        width: ListView.view.width
                        height: 44
                        onClicked: { capture.row = shortcutRow.index; capture.actionId = shortcutRow.actionId; capture.open() }
                        contentItem: RowLayout {
                            spacing: 8
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 0
                                Label { text: shortcutRow.name; elide: Text.ElideRight; Layout.fillWidth: true }
                                Label {
                                    visible: shortcutRow.conflict !== ""
                                    text: qsTr("also used by “%1”").arg(shortcutRow.conflict)
                                    color: "#c5221f"
                                    font.pixelSize: 11
                                }
                            }
                            Rectangle {
                                radius: 5
                                color: shortcutRow.isDefault ? "#f1f3f4" : "#e0e3f5"
                                border.width: 1
                                border.color: shortcutRow.conflict !== "" ? "#c5221f" : "#d5d8dc"
                                implicitWidth: rowKeys.implicitWidth + 14
                                implicitHeight: 28
                                Label {
                                    id: rowKeys
                                    anchors.centerIn: parent
                                    text: shortcutRow.keys === "" ? qsTr("none") : shortcutRow.keys
                                    font.family: "monospace"
                                    font.pixelSize: 12
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // Catches the keys for one shortcut
    Dialog {
        id: capture
        objectName: "shortcutCapture"
        property int row: -1
        property string actionId: ""
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: 360
        title: qsTr("Press the keys")
        standardButtons: Dialog.Cancel
        onOpened: catcher.forceActiveFocus()
        ColumnLayout {
            width: capture.availableWidth
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: qsTr("Press the key combination for this action. Backspace removes the shortcut.")
            }
            Item {
                id: catcher
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                focus: true
                Keys.onPressed: function(event) {
                    event.accepted = true
                    if (event.key === Qt.Key_Escape) { capture.close(); return }
                    if (event.key === Qt.Key_Backspace) { app.shortcuts.setKeys(capture.actionId, ""); capture.close(); return }
                    if ([Qt.Key_Control, Qt.Key_Shift, Qt.Key_Alt, Qt.Key_Meta].indexOf(event.key) >= 0) return
                    const combination = event.key | (event.modifiers & ~Qt.KeypadModifier)
                    app.shortcuts.setKeys(capture.actionId, capture.textOf(combination))
                    capture.close()
                }
            }
        }
        function textOf(combination) { return app.shortcuts.keyText(combination) }
    }
}
