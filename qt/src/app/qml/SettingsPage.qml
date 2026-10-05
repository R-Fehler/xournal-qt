// Settings: a large modal sheet with sections (pen, touch, stabilizer, documents, display, search, new pages,
// storage, shortcuts). On a desktop or a tablet the sections are tabs; on a phone (qt/docs/adaptive-layout.md) the sheet
// takes the whole screen and shows the sections as a list, each opening as a page with a back arrow. Below 600 px the
// rows put their label above the slider or the box.
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
    parent: Overlay.overlay
    readonly property var win: ApplicationWindow.window
    readonly property var adaptive: win && win.adaptive ? win.adaptive : null
    /// A phone: the whole screen, the sections as a list
    readonly property bool phone: adaptive !== null && ["phonePortrait", "phoneShort", "tiny"].indexOf(adaptive.layoutClass) >= 0
    /// The rows put their label above the control
    readonly property bool narrow: width < 600
    /// On a phone: a section is shown (else the list of them)
    property bool sectionShown: false
    /// The window's safe area and the soft keyboard (Main.qml: win.safeInsets, win.keyboardTop)
    readonly property bool inWindow: win !== null && win !== undefined && win.safeInsets !== undefined
    readonly property real keyboardTop: {
        if (inWindow) return win.keyboardOpen ? win.keyboardTop : Infinity
        const r = Qt.inputMethod.keyboardRectangle
        return Qt.inputMethod.visible && r.height > 0 ? r.y / (Qt.platform.os === "android" ? Screen.devicePixelRatio : 1)
                                                      : Infinity
    }
    readonly property real safeTop: inWindow ? win.safeTop : 0
    readonly property real safeLeft: inWindow ? win.safeLeft : 0
    readonly property real safeRight: inWindow ? win.safeRight : 0
    /// Above the soft keyboard while it is open, else above the navigation bar
    readonly property real roomBottom: parent ? Math.min(parent.height - (inWindow ? win.safeBottom : 0), keyboardTop) : 700
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
    function showSection(index) {
        sections.currentIndex = index
        sectionShown = true
    }
    function showShortcuts() { showSection(shortcutsSection) }
    /// Help (qt/docs/onboarding.md): the window shows the introduction, the tutorial, the question whether to start
    /// the tutorial again (the sheet is closed first)
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
    component SliderRow: GridLayout {
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
        // (narrow: the label above the slider and its value)
        columns: sheet.narrow ? 2 : 3
        rowSpacing: 0
        Label {
            id: label
            Layout.columnSpan: sheet.narrow ? 2 : 1
            Layout.fillWidth: sheet.narrow
            Layout.preferredWidth: sheet.narrow ? -1 : 220
            wrapMode: Text.WordWrap
        }
        Slider {
            id: slider
            Layout.fillWidth: true
            Layout.minimumWidth: 120
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
    component ComboRow: GridLayout {
        id: row
        property string key
        property alias text: label.text
        property var options: []    // [{ text, value }]; value may be a string or an index
        property var dependsOn      // another value its setting follows (read again when it changes)
        // Instead of a key: where the value is read and written (functions)
        property var getter: null
        property var setter: null
        Layout.fillWidth: true
        // (narrow: the label above the box)
        columns: sheet.narrow ? 1 : 2
        rowSpacing: 0
        Label { id: label; Layout.fillWidth: true; wrapMode: Text.WordWrap }
        ComboBox {
            Layout.fillWidth: sheet.narrow
            Layout.preferredWidth: sheet.narrow ? -1 : 260
            model: row.options
            textRole: "text"
            valueRole: "value"
            // count: re-evaluate once the model is there
            currentIndex: (sheet.s.revision, row.dependsOn, count,
                           indexOfValue(row.getter ? row.getter() : sheet.s.get(row.key)))
            onActivated: row.setter ? row.setter(currentValue) : sheet.s.set(row.key, currentValue)
        }
    }

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
            ScrollView {
                contentWidth: availableWidth
                ColumnLayout {
                    width: parent.width - 48
                    x: 24
                    spacing: 10
                    // The toolbox or the classic tool bar (qt/docs/toolbox.md)
                    SectionTitle { text: qsTr("Tools") }
                    ComboRow {
                        objectName: "toolbarModeRow"
                        key: "toolbarMode"; text: qsTr("The tools")
                        options: [
                            { text: qsTr("My toolbox (my own pens, highlighters, …)"), value: "toolbox" },
                            { text: qsTr("Classic tool bar"), value: "classic" }
                        ]
                    }
                    Hint {
                        text: qsTr("The toolbox holds your own tools, each with its color and width, beside the page "
                                   + "(drag its dotted grip to another edge). Tap a tool to take it, tap it again to "
                                   + "change it; hold it for its menu, hold and move it to sort it. The classic tool "
                                   + "bar stays for one more release.")
                    }
                    Button {
                        objectName: "resetToolboxButton"
                        visible: (sheet.s.revision, sheet.s.get("toolbarMode")) === "toolbox"
                        text: qsTr("Back to the first tools…")
                        onClicked: resetToolboxDialog.open()
                    }
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
                    ComboRow {
                        objectName: "hoverPointerRow"
                        key: "hoverPointer"; text: qsTr("Pointer over the page")
                        options: [
                            { text: qsTr("Dot"), value: "dot" },
                            { text: qsTr("Crosshair"), value: "crosshair" }
                        ]
                    }
                    Hint {
                        text: qsTr("What the mouse and the hovering pen show over the page: a small dot where the "
                                   + "pen will write, or the crosshair. With the eraser it is the eraser itself, gray, "
                                   + "as big as it erases at the zoom (dashed when it erases whole strokes).")
                    }
                    // The pen's gestures (qt/docs/pen-gestures.md)
                    SectionTitle { text: qsTr("Gestures") }
                    SwitchRow {
                        objectName: "holdToStraightenSwitch"
                        key: "holdToStraighten"; text: qsTr("Hold to straighten")
                    }
                    SliderRow {
                        objectName: "holdToStraightenTimeSlider"
                        visible: (sheet.s.revision, sheet.s.get("holdToStraighten"))
                        key: "holdToStraightenTime"; text: qsTr("Hold for")
                        from: 250; to: 2000; stepSize: 50
                        factor: 0.001; decimals: 2; suffix: " s"
                    }
                    Hint {
                        text: qsTr("Finish a line, a circle, an ellipse, a rectangle or a triangle with the pen or the "
                                   + "highlighter and keep the pen still on the screen for a moment: the stroke becomes "
                                   + "that shape, straight and neat. Undo brings the stroke back as you drew it.")
                    }
                    SwitchRow {
                        objectName: "scratchOutSwitch"
                        key: "scratchOut"; text: qsTr("Scratch out to erase")
                    }
                    Hint {
                        text: qsTr("Scribble quickly back and forth over handwriting (at least four strokes across, "
                                   + "like crossing out on paper): what the zigzag covers is erased, and the zigzag "
                                   + "goes too. Undo brings it back. A zigzag over nothing stays a stroke.")
                    }
                    SectionTitle { text: qsTr("Laser pointer") }
                    SliderRow {
                        objectName: "laserFadeSlider"
                        key: "laserPointerFadeOutTime"; text: qsTr("The ink fades after")
                        from: 0; to: 10000; stepSize: 100
                        factor: 0.001; decimals: 1; suffix: " s"
                    }
                    Hint {
                        text: qsTr("The laser pointer and the laser highlighter (in the list of the pen button, and "
                                   + "in the tools of full screen and presenting) draw ink that is never kept: it "
                                   + "fades this long after the pen is lifted.")
                    }
                    SectionTitle { text: qsTr("Grid") }
                    SwitchRow { objectName: "snapGridSwitch"; key: "snapGrid"; text: qsTr("Snap to the grid") }
                    Hint {
                        text: qsTr("Selections that are moved and the corners of shapes jump onto the nearest point "
                                   + "of a half-centimetre grid when they come close to it (as in Xournal++). Also in "
                                   + "the shapes menu of the tool bar. Hold Alt to do the opposite for a moment.")
                    }
                    SectionTitle { text: qsTr("Colors") }
                    ComboRow {
                        objectName: "colorPaletteRow"
                        text: qsTr("Color palette")
                        options: app.colorPalettes.map(function(p) { return { text: p.name, value: p.id } })
                        dependsOn: app.colorPalette
                        getter: function() { return app.colorPalette }
                        setter: function(id) { app.colorPalette = id }
                    }
                    Flow {  // the chosen palette's ink colors, with their roles' names
                        objectName: "colorPalettePreview"
                        Layout.fillWidth: true
                        spacing: 4
                        Repeater {
                            model: {
                                const all = app.colorPalettes
                                for (let i = 0; i < all.length; ++i) if (all[i].id === app.colorPalette) return all[i].roles
                                return []
                            }
                            delegate: Rectangle {
                                required property var modelData
                                width: 22; height: 22; radius: 11
                                color: modelData.ink
                                border.width: 1
                                border.color: "#9e9e9e"
                                ToolTip.visible: dotHover.hovered
                                ToolTip.text: modelData.name
                                ToolTip.delay: 400
                                HoverHandler { id: dotHover }
                            }
                        }
                    }
                    Hint {
                        text: qsTr("The color chooser of the tool bar has a tab for each palette. Each color of a "
                                   + "palette has a meaning (warnings, key terms, headings, …) that stays the same in "
                                   + "every palette: a color taken from a palette follows when another palette is "
                                   + "chosen. With the highlighter the palettes give their highlight colors, half "
                                   + "see-through on light paper and stronger on dark paper.")
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
                    GridLayout {
                        visible: app.penHover.reportsHeight
                        columns: sheet.narrow ? 1 : 2
                        columnSpacing: 12
                        Label {
                            Layout.preferredWidth: sheet.narrow ? -1 : 220
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
                    SwitchRow {
                        objectName: "handWhenOpeningSwitch"
                        key: "handWhenOpening"
                        text: qsTr("Open documents with the hand")
                    }
                    Hint {
                        text: qsTr("A document opens with the hand tool, so one finger scrolls. Choose the pen (or "
                                   + "another tool) to write.")
                    }
                    ComboRow {
                        objectName: "touchProfileRow"
                        key: "touchProfile"
                        text: qsTr("Buttons sized for fingers")
                        options: [{ text: qsTr("When a finger is used"), value: "auto" },
                                  { text: qsTr("Always"), value: "on" },
                                  { text: qsTr("Never"), value: "off" }]
                    }
                    Hint {
                        text: qsTr("Some buttons get bigger when the screen is touched with a finger, and smaller again "
                                   + "when the mouse is used. The pen changes nothing.")
                    }
                    SwitchRow { key: "zoomGestures"; text: qsTr("Pinch with two fingers to zoom") }
                    Hint {
                        text: qsTr("One finger scrolls (unless it draws), two fingers pan and zoom. Tap with two "
                                   + "fingers to undo, with three fingers to redo.")
                    }
                    SwitchRow {
                        objectName: "rotateGestureRow"
                        key: "rotateGesture"
                        text: qsTr("Turn the canvas with two fingers")
                    }
                    Hint {
                        text: qsTr("Twist two fingers (or turn them on the touchpad) to turn the canvas, not the pages. "
                                   + "It snaps to quarter turns. Two taps on the page, a fit or the chip beside the "
                                   + "page number turn it upright again; Ctrl+] and Ctrl+[ turn it by a quarter.")
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
                    ComboRow {
                        objectName: "newTextDocumentsRow"
                        key: "newTextDocuments"
                        dependsOn: app.documentMode  // (while not chosen, it follows the mode)
                        text: qsTr("New text documents")
                        options: [
                            { text: qsTr("PDF document"), value: "pdf" },
                            { text: qsTr("Markdown file"), value: "md" }
                        ]
                    }
                    Hint {
                        text: qsTr("A PDF document opens in any PDF app and carries its text as a Markdown file inside. "
                                   + "Markdown files you have stay Markdown files.")
                    }
                    // Quick note (qt/docs/quick-note.md): Ctrl+Alt+N, the home screen, ⋮, --quick-note
                    ComboRow {
                        objectName: "quickNoteRow"
                        key: "quickNote"
                        text: qsTr("Quick note")
                        options: [
                            { text: qsTr("A new note"), value: "note" },
                            { text: qsTr("A line in today's Markdown note"), value: "daily" }
                        ]
                    }
                    Hint {
                        text: qsTr("Quick notes go into the folder “Inbox” of the library: a new note is named by the date "
                                   + "and time; today's Markdown note by the date, a line with the time added each time.")
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
                    // The library's To-dos (qt/docs/todos.md): which check boxes of Markdown are to-dos
                    SectionTitle { text: qsTr("To-dos") }
                    ComboRow {
                        objectName: "todoSourceRow"
                        key: "todoSource"
                        text: qsTr("Collect to-dos from")
                        options: [
                            { text: qsTr("Lines marked as to-dos"), value: "marked" },
                            { text: qsTr("Every check box"), value: "all" }
                        ]
                    }
                    GridLayout {
                        Layout.fillWidth: true
                        visible: (sheet.s.revision, sheet.s.get("todoSource")) !== "all"
                        columns: sheet.narrow ? 1 : 2
                        rowSpacing: 0
                        Label { text: qsTr("Marker"); Layout.fillWidth: true; wrapMode: Text.WordWrap }
                        TextField {
                            objectName: "todoMarkerField"
                            Layout.fillWidth: sheet.narrow
                            Layout.preferredWidth: sheet.narrow ? -1 : 260
                            text: (sheet.s.revision, sheet.s.get("todoMarker"))
                            placeholderText: "todo:"
                            onEditingFinished: sheet.s.set("todoMarker", text)
                        }
                    }
                    Hint {
                        text: qsTr("A check box line (- [ ] …) of a Markdown text, a sticky note or a Markdown file is a "
                                   + "to-do when it has the marker anywhere in it (upper or lower case); the list does not "
                                   + "show the marker. Check-box stamps for handwritten to-dos always count. A due date: "
                                   + "📅 2026-10-12 or due:2026-10-12.")
                    }
                    // Looking up selected text, the papers of references, arXiv (qt/docs/citations.md)
                    SectionTitle { text: qsTr("Web and citations") }
                    SwitchRow {
                        objectName: "webConfirmRow"
                        key: "webConfirm"
                        text: qsTr("Ask before opening a web address (it is shown whole)")
                    }
                    ComboRow {
                        objectName: "networkAccessRow"
                        key: "networkAccess"
                        text: qsTr("Connect to the web: arXiv (searching sends the title's words; downloading fetches the PDF), "
                                   + "web pictures of Markdown texts (when \"Load image\" is chosen)")
                        options: [
                            { text: qsTr("Ask the first time"), value: "ask" },
                            { text: qsTr("Allowed"), value: "on" },
                            { text: qsTr("Off"), value: "off" }
                        ]
                    }
                    // The web search of selected text (the look-up menu): an engine, or an address with {text}
                    GridLayout {
                        id: webSearchRow
                        Layout.fillWidth: true
                        columns: sheet.narrow ? 1 : 2
                        rowSpacing: 0
                        readonly property string current: (sheet.s.revision, sheet.s.get("webSearch"))
                        readonly property var known: app.citations.searchEngines()
                        readonly property bool custom: !known.some(function(e) { return e.key === current })
                        Label { text: qsTr("Search the web with"); Layout.fillWidth: true; wrapMode: Text.WordWrap }
                        ComboBox {
                            objectName: "webSearchChoice"
                            Layout.fillWidth: sheet.narrow
                            Layout.preferredWidth: sheet.narrow ? -1 : 260
                            model: webSearchRow.known.map(function(e) { return e.name }).concat([qsTr("Custom…")])
                            currentIndex: {
                                for (let i = 0; i < webSearchRow.known.length; ++i)
                                    if (webSearchRow.known[i].key === webSearchRow.current) return i
                                return webSearchRow.known.length
                            }
                            onActivated: function(index) {
                                if (index < webSearchRow.known.length) sheet.s.set("webSearch", webSearchRow.known[index].key)
                                else if (!webSearchRow.custom) sheet.s.set("webSearch", "https://www.google.com/search?q={text}")
                            }
                        }
                    }
                    TextField {
                        id: webSearchAddress
                        objectName: "webSearchAddress"
                        visible: webSearchRow.custom
                        Layout.fillWidth: true
                        text: webSearchRow.current
                        placeholderText: "https://…{text}…"
                        readonly property bool valid: app.citations.isSearchTemplate(text)
                        // (only a valid address is kept: the menu's entry needs one)
                        onEditingFinished: if (valid) sheet.s.set("webSearch", text)
                    }
                    Hint {
                        objectName: "webSearchHint"
                        visible: webSearchRow.custom
                        color: webSearchAddress.valid ? "#6b6f75" : "#b3261e"
                        text: webSearchAddress.valid
                              ? qsTr("{text} is replaced by the selected text.")
                              : qsTr("The address must start with http:// or https:// and contain {text}.")
                    }
                    GridLayout {
                        id: translatorRow
                        Layout.fillWidth: true
                        columns: sheet.narrow ? 1 : 2
                        rowSpacing: 0
                        readonly property string current: (sheet.s.revision, sheet.s.get("translateService"))
                        readonly property var known: app.citations.translators()
                        readonly property bool custom: !known.some(function(t) { return t.key === current })
                        Label { text: qsTr("Translate with"); Layout.fillWidth: true; wrapMode: Text.WordWrap }
                        ComboBox {
                            objectName: "translatorChoice"
                            Layout.fillWidth: sheet.narrow
                            Layout.preferredWidth: sheet.narrow ? -1 : 260
                            model: translatorRow.known.map(function(t) { return t.name }).concat([qsTr("Custom address…")])
                            currentIndex: {
                                for (let i = 0; i < translatorRow.known.length; ++i)
                                    if (translatorRow.known[i].key === translatorRow.current) return i
                                return translatorRow.known.length
                            }
                            onActivated: function(index) {
                                if (index < translatorRow.known.length) sheet.s.set("translateService", translatorRow.known[index].key)
                                else if (!translatorRow.custom) sheet.s.set("translateService", "https://translate.google.com/?sl=auto&tl={lang}&text={text}")
                            }
                        }
                    }
                    TextField {
                        objectName: "translatorAddress"
                        visible: translatorRow.custom
                        Layout.fillWidth: true
                        text: translatorRow.current
                        placeholderText: "https://…{text}…{lang}"
                        onEditingFinished: sheet.s.set("translateService", text)
                    }
                    Hint {
                        visible: translatorRow.custom
                        text: qsTr("{text} is replaced by the selected text, {lang} by the language below.")
                    }
                    ComboRow {
                        objectName: "translateLanguageRow"
                        key: "translateLanguage"
                        text: qsTr("Translate into")
                        options: [
                            { text: qsTr("The system's language (%1)").arg(app.citations.systemLanguage()), value: "" },
                            { text: "English", value: "en" }, { text: "Deutsch", value: "de" },
                            { text: "Français", value: "fr" }, { text: "Español", value: "es" },
                            { text: "Italiano", value: "it" }, { text: "Português", value: "pt" },
                            { text: "Nederlands", value: "nl" }, { text: "Polski", value: "pl" },
                            { text: "Čeština", value: "cs" }, { text: "Svenska", value: "sv" },
                            { text: "Türkçe", value: "tr" }, { text: "Українська", value: "uk" },
                            { text: "Русский", value: "ru" }, { text: "中文 (简体)", value: "zh-CN" },
                            { text: "日本語", value: "ja" }, { text: "한국어", value: "ko" }
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
                    GridLayout {
                        Layout.fillWidth: true
                        columns: sheet.narrow ? 1 : 2
                        rowSpacing: 0
                        Label { text: qsTr("Name for new documents"); Layout.preferredWidth: sheet.narrow ? -1 : 220 }
                        TextField {
                            Layout.fillWidth: true
                            text: (sheet.s.revision, sheet.s.get("defaultSaveName"))
                            onEditingFinished: sheet.s.set("defaultSaveName", text)
                        }
                    }
                    Hint { text: qsTr("%F is the date (2026-09-19), %H-%M the time.") }
                    // Audio recordings (qt/docs/audio.md)
                    SectionTitle { visible: app.audio.available; text: qsTr("Audio recordings") }
                    RowLayout {
                        visible: app.audio.available
                        Layout.fillWidth: true
                        Label {
                            text: qsTr("Playing from ink starts this much earlier")
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }
                        SpinBox {
                            objectName: "audioLeadIn"
                            from: 0
                            to: 10
                            value: Math.round(app.audio.leadInMs / 1000)
                            textFromValue: function(v) { return qsTr("%1 s").arg(v) }
                            valueFromText: function(t) { return parseInt(t) }
                            onValueModified: app.audio.leadInMs = value * 1000
                        }
                    }
                    Hint {
                        visible: app.audio.available
                        text: qsTr("Recordings are kept in the app's audio folder, as Xournal++ keeps them; a PDF with notes carries its own.")
                    }
                    Item { Layout.preferredHeight: 16 }
                }
            }

            // --- Display: the screen's calibration, so that 100 % is the size of the paper (ScreenCalibration.h) ---
            ScrollView {
                id: displayPage
                contentWidth: availableWidth
                // The screen this window is on, read again when the window moves to another screen or its scaling
                // changes. DPIs in logical pixels per inch (what the ruler is drawn in).
                readonly property var info: (sheet.s.revision, Screen.name, Screen.devicePixelRatio,
                                             sheet.s.screenCalibration(Window.window))
                /// What the ruler shows now (saved with "Save for this screen")
                property real dpi: 96
                readonly property real minDpi: 40
                readonly property real maxDpi: 400
                onInfoChanged: dpi = info.dpi
                Component.onCompleted: dpi = info.dpi
                function setDpi(value) { dpi = Math.max(minDpi, Math.min(maxDpi, value)) }

                ColumnLayout {
                    width: parent.width - 48
                    x: 24
                    spacing: 10
                    SectionTitle { text: qsTr("Real size") }
                    Hint {
                        text: qsTr("Hold a ruler against the screen and move the slider, or drag the ruler on the "
                                   + "screen, until its marks match the real ones. Pages at 100 % (Ctrl+1) are then "
                                   + "as large as the paper, and the set square and compass measure real "
                                   + "centimetres. Each screen keeps its own.")
                    }
                    Label {
                        objectName: "calibrationScreen"
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: qsTr("This screen: %1, scaled %2 %.").arg(displayPage.info.name || qsTr("unnamed"))
                                                                  .arg(Math.round(displayPage.info.dpr * 100))
                              + " "
                              + (displayPage.info.reportedPlausible
                                 ? qsTr("It says it has %1 dpi.").arg(displayPage.info.reportedDpi.toFixed(1))
                                 : qsTr("It does not tell its size: 96 dpi until it is calibrated."))
                    }
                    // The ruler: centimetres above, inches below, from the 0 mark on the left. A drag stretches it
                    // (the 0 mark stays), with the mouse or a finger.
                    Canvas {
                        id: ruler
                        objectName: "calibrationRuler"
                        Layout.fillWidth: true
                        Layout.preferredHeight: 112
                        readonly property real zeroX: 16
                        readonly property real dpi: displayPage.dpi
                        readonly property real pixelsPerCm: dpi / 2.54
                        onDpiChanged: requestPaint()
                        onWidthChanged: requestPaint()
                        onPaint: {
                            const ctx = getContext("2d")
                            ctx.reset()
                            ctx.fillStyle = "#fff8d6"
                            ctx.fillRect(0, 0, width, height)
                            ctx.strokeStyle = "#202124"
                            ctx.fillStyle = "#202124"
                            ctx.lineWidth = 1
                            ctx.font = "12px sans-serif"
                            ctx.beginPath()
                            // centimetres, in millimetres
                            const mm = pixelsPerCm / 10
                            for (let i = 0; zeroX + i * mm <= width - 2; ++i) {
                                const x = zeroX + i * mm
                                const h = i % 10 === 0 ? 30 : (i % 5 === 0 ? 20 : 11)
                                ctx.moveTo(x, 0)
                                ctx.lineTo(x, h)
                                if (i % 10 === 0)
                                    ctx.fillText(String(i / 10) + (i === 0 ? " cm" : ""), x + 3, 44)
                            }
                            // inches, in eighths
                            const eighth = dpi / 8
                            for (let j = 0; zeroX + j * eighth <= width - 2; ++j) {
                                const x = zeroX + j * eighth
                                const h = j % 8 === 0 ? 30 : (j % 4 === 0 ? 22 : (j % 2 === 0 ? 15 : 9))
                                ctx.moveTo(x, height)
                                ctx.lineTo(x, height - h)
                                if (j % 8 === 0)
                                    ctx.fillText(String(j / 8) + (j === 0 ? " in" : ""), x + 3, height - 34)
                            }
                            ctx.stroke()
                        }
                        MouseArea {
                            objectName: "calibrationRulerDrag"
                            anchors.fill: parent
                            preventStealing: true
                            cursorShape: Qt.SizeHorCursor
                            property real startX: 0
                            property real startDpi: 0
                            onPressed: function(mouse) {
                                startX = mouse.x - ruler.zeroX
                                startDpi = displayPage.dpi
                            }
                            onPositionChanged: function(mouse) {
                                if (startX > 24)  // (too close to the 0 mark to stretch from)
                                    displayPage.setDpi(startDpi * (mouse.x - ruler.zeroX) / startX)
                            }
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        ToolButton {
                            text: "−"
                            font.pixelSize: 22
                            implicitWidth: 44
                            ToolTip.visible: hovered
                            ToolTip.text: qsTr("A little shorter")
                            ToolTip.delay: 600
                            autoRepeat: true
                            onClicked: displayPage.setDpi(displayPage.dpi - 0.1)
                        }
                        Slider {
                            objectName: "calibrationSlider"
                            Layout.fillWidth: true
                            from: displayPage.minDpi; to: displayPage.maxDpi; stepSize: 0.1
                            value: displayPage.dpi
                            onMoved: displayPage.setDpi(value)
                        }
                        ToolButton {
                            text: "+"
                            font.pixelSize: 22
                            implicitWidth: 44
                            ToolTip.visible: hovered
                            ToolTip.text: qsTr("A little longer")
                            ToolTip.delay: 600
                            autoRepeat: true
                            onClicked: displayPage.setDpi(displayPage.dpi + 0.1)
                        }
                        Label {
                            objectName: "calibrationValue"
                            Layout.preferredWidth: 90
                            horizontalAlignment: Text.AlignRight
                            text: qsTr("%1 dpi").arg(displayPage.dpi.toFixed(1))
                        }
                    }
                    // (a Flow: on a phone the two buttons go on two lines)
                    Flow {
                        Layout.fillWidth: true
                        spacing: 8
                        Button {
                            objectName: "calibrationSave"
                            text: qsTr("Save for this screen")
                            highlighted: true
                            enabled: displayPage.info.key !== ""
                                     && (!displayPage.info.calibrated
                                         || Math.abs(displayPage.dpi - displayPage.info.dpi) > 0.05)
                            onClicked: sheet.s.calibrateScreen(Window.window, displayPage.dpi)
                        }
                        Button {
                            objectName: "calibrationReset"
                            text: qsTr("Back to what the screen says")
                            flat: true
                            enabled: displayPage.info.calibrated
                                     || Math.abs(displayPage.dpi - displayPage.info.defaultDpi) > 0.05
                            onClicked: {
                                sheet.s.resetScreenCalibration(Window.window)
                                displayPage.dpi = displayPage.info.defaultDpi
                            }
                        }
                    }
                    Hint {
                        objectName: "calibrationState"
                        text: displayPage.info.calibrated
                              ? qsTr("Calibrated: 100 % is the real size on this screen. Another screen keeps its "
                                     + "own calibration; the window takes it when it is moved there.")
                              : qsTr("Not calibrated yet: 100 % follows what the screen says.")
                    }

                    // The layout for the window's size (qt/docs/adaptive-layout.md)
                    SectionTitle { text: qsTr("Window size") }
                    SwitchRow {
                        objectName: "adaptiveLayoutSwitch"
                        key: "adaptiveLayout"
                        text: qsTr("Adapt the layout to the window size")
                    }
                    Hint {
                        objectName: "sizeClassHint"
                        readonly property var names: ({
                            desktopWide: qsTr("a wide desktop window"),
                            desktopNarrow: qsTr("a narrow desktop window or a tablet in landscape"),
                            tabletPortrait: qsTr("a tablet in portrait"),
                            phonePortrait: qsTr("a phone in portrait"),
                            phoneShort: qsTr("a phone in landscape or a low window"),
                            tiny: qsTr("a tiny window")
                        })
                        readonly property var adaptive: win.adaptive
                        text: qsTr("This window is %1 (%2 × %3).").arg(names[adaptive.sizeClass])
                                                            .arg(Math.round(adaptive.classWidth))
                                                            .arg(Math.round(adaptive.classHeight))
                              + " " + ((sheet.s.revision, sheet.s.get("adaptiveLayout"))
                                         ? qsTr("The page sidebar shows beside the page when there is room for it. "
                                                + "What you show or hide by hand is kept for each kind of window.")
                                         : qsTr("The window keeps the desktop layout at every size."))
                    }
                    ComboRow {
                        // The chrome chosen for this kind of window (stored as a layout choice, not a setting)
                        objectName: "chromeChoiceRow"
                        text: qsTr("Controls at this size")
                        // (automatic: all, and in a tiny window none)
                        options: [{ text: win.phoneLayout ? qsTr("All (app bar, tool dock)") : qsTr("All (tabs, tool bar)"), value: "full" },
                                  { text: qsTr("Compact (as in full screen)"), value: "compact" },
                                  { text: qsTr("None (reading)"), value: "reader" }]
                        getter: function() { return win.chromeSetting }
                        setter: function(v) { win.chooseChrome(v) }
                    }
                    Button {
                        objectName: "resetLayoutButton"
                        text: qsTr("Reset the layout choices")
                        flat: true
                        enabled: (sheet.s.revision, sheet.s.hasLayoutChoices())
                        onClicked: sheet.s.resetLayoutChoices()
                    }
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
                    GridLayout {
                        Layout.fillWidth: true
                        columns: sheet.narrow ? 1 : 2
                        rowSpacing: 0
                        SectionTitle { text: qsTr("Fuzzy search") }
                        Button {
                            objectName: "fuzzyHelpButton"
                            Layout.topMargin: sheet.narrow ? 0 : 18
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
                    GridLayout {
                        Layout.fillWidth: true
                        columns: sheet.narrow ? 1 : 2
                        rowSpacing: 0
                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            text: qsTr("Typo tolerance in text")
                        }
                        ComboBox {
                            id: typos
                            objectName: "fuzzyTyposCombo"
                            Layout.fillWidth: sheet.narrow
                            Layout.preferredWidth: sheet.narrow ? -1 : 360
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

                    // --- Handwriting (qt/docs/handwriting-search.md) ---
                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            text: qsTr("Search handwriting")
                        }
                        Switch {
                            objectName: "handwritingSearchSwitch"
                            checked: app.handwriting.enabled
                            onToggled: app.handwriting.enabled = checked
                        }
                    }
                    Hint {
                        text: qsTr("Handwritten words become searchable: in open documents, in the library, and (as "
                                   + "invisible text) in PDFs with notes and archive PDFs that other PDF apps open. "
                                   + "The handwriting is never turned into text. English only for now. The recogniser "
                                   + "runs on this computer at low priority; the library's other documents are read "
                                   + "only on mains power.")
                    }
                    Label {
                        objectName: "handwritingStatus"
                        visible: app.handwriting.enabled
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: app.handwriting.status
                        color: app.handwriting.ready ? "#1e7e34" : "#6b6f75"
                    }
                    ColumnLayout {
                        // The model is downloaded only when the user asks, its address and size shown first
                        objectName: "handwritingDownload"
                        visible: app.handwriting.enabled && app.handwriting.ownModel && !app.handwriting.modelInstalled
                        Layout.fillWidth: true
                        spacing: 6
                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            text: qsTr("The recogniser needs its model (%1), downloaded once from:")
                                  .arg(app.handwriting.downloadSize)
                        }
                        TextEdit {
                            objectName: "handwritingModelSource"
                            Layout.fillWidth: true
                            readOnly: true
                            selectByMouse: true
                            wrapMode: TextEdit.WrapAnywhere
                            text: app.handwriting.downloadSource
                            font.pixelSize: 13
                            color: "#3c4043"
                        }
                        RowLayout {
                            Button {
                                objectName: "handwritingDownloadButton"
                                text: qsTr("Download the model")
                                enabled: app.handwriting.downloadAvailable && !app.handwriting.downloading
                                onClicked: app.handwriting.download()
                            }
                            Button {
                                objectName: "handwritingCancelDownload"
                                visible: app.handwriting.downloading
                                text: qsTr("Cancel")
                                onClicked: app.handwriting.cancelDownload()
                            }
                        }
                        ProgressBar {
                            objectName: "handwritingDownloadProgress"
                            visible: app.handwriting.downloading
                            Layout.fillWidth: true
                            value: app.handwriting.downloadProgress
                        }
                        Label {
                            visible: app.handwriting.downloadError !== ""
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            text: app.handwriting.downloadError
                            color: "#c5221f"
                        }
                        Hint {
                            visible: !app.handwriting.downloadAvailable
                            text: qsTr("This version of the app cannot download it yet: install it with "
                                       + "qt/scripts/hwr-model.sh (see the documentation of the handwriting search).")
                        }
                    }
                    RowLayout {
                        visible: app.handwriting.ownModel && app.handwriting.modelInstalled
                        Layout.fillWidth: true
                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.WrapAnywhere
                            text: qsTr("The model is in %1").arg(app.handwriting.modelFolder)
                            font.pixelSize: 12
                            color: "#6b6f75"
                        }
                        Button {
                            objectName: "handwritingRemoveModel"
                            text: qsTr("Remove the model")
                            onClicked: app.handwriting.removeModel()
                        }
                    }
                    Hint {
                        visible: app.handwriting.enabled && !app.handwriting.ownModel
                        text: qsTr("The model is the one in %1 (XQT_HWR_MODEL or the setting \"handwritingModel\"): "
                                   + "it is never downloaded over or removed here.").arg(app.handwriting.modelFolder)
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
                        // (a size that is none of them, set in Xournal++: shown as it is)
                        options: sheet.s.paperFormats.map(function(name, i) { return { text: name, value: i } })
                                 .concat((sheet.s.revision, sheet.s.get("paperFormat")) < 0
                                         ? [{ text: qsTr("Other: %1").arg(sheet.s.templatePaperSize()), value: -1 }] : [])
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

            // --- Help: the introduction of the first start, the keyboard shortcuts (qt/docs/onboarding.md) ---
            ScrollView {
                contentWidth: availableWidth
                ColumnLayout {
                    width: parent.width - 48
                    x: 24
                    spacing: 10
                    SectionTitle { text: qsTr("Getting started") }
                    Hint {
                        text: qsTr("A few pages about what the app is for: notes and PDFs you write on, Markdown, "
                                   + "libraries and search, and how documents are kept.")
                    }
                    Button {
                        objectName: "showIntroButton"
                        text: qsTr("Show the introduction")
                        onClicked: { sheet.close(); sheet.introRequested() }
                    }
                    SectionTitle { text: qsTr("Tutorial") }
                    Hint {
                        text: qsTr("A document that asks you to try the tools, pages, search, tabs, Markdown and the "
                                   + "library, one page each. It is your copy to write on, kept in the app's own "
                                   + "folder, not in your library.")
                    }
                    RowLayout {
                        spacing: 8
                        Button {
                            objectName: "openTutorialButton"
                            text: qsTr("Open the tutorial")
                            onClicked: { sheet.close(); sheet.tutorialRequested() }
                        }
                        Button {
                            objectName: "restartTutorialButton"
                            visible: app.tutorialExists
                            flat: true
                            text: qsTr("Start it again…")
                            onClicked: { sheet.close(); sheet.restartTutorialRequested() }
                        }
                    }
                    SectionTitle { text: qsTr("Keyboard shortcuts") }
                    Hint { text: qsTr("F1 shows them over the page; Shortcuts here changes them.") }
                    Button {
                        objectName: "helpShortcutsButton"
                        text: qsTr("Show the shortcuts")
                        onClicked: sheet.showShortcuts()
                    }
                }
            }
        }
    }

    // Shown once, when "Save notes into the PDF itself" is turned on
    AdaptiveDialog {
        id: intoPdfExplanation
        objectName: "intoPdfExplanation"
        kind: "card"
        preferredWidth: 480
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
    AdaptiveDialog {
        id: removeCaches
        objectName: "removeCachesDialog"
        kind: "question"
        preferredWidth: 480
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
    AdaptiveDialog {
        id: capture
        objectName: "shortcutCapture"
        kind: "card"
        property int row: -1
        property string actionId: ""
        preferredWidth: 360
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
    // The toolbox back to its first tools (its order, colors and widths are lost)
    AdaptiveDialog {
        id: resetToolboxDialog
        objectName: "resetToolboxDialog"
        kind: "question"
        preferredWidth: 420
        title: qsTr("Back to the first tools?")
        Label {
            width: resetToolboxDialog.availableWidth
            wrapMode: Text.Wrap
            text: qsTr("Your tools, their order and their settings are replaced by the toolbox of the first start.")
        }
        footer: DialogButtonBox {
            Button { objectName: "resetToolboxConfirm"; text: qsTr("Reset"); DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole }
            Button { text: qsTr("Cancel"); DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
        }
        onAccepted: {
            app.toolbox.reset()
            app.applyToolEntry(app.toolbox.active)
        }
    }
}
