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
        TabBar {
            id: sections
            Layout.fillWidth: true
            Layout.leftMargin: 12
            Layout.rightMargin: 12
            Material.background: "transparent"
            TabButton { text: qsTr("Pen"); width: implicitWidth }
            TabButton { text: qsTr("Touch"); width: implicitWidth }
            TabButton { text: qsTr("Stabilizer"); width: implicitWidth }
            TabButton { text: qsTr("Documents"); width: implicitWidth }
            TabButton { text: qsTr("New pages"); width: implicitWidth }
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
                        key: "stylusButtonTool"; text: qsTr("Barrel button")
                        options: [
                            { text: qsTr("No change (keep the tool)"), value: "none" },
                            { text: qsTr("Eraser"), value: "eraser" },
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
                        text: qsTr("Touch is ignored while the pen is near the screen (a hand resting on the screen "
                                   + "while writing is ignored until it is lifted). For pens that do not report "
                                   + "when they are near, touch is ignored for a while after the pen was used:")
                    }
                    SliderRow {
                        key: "palmRejectionTimeout"; text: qsTr("Pens without proximity: ignore touch for")
                        from: 0; to: 3000; stepSize: 100; decimals: 1; factor: 0.001; suffix: " s"
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
        }
    }
}
