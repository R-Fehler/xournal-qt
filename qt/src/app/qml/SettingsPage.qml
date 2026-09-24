// Settings: a large modal sheet with sections (pen, touch, stabilizer, documents, search, new pages, storage,
// shortcuts).
// The values are upstream Xournal++'s settings (settings.xml keys); they apply immediately and are saved when the
// sheet closes. "Storage" is about the cache of the library of this window (a setting of the library).
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
    /// The cache was removed: the window closes (so the app does not build it again at once)
    signal quitRequested()

    function sizeText(bytes) {
        if (bytes >= 1024 * 1024)
            return qsTr("%1 MB").arg((bytes / 1024 / 1024).toFixed(1))
        return qsTr("%1 kB").arg(bytes > 0 ? Math.max(1, Math.round(bytes / 1024)) : 0)
    }

    background: Rectangle { color: "#fafafa"; radius: 14 }

    FuzzyHelp { id: fuzzyHelp; objectName: "settingsFuzzyHelp" }

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
            TabButton { objectName: "documentsTab"; text: qsTr("Documents"); width: implicitWidth }
            TabButton { objectName: "searchTab"; text: qsTr("Search"); width: implicitWidth }
            TabButton { text: qsTr("New pages"); width: implicitWidth }
            TabButton { objectName: "storageTab"; text: qsTr("Storage"); width: implicitWidth }
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
                    SwitchRow {
                        objectName: "touchDrawingSwitch"
                        key: "touchDrawing"
                        text: qsTr("Draw with the finger")
                    }
                    Hint {
                        text: qsTr("One finger draws with the current tool (the hand still scrolls), two fingers scroll "
                                   + "and zoom. The same as the finger button in the tool bar. The mouse always draws "
                                   + "with its left button.")
                    }
                    SwitchRow { key: "zoomGestures"; text: qsTr("Pinch with two fingers to zoom") }
                    Hint {
                        text: qsTr("One finger scrolls (unless it draws), two fingers pan and zoom. Tap with two "
                                   + "fingers to undo, with three fingers to redo.")
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
                    SectionTitle { text: qsTr("Keep documents as") }
                    DocumentModeCards {
                        objectName: "documentModeCards"
                        Layout.fillWidth: true
                        Layout.maximumWidth: 640
                        mode: app.documentMode
                        onPicked: function(mode) { app.documentMode = mode }
                    }
                    Hint {
                        text: qsTr("Documents you have keep their format: a .xopp stays a .xopp until you save it as a "
                                   + "PDF with notes (Save as…). Copies for Xournal++: Share → “For Xournal++”.")
                    }
                    SectionTitle { text: qsTr("Start") }
                    SwitchRow { key: "restoreSession"; text: qsTr("Reopen the documents of the last session") }
                    SwitchRow { key: "resumeAtLastPage"; text: qsTr("Open documents where they were left off") }
                    SectionTitle { text: qsTr("Links") }
                    ComboRow {
                        objectName: "linkOpeningRow"
                        key: "linkOpening"
                        text: qsTr("A link to another document opens")
                        options: [
                            { text: qsTr("Ask each time"), value: "ask" },
                            { text: qsTr("In a new tab"), value: "tab" },
                            { text: qsTr("As reference, beside this one"), value: "reference" },
                            { text: qsTr("Here, in place of this one"), value: "here" }
                        ]
                    }
                    SectionTitle { text: qsTr("Hybrid PDF") }
                    Hint {
                        text: qsTr("A PDF with notes (Save as… → \"PDF with notes, editable\") shows your notes in "
                                   + "any PDF app and opens here with everything editable.")
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        visible: !app.pdfOnly  // (PDF files: notes always go into the PDF itself)
                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            text: qsTr("Save notes into the PDF itself")
                        }
                        Switch {
                            objectName: "hybridIntoPdfSwitch"
                            checked: (sheet.s.revision, sheet.s.get("hybridIntoPdf"))
                            onToggled: {
                                sheet.s.set("hybridIntoPdf", checked)
                                if (checked && !sheet.s.get("hybridIntoPdfExplained")) {
                                    sheet.s.set("hybridIntoPdfExplained", true)
                                    intoPdfExplanation.open()
                                }
                            }
                        }
                    }
                    SwitchRow {
                        key: "hybridExportXopp"
                        text: qsTr("On every save of a hybrid PDF, also write a .xopp for Xournal++")
                    }
                    ComboRow {
                        objectName: "hybridOldXoppRow"
                        key: "hybridOldXopp"
                        text: qsTr("A document saved as a .xopp is saved as a PDF with notes: its .xopp")
                        options: [
                            { text: qsTr("Ask each time"), value: "ask" },
                            { text: qsTr("Move it to the trash"), value: "trash" },
                            { text: qsTr("Keep it updated for Xournal++"), value: "update" },
                            { text: qsTr("Keep it as it is"), value: "keep" }
                        ]
                    }
                    SectionTitle { text: qsTr("Autosave") }
                    SwitchRow { key: "autosaveEnabled"; text: qsTr("Save a backup of unsaved changes regularly") }
                    SliderRow {
                        enabled: (sheet.s.revision, sheet.s.get("autosaveEnabled"))
                        key: "autosaveMinutes"; text: qsTr("Every")
                        from: 1; to: 30; stepSize: 1; decimals: 0; suffix: " min"
                    }
                    SectionTitle { text: qsTr("Memory") }
                    SliderRow {
                        key: "canvasMemory"; text: qsTr("Rendered pages of the open documents")
                        // Up to a third of the memory of this computer (default: a quarter)
                        from: 256; to: Math.max(512, Math.floor(sheet.s.systemMemory / 3 / 128) * 128)
                        stepSize: 128; decimals: 0; suffix: " MB"
                    }
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        opacity: 0.7
                        font.pixelSize: 13
                        text: qsTr("Pages are drawn ahead and kept: scrolling shows them at once and needs no "
                                   + "drawing again. The document in use may take 70 % of it while others are open.")
                    }
                    SliderRow {
                        key: "previewMemory"; text: qsTr("Page previews (sidebar, overviews)")
                        from: 64; to: 1024; stepSize: 64; decimals: 0; suffix: " MB"
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

            // --- Search: the fuzzy search (its toggle is the one in the search fields: app.library.fuzzySearch) ---
            ScrollView {
                contentWidth: availableWidth
                ColumnLayout {
                    width: parent.width - 48
                    x: 24
                    spacing: 10
                    RowLayout {
                        Layout.fillWidth: true
                        SectionTitle { text: qsTr("Fuzzy search") }
                        Button {
                            objectName: "fuzzyHelpButton"
                            Layout.topMargin: 18
                            flat: true
                            text: qsTr("Help: syntax and examples")
                            onClicked: fuzzyHelp.open()
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            text: qsTr("Fuzzy search in the library and the tab overview (the \"Fuzzy\" button "
                                       + "next to their search fields)")
                        }
                        Switch {
                            objectName: "fuzzySearchSwitch"
                            checked: app.library.fuzzySearch
                            onToggled: app.library.fuzzySearch = checked
                        }
                    }
                    Hint {
                        text: qsTr("Names match when their letters come in this order (like fzf). In the text of "
                                   + "the documents, a word of three or more letters matches when it has the letters "
                                   + "close together (tbine finds \"turbine\"), or with a typo; the whole word is "
                                   + "marked. Shorter words are found as they are typed.")
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            text: qsTr("Typo tolerance in text")
                        }
                        ComboBox {
                            id: typos
                            objectName: "fuzzyTyposCombo"
                            Layout.preferredWidth: 360
                            model: [
                                { text: qsTr("Off"), value: 0 },
                                { text: qsTr("1 letter, in words of 5+ letters"), value: 1 },
                                { text: qsTr("Up to 2 letters, in words of 8+ letters"), value: 2 }
                            ]
                            textRole: "text"
                            valueRole: "value"
                            currentIndex: (sheet.s.revision, count, indexOfValue(sheet.s.get("fuzzyTypos")))
                            onActivated: sheet.s.set("fuzzyTypos", currentValue)
                        }
                    }
                    Hint {
                        text: qsTr("A typo is one letter swapped with the next, left out, added or wrong: with 1, "
                                   + "turbnie, trbine and turbime find \"turbine\". With 2, words of 8 or more "
                                   + "letters may have two (trasnfromation finds \"transformation\"), shorter ones "
                                   + "one. Documents with the word as typed come first.")
                    }
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

            // --- Storage: the cache of this library ---
            ScrollView {
                contentWidth: availableWidth
                onVisibleChanged: if (visible && app.library.available) app.library.measureCache()
                ColumnLayout {
                    width: parent.width - 48
                    x: 24
                    spacing: 10
                    SectionTitle { text: qsTr("Cache of this library") }
                    Hint {
                        text: app.library.available
                              ? qsTr("“%1” keeps the previews of its documents and its search index in a hidden folder "
                                     + ".xournal_library in each folder with documents, or in the app's cache folder. "
                                     + "It only makes the app faster: it can be removed at any time and is built again "
                                     + "when needed.").arg(app.library.name)
                              : qsTr("No library is open in this window.")
                    }
                    Label {
                        objectName: "cacheSizeLabel"
                        visible: app.library.available
                        text: app.library.cacheBytes < 0
                              ? qsTr("Counting …")
                              : qsTr("It takes %1 in %2 files.").arg(sheet.sizeText(app.library.cacheBytes))
                                                                  .arg(app.library.cacheFiles)
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        visible: app.library.available
                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            text: qsTr("Keep the cache in the app's cache folder, not in the library's folders")
                        }
                        Switch {
                            objectName: "cacheInAppSwitch"
                            enabled: !app.library.cacheRemoved
                            checked: app.library.cacheInAppCache
                            onToggled: app.library.cacheInAppCache = checked
                        }
                    }
                    Hint {
                        visible: app.library.available
                        text: qsTr("Recommended for folders that are synced (OneDrive, Dropbox, Nextcloud …): the "
                                   + "library's folders then stay free of the app's files, and nothing of it is "
                                   + "uploaded. Switching moves the cache. The app's cache folder of this library: %1")
                              .arg(app.library.appCachePath)
                    }
                    SectionTitle { text: qsTr("Clean up"); visible: app.library.available }
                    Hint {
                        visible: app.library.available
                        text: qsTr("Remove all cache folders of this library, e.g. to zip the library and send it. "
                                   + "Where you were in each document is kept.")
                    }
                    Button {
                        objectName: "removeCachesButton"
                        visible: app.library.available
                        enabled: !app.library.cacheRemoved
                        text: qsTr("Remove all cache folders of this library …")
                        onClicked: removeCaches.open()
                    }
                    Hint {
                        visible: app.library.cacheRemoved
                        text: qsTr("Removed. They are built again the next time the library is opened.")
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

    // Shown once, when "Save notes into the PDF itself" is turned on
    Dialog {
        id: intoPdfExplanation
        objectName: "intoPdfExplanation"
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: Math.min(480, parent ? parent.width - 32 : 480)
        title: qsTr("Notes in the PDF itself")
        standardButtons: Dialog.Ok
        Label {
            width: intoPdfExplanation.availableWidth
            wrapMode: Text.Wrap
            text: qsTr("Saving an annotated PDF now writes your notes into that PDF, as in Xodo or Drawboard: "
                       + "Ctrl+S saves it without asking. Other PDF apps show the notes as annotations, and xournal-qt "
                       + "opens them with everything editable.") + "\n\n"
                  + qsTr("The first time a PDF gets notes, its original is kept next to it as “name.original.pdf”. "
                         + "Turned off, notes go into a new file “name.notes.pdf” and the PDF is left alone.")
        }
    }

    // Removing the cache folders: says what happens (the app closes afterwards)
    Dialog {
        id: removeCaches
        objectName: "removeCachesDialog"
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: Math.min(480, parent ? parent.width - 32 : 480)
        title: qsTr("Remove the cache folders?")
        ColumnLayout {
            width: removeCaches.availableWidth
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: qsTr("All .xournal_library folders of “%1” are removed, and its cache in the app's cache "
                           + "folder (%2). Only files the app wrote are removed; where you were in each document is "
                           + "kept.").arg(app.library.name).arg(sheet.sizeText(Math.max(0, app.library.cacheBytes)))
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: qsTr("The app closes afterwards, so it does not build them again right away (for example while "
                           + "you zip the library to send it). The next time the library is opened, they are built "
                           + "again.")
            }
        }
        footer: DialogButtonBox {
            Button {
                objectName: "removeCachesConfirm"
                text: qsTr("Remove and close the app")
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            }
            Button {
                text: qsTr("Cancel")
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            }
        }
        onAccepted: {
            app.library.removeCaches()
            sheet.close()
            sheet.quitRequested()
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
