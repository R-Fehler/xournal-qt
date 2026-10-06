// The editor of one of the toolbox's tools (qt/docs/toolbox.md, "Editing a tool"): a tap on the tool in hand opens it,
// beside the tool towards the page (a bottom sheet on a phone). Every change is written at once and the tool in hand
// follows; there is no OK. The same editor makes a new tool ("+": a draft prefilled from the last tool of that kind,
// added with "Add").
//   a preview stroke · the color: the roles of the palette (they follow a palette switch), the colors used lately,
//   another color (the picker, a hex code) · the width: a slider from 0.1 to 150 pt on a log scale, shown in mm, and
//   the five sizes as dots · the line style · the filling (none, the line's color or another, its opacity) · the
//   eraser's kind · the shape and what draws it · the font of a text box · a sticky note's pastel · the laser's kind
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Dialogs
import QtQuick.Layouts
import "LineStyles.js" as LineStyles

Popup {
    id: editor
    objectName: "toolEntryEditor"
    /// The entry being edited ({…}), or the draft of a new one
    property var entry: ({})
    /// Editing an entry of the toolbox ("": a new one, added with "Add")
    property string entryId: ""
    /// Where a new one goes (an index among the items of its bar; -1: at the end: on the rail after the user's last
    /// tool) and the bar ("rail", "top")
    property int addAt: -1
    property string addBar: "rail"
    /// The button it opened from (it opens beside it, towards the page)
    property Item owner: null
    /// The button that shows an entry now (Toolbox.buttonFor): the owner again when the rail made its buttons anew
    property var ownerOf: null
    /// The toolbox's edge (where the page is from the owner)
    property string edge: "right"
    readonly property bool adding: entryId === ""
    readonly property string type: entry && entry.type ? entry.type : ""
    readonly property bool asSheet: win.phoneLayout
    readonly property var store: app.toolbox
    signal added(string id)

    /// Edit an entry of the toolbox
    function openFor(e, button, edgeName) {
        entryId = e.id
        entry = store.entry(e.id)
        owner = button
        edge = edgeName || "right"
        open()
    }
    /// Make a new tool of this type, prefilled from the last one of its type
    function openNew(type, at, button, edgeName, bar) {
        entryId = ""
        addAt = at === undefined ? -1 : at
        addBar = bar === "top" ? "top" : "rail"
        entry = store.normalize(store.prefill(type))
        owner = button
        edge = edgeName || "right"
        open()
    }
    /// A change: written at once (an entry of the toolbox), the tool in hand follows
    function set(fields) {
        if (adding) {
            const draft = Object.assign({}, entry)
            for (const k in fields) {
                if ((k === "fill" || k === "font") && typeof fields[k] === "object")
                    draft[k] = Object.assign({}, draft[k] || {}, fields[k])
                else
                    draft[k] = fields[k]
            }
            if (fields.color !== undefined && fields.role === undefined) draft.role = ""
            entry = store.normalize(draft)
            return
        }
        store.update(entryId, fields)
        entry = store.entry(entryId)
        if (store.active === entryId) app.applyToolEntry(entryId)
    }
    function add() {
        const id = store.add(entry, addBar === "rail" ? addAt : -1)
        if (id !== "" && addBar === "top") store.moveTo(id, "top", addAt >= 0 ? addAt : store.top.length)
        close()
        if (id !== "") {
            app.applyToolEntry(id)
            added(id)
        }
    }

    // --- the width ---
    /// The five sizes of upstream's tool of this kind (points)
    readonly property var sizes: type === "highlighter" || (entry.base === "highlighter" && type !== "laser")
                                 ? [1, 2.83, 8.5, 12, 18]
                                 : type === "eraser" ? [1, 2.83, 8.5, 19.84, 30]
                                 : type === "laser" ? [0.7, 1.41, 2.4, 4, 7] : [0.42, 0.85, 1.41, 2.26, 5.67]
    readonly property bool hasWidth: ["pen", "highlighter", "shape", "eraser", "laser"].indexOf(type) >= 0
    readonly property bool hasColor: type !== "eraser" && type !== "snip"
    readonly property bool hasLineStyle: type === "pen" || (type === "shape" && entry.base !== "highlighter")
    readonly property bool hasFill: type === "pen" || type === "highlighter" || type === "shape"
    /// 0.1 to 150 pt on a log scale (slider 0 … 1)
    function widthToSlider(w) { return Math.log(Math.max(0.1, w) / 0.1) / Math.log(1500) }
    function sliderToWidth(v) { return Math.round(0.1 * Math.pow(1500, v) * 100) / 100 }
    function mm(w) {
        const v = w * 25.4 / 72
        return (v < 1 ? v.toFixed(2) : v < 10 ? v.toFixed(1) : Math.round(v)) + " mm"
    }

    // --- the color ---
    readonly property var palette: {
        const ps = app.colorPalettes
        for (let i = 0; i < ps.length; ++i) if (ps[i].id === app.colorPalette) return ps[i]
        return ps.length > 0 ? ps[0] : null
    }
    readonly property bool highlights: type === "highlighter" || ((type === "shape" || type === "laser") && entry.base === "highlighter")
    readonly property color shownColor: (app.colorPalette, app.toolEntryColor(entry))

    parent: Overlay.overlay
    modal: asSheet
    dim: asSheet
    focus: true  // (Esc closes it)
    padding: 12
    topPadding: 10
    width: asSheet ? win.sheetWidth : 340
    height: Math.min(implicitHeight, (parent ? parent.height : 600) - 16)
    // Beside its button, towards the page. Placed (not bound to the button): a button can go away while the editor is
    // open (the rail made anew, the tool replaced), and a binding then put the editor in the window's top left corner
    // (2026-10-05). Without a button it stays where it is, and takes the entry's button again once there is one.
    property real placedX: 8
    property real placedY: 8
    function place() {
        if (!owner || !parent) return
        const p = owner.mapToItem(parent, 0, 0)
        const r = Qt.rect(p.x, p.y, owner.width, owner.height)
        placedX = Math.max(8, Math.min(parent.width - width - 8,
                                       edge === "right" ? r.x - width - 12 : edge === "left" ? r.x + r.width + 12
                                                        : r.x + r.width / 2 - width / 2))
        placedY = Math.max(8, Math.min(parent.height - height - 8,
                                       edge === "bottom" ? r.y - height - 12 : edge === "top" ? r.y + r.height + 12
                                                         : r.y + r.height / 2 - height / 2))
    }
    function findOwner() {
        if (owner || !visible || entryId === "" || typeof ownerOf !== "function") return
        owner = ownerOf(entryId)
    }
    onOwnerChanged: owner ? place() : Qt.callLater(findOwner)
    onAboutToShow: place()
    onWidthChanged: place()
    onHeightChanged: place()
    Connections {
        target: editor.parent
        function onWidthChanged() { editor.place() }
        function onHeightChanged() { editor.place() }
    }
    x: asSheet ? win.sheetX : placedX
    y: asSheet ? win.sheetBottom - height : placedY
    bottomPadding: asSheet ? 12 + win.sheetBottomPadding : 12
    background: Rectangle {
        radius: editor.asSheet ? 16 : 12
        color: "#ffffff"
        border.width: editor.asSheet ? 0 : 1
        border.color: "#d5d8dc"
    }
    Shortcut { sequence: "Back"; enabled: editor.opened; onActivated: editor.close() }  // (Android's back key)

    contentItem: Flickable {
        implicitHeight: body.implicitHeight
        contentHeight: body.implicitHeight
        contentWidth: width
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        interactive: contentHeight > height
        ScrollBar.vertical: ScrollBar { policy: parent.interactive ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff }
        ColumnLayout {
            id: body
            width: parent.width
            spacing: 8

            // The title and a preview stroke of what it draws
            RowLayout {
                Layout.fillWidth: true
                Label {
                    objectName: "toolEditorTitle"
                    Layout.fillWidth: true
                    text: editor.adding ? qsTr("New: %1").arg(win.toolEntryName(editor.entry)) : win.toolEntryName(editor.entry)
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }
                ToolButton {
                    objectName: "toolEditorClose"
                    visible: !editor.adding
                    icon.source: app.iconUrl("xqt-close")
                    onClicked: editor.close()
                }
            }
            Canvas {
                id: preview
                objectName: "toolEditorPreview"
                visible: editor.type !== "text" && editor.type !== "sticky" && editor.type !== "snip"
                Layout.fillWidth: true
                Layout.preferredHeight: 44
                readonly property var key: [editor.shownColor, editor.entry.width, editor.entry.lineStyle, editor.type,
                                            editor.entry.fill ? [editor.entry.fill.on, editor.entry.fill.color, editor.entry.fill.alpha] : 0,
                                            editor.entry.variant, editor.entry.base, width]
                onKeyChanged: requestPaint()
                onPaint: {
                    const ctx = getContext("2d")
                    ctx.reset()
                    const e = editor.entry
                    // the paper of this page
                    ctx.fillStyle = app.paperColor()
                    ctx.fillRect(0, 0, width, height)
                    if (editor.type === "eraser") {
                        ctx.strokeStyle = "#80868b"
                        ctx.lineWidth = 1
                        ctx.setLineDash(e.variant === "deleteStroke" ? [4, 3] : [])
                        const r = Math.min(height / 2 - 2, Math.max(3, e.width))
                        if (e.variant === "whiteout") {
                            ctx.beginPath(); ctx.arc(width / 2, height / 2, r, 0, 2 * Math.PI); ctx.stroke()
                        } else {
                            ctx.strokeRect(width / 2 - r, height / 2 - r, 2 * r, 2 * r)
                        }
                        return
                    }
                    const w = Math.min(height - 6, Math.max(0.5, e.width || 1))
                    const fill = e.fill && e.fill.on
                    if (fill) {
                        ctx.fillStyle = e.fill.color !== "" ? e.fill.color : editor.shownColor
                        ctx.globalAlpha = (e.fill.alpha || 128) / 255 * (editor.highlights ? 0.5 : 1)
                        ctx.beginPath()
                        ctx.ellipse(width / 2 - 50, 6, 100, height - 12)
                        ctx.fill()
                    }
                    ctx.globalAlpha = editor.highlights ? 0.5 : 1
                    ctx.strokeStyle = editor.shownColor
                    ctx.lineWidth = w
                    ctx.lineCap = editor.highlights ? "butt" : "round"
                    ctx.setLineDash(LineStyles.dashes(e.lineStyle))  // (as it draws: in widths of the line)
                    ctx.beginPath()
                    if (fill) {
                        ctx.ellipse(width / 2 - 50, 6, 100, height - 12)
                    } else {
                        // a wave across the paper
                        ctx.moveTo(16, height / 2)
                        for (let x = 16; x <= width - 16; x += 4)
                            ctx.lineTo(x, height / 2 + Math.sin((x - 16) / 18) * Math.max(2, (height - w) / 2 - 4))
                    }
                    ctx.stroke()
                }
            }

            // --- the shape: which one, and what draws it ---
            Flow {
                objectName: "toolEditorShapes"
                visible: editor.type === "shape"
                Layout.fillWidth: true
                spacing: 2
                Repeater {
                    model: win.toolGroups.variants("shape")
                    delegate: IconButton {
                        required property var modelData
                        objectName: "editorShape_" + modelData.key
                        implicitWidth: 40; implicitHeight: 40
                        icon.width: 22; icon.height: 22
                        iconName: modelData.icon
                        tip: modelData.name
                        checked: editor.entry.variant === modelData.key
                        onClicked: editor.set({ variant: modelData.key })
                    }
                }
            }
            RowLayout {
                visible: editor.type === "shape" || editor.type === "laser"
                Layout.fillWidth: true
                Label { text: qsTr("Drawn with"); Layout.fillWidth: true; color: "#5f6368" }
                Button {
                    objectName: "editorBasePen"
                    flat: true
                    checkable: true
                    checked: editor.entry.base !== "highlighter"
                    text: qsTr("Pen")
                    onClicked: editor.set({ base: "pen" })
                }
                Button {
                    objectName: "editorBaseHighlighter"
                    flat: true
                    checkable: true
                    checked: editor.entry.base === "highlighter"
                    text: qsTr("Highlighter")
                    onClicked: editor.set({ base: "highlighter" })
                }
            }

            // --- the eraser's kind ---
            RowLayout {
                objectName: "toolEditorEraserKinds"
                visible: editor.type === "eraser"
                Layout.fillWidth: true
                Repeater {
                    model: win.toolGroups.variants("eraser")
                    delegate: Button {
                        required property var modelData
                        objectName: "editorEraser_" + modelData.key
                        Layout.fillWidth: true
                        flat: true
                        checkable: true
                        checked: editor.entry.variant === modelData.key
                        icon.source: app.iconUrl(modelData.icon)
                        text: modelData.name
                        font.pixelSize: 12
                        display: AbstractButton.TextUnderIcon
                        onClicked: editor.set({ variant: modelData.key })
                    }
                }
            }

            // --- a snip's shape (a tap on it while it is armed takes the other one too) ---
            RowLayout {
                objectName: "toolEditorSnipShapes"
                visible: editor.type === "snip"
                Layout.fillWidth: true
                Repeater {
                    model: win.toolGroups.variants("snip")
                    delegate: Button {
                        required property var modelData
                        readonly property string shape: modelData.snip
                        objectName: "editorSnip_" + shape
                        Layout.fillWidth: true
                        flat: true
                        checkable: true
                        checked: (editor.entry.variant || "rect") === shape
                        icon.source: app.iconUrl(modelData.icon)
                        text: shape === "lasso" ? qsTr("Lasso") : qsTr("Rectangle")
                        font.pixelSize: 12
                        display: AbstractButton.TextUnderIcon
                        onClicked: editor.set({ variant: shape })
                    }
                }
            }

            // --- how sharp every snip is (a setting of all snips, also in Settings) ---
            RowLayout {
                objectName: "toolEditorSnipResolution"
                visible: editor.type === "snip"
                Layout.fillWidth: true
                Repeater {
                    model: win.toolGroups.snipResolutions
                    delegate: Button {
                        required property var modelData
                        objectName: "editorSnipResolution_" + modelData.key
                        Layout.fillWidth: true
                        flat: true
                        checkable: true
                        checked: app.snipResolution === modelData.key
                        text: modelData.short
                        font.pixelSize: 12
                        ToolTip.visible: hovered
                        ToolTip.text: modelData.name
                        ToolTip.delay: 600
                        onClicked: app.snipResolution = modelData.key
                    }
                }
            }

            // --- the color: the palette's roles, the colors used lately, another one ---
            ColumnLayout {
                objectName: "toolEditorColors"
                visible: editor.hasColor && editor.type !== "sticky"
                Layout.fillWidth: true
                spacing: 4
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: qsTr("Color"); Layout.fillWidth: true; color: "#5f6368" }
                    // The palette (app-wide: every tool with a role follows it)
                    ComboBox {
                        objectName: "toolEditorPalette"
                        Layout.preferredWidth: 180
                        model: app.colorPalettes.map(function(p) { return p.name })
                        currentIndex: {
                            const ps = app.colorPalettes
                            for (let i = 0; i < ps.length; ++i) if (ps[i].id === app.colorPalette) return i
                            return 0
                        }
                        onActivated: function(i) { app.colorPalette = app.colorPalettes[i].id }
                    }
                }
                Grid {
                    columns: 4
                    Layout.fillWidth: true
                    Repeater {
                        model: editor.palette ? editor.palette.roles : []
                        delegate: AbstractButton {
                            id: roleCell
                            required property var modelData
                            objectName: "editorRole_" + modelData.key
                            readonly property color c: editor.highlights ? modelData.highlight : modelData.ink
                            readonly property bool current: editor.entry.role === modelData.key
                            width: (body.width) / 4
                            height: 52
                            focusPolicy: Qt.NoFocus
                            Accessible.name: modelData.name
                            ToolTip.visible: hovered
                            ToolTip.text: modelData.name
                            ToolTip.delay: 600
                            onClicked: editor.set({ role: modelData.key, color: String(c) })
                            contentItem: Item {
                                Rectangle {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    y: 2
                                    width: 34; height: 26; radius: 8
                                    color: "#ffffff"
                                    border.width: roleCell.current ? 3 : 1
                                    border.color: roleCell.current ? Material.accentColor : "#c9ccd1"
                                    Rectangle {
                                        anchors.centerIn: parent
                                        width: editor.highlights ? 24 : 16
                                        height: editor.highlights ? 10 : 16
                                        radius: editor.highlights ? 2 : 8
                                        color: roleCell.c
                                        opacity: editor.highlights ? 0.6 : 1
                                    }
                                }
                                Label {
                                    y: 30
                                    width: parent.width
                                    horizontalAlignment: Text.AlignHCenter
                                    text: roleCell.modelData.name
                                    elide: Text.ElideRight
                                    font.pixelSize: 10
                                    color: roleCell.current ? Material.accentColor : "#5f6368"
                                }
                            }
                        }
                    }
                }
                // The colors used lately, and another one
                Flow {
                    objectName: "toolEditorRecent"
                    Layout.fillWidth: true
                    spacing: 4
                    Repeater {
                        model: win.toolGroups.recentColors.slice(0, 8)
                        delegate: AbstractButton {
                            required property string modelData
                            objectName: "editorRecent_" + index
                            required property int index
                            width: 28; height: 28
                            focusPolicy: Qt.NoFocus
                            onClicked: editor.set({ color: modelData })
                            contentItem: Rectangle {
                                radius: 14
                                color: parent.modelData
                                border.width: editor.entry.role === "" && Qt.colorEqual(editor.entry.color || "transparent", parent.modelData) ? 3 : 1
                                border.color: border.width > 1 ? Material.accentColor : "#40000000"
                            }
                        }
                    }
                    TextField {
                        id: hexField
                        objectName: "toolEditorHex"
                        width: 92
                        height: 32
                        font.pixelSize: 13
                        placeholderText: "#rrggbb"
                        text: String(editor.shownColor)
                        selectByMouse: true
                        validator: RegularExpressionValidator { regularExpression: /#?[0-9a-fA-F]{6}/ }
                        onAccepted: editor.set({ color: text.charAt(0) === "#" ? text : "#" + text })
                    }
                    ToolButton {
                        objectName: "toolEditorPicker"
                        icon.source: app.iconUrl("xqt-palette")
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Another color…")
                        onClicked: entryColorDialog.open()
                    }
                }
            }
            // A sticky note's pastel
            Flow {
                objectName: "toolEditorNoteColors"
                visible: editor.type === "sticky"
                Layout.fillWidth: true
                spacing: 6
                Repeater {
                    model: app.stickyNoteColors
                    delegate: AbstractButton {
                        required property color modelData
                        required property int index
                        objectName: "editorNoteColor_" + index
                        width: 36; height: 36
                        focusPolicy: Qt.NoFocus
                        onClicked: editor.set({ color: String(modelData) })
                        contentItem: Rectangle {
                            radius: 4
                            color: parent.modelData
                            border.width: Qt.colorEqual(editor.entry.color || "transparent", parent.modelData) ? 3 : 1
                            border.color: border.width > 1 ? Material.accentColor : "#40000000"
                        }
                    }
                }
            }

            // --- the width ---
            ColumnLayout {
                objectName: "toolEditorWidth"
                visible: editor.hasWidth
                Layout.fillWidth: true
                spacing: 0
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: editor.type === "eraser" ? qsTr("Size") : qsTr("Width"); Layout.fillWidth: true; color: "#5f6368" }
                    Label {
                        objectName: "toolEditorWidthValue"
                        text: editor.mm(editor.entry.width || 1)
                        font.weight: Font.DemiBold
                    }
                }
                Slider {
                    id: widthSlider
                    objectName: "toolEditorWidthSlider"
                    Layout.fillWidth: true
                    from: 0
                    to: 1
                    value: editor.widthToSlider(editor.entry.width || 1)
                    onMoved: editor.set({ width: editor.sliderToWidth(value) })
                }
                // The five sizes as dots
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 0
                    Repeater {
                        model: editor.sizes
                        delegate: AbstractButton {
                            required property real modelData
                            required property int index
                            objectName: "editorSize_" + index
                            Layout.fillWidth: true
                            height: 32
                            focusPolicy: Qt.NoFocus
                            ToolTip.visible: hovered
                            ToolTip.text: editor.mm(modelData)
                            ToolTip.delay: 600
                            onClicked: editor.set({ width: modelData })
                            contentItem: Item {
                                Rectangle {
                                    anchors.centerIn: parent
                                    readonly property real d: 4 + index * 3
                                    width: d; height: d; radius: d / 2
                                    color: Math.abs((editor.entry.width || 0) - modelData) < 0.01 ? Material.accentColor : "#5f6368"
                                }
                            }
                        }
                    }
                }
            }

            // --- the line style ---
            RowLayout {
                objectName: "toolEditorLineStyles"
                visible: editor.hasLineStyle
                Layout.fillWidth: true
                spacing: 2
                Label { text: qsTr("Line"); Layout.fillWidth: true; color: "#5f6368" }
                Repeater {
                    model: [{ key: "plain", name: qsTr("Solid") }, { key: "dash", name: qsTr("Dashed") },
                            { key: "dashdot", name: qsTr("Dash-dot") }, { key: "dot", name: qsTr("Dotted") }]
                    delegate: AbstractButton {
                        id: styleButton
                        required property var modelData
                        objectName: "editorLineStyle_" + modelData.key
                        implicitWidth: 44
                        implicitHeight: 36
                        focusPolicy: Qt.NoFocus
                        readonly property bool current: (editor.entry.lineStyle || "plain") === modelData.key
                        onClicked: editor.set({ lineStyle: modelData.key })
                        ToolTip.visible: hovered
                        ToolTip.text: modelData.name
                        ToolTip.delay: 600
                        contentItem: Item {
                            Rectangle {
                                anchors.centerIn: parent
                                width: 40; height: 30; radius: 6
                                color: styleButton.current ? "#e0e3f5" : "transparent"
                                border.width: styleButton.current ? 1 : 0
                                border.color: Material.accentColor
                            }
                            Canvas {
                                objectName: "lineStyleSample"
                                anchors.centerIn: parent
                                width: 32; height: 12
                                onPaint: {
                                    const ctx = getContext("2d")
                                    ctx.reset()
                                    const key = styleButton.modelData.key
                                    ctx.lineWidth = 2.5
                                    ctx.lineCap = LineStyles.sampleCap(key, "round")
                                    ctx.strokeStyle = "#303030"
                                    ctx.setLineDash(LineStyles.sampleDashes(key, ctx.lineWidth))
                                    ctx.beginPath()
                                    ctx.moveTo(2.5, height / 2)
                                    ctx.lineTo(width - 2.5, height / 2)
                                    ctx.stroke()
                                }
                            }
                        }
                    }
                }
            }

            // --- the filling ---
            ColumnLayout {
                objectName: "toolEditorFill"
                visible: editor.hasFill
                Layout.fillWidth: true
                spacing: 2
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: qsTr("Fill"); Layout.fillWidth: true; color: "#5f6368" }
                    Switch {
                        objectName: "toolEditorFillSwitch"
                        checked: editor.entry.fill ? editor.entry.fill.on : false
                        focusPolicy: Qt.NoFocus
                        onToggled: editor.set({ fill: { on: checked } })
                    }
                }
                RowLayout {
                    visible: editor.entry.fill ? editor.entry.fill.on : false
                    Layout.fillWidth: true
                    // The line's color, or another (the pen only: upstream keeps no other for the highlighter)
                    Button {
                        objectName: "toolEditorFillSame"
                        flat: true
                        checkable: true
                        checked: !editor.entry.fill || editor.entry.fill.color === ""
                        text: qsTr("The line's color")
                        font.pixelSize: 12
                        onClicked: editor.set({ fill: { color: "" } })
                    }
                    Button {
                        objectName: "toolEditorFillOther"
                        visible: !editor.highlights
                        flat: true
                        checkable: true
                        checked: !!(editor.entry.fill && editor.entry.fill.color !== "")
                        text: qsTr("Another…")
                        font.pixelSize: 12
                        onClicked: fillColorDialog.open()
                    }
                    Rectangle {
                        visible: !!(editor.entry.fill && editor.entry.fill.color !== "")
                        width: 20; height: 20; radius: 10
                        color: editor.entry.fill && editor.entry.fill.color !== "" ? editor.entry.fill.color : "transparent"
                        border.width: 1
                        border.color: "#40000000"
                    }
                }
                RowLayout {
                    visible: editor.entry.fill ? editor.entry.fill.on : false
                    Layout.fillWidth: true
                    Label { text: qsTr("Opacity"); color: "#5f6368" }
                    Slider {
                        objectName: "toolEditorFillOpacity"
                        Layout.fillWidth: true
                        from: 1
                        to: 255
                        value: editor.entry.fill ? editor.entry.fill.alpha : 128
                        onMoved: editor.set({ fill: { alpha: Math.round(value) } })
                    }
                    Label { text: Math.round((editor.entry.fill ? editor.entry.fill.alpha : 128) / 2.55) + " %"; color: "#5f6368" }
                }
            }

            // --- the font of a text box ---
            ColumnLayout {
                objectName: "toolEditorFont"
                visible: editor.type === "text"
                Layout.fillWidth: true
                spacing: 4
                Label { text: qsTr("Font"); color: "#5f6368" }
                ComboBox {
                    objectName: "toolEditorFontFamily"
                    Layout.fillWidth: true
                    model: editor.opened && editor.type === "text" ? app.fontFamilies() : []
                    currentIndex: model.indexOf ? model.indexOf(editor.entry.font ? editor.entry.font.family : "") : -1
                    onActivated: editor.set({ font: { family: currentText } })
                }
                RowLayout {
                    Label { text: qsTr("Size"); Layout.fillWidth: true }
                    SpinBox {
                        objectName: "toolEditorFontSize"
                        from: 4; to: 200
                        editable: true
                        value: Math.round(editor.entry.font ? editor.entry.font.size : 12)
                        onValueModified: editor.set({ font: { size: value } })
                    }
                }
            }

            // A new tool: add it (an entry of the toolbox is written as it is changed)
            RowLayout {
                visible: editor.adding
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                Button {
                    objectName: "toolEditorCancel"
                    flat: true
                    text: qsTr("Cancel")
                    onClicked: editor.close()
                }
                Button {
                    objectName: "toolEditorAdd"
                    highlighted: true
                    text: qsTr("Add")
                    onClicked: editor.add()
                }
            }
        }
    }

    ColorDialog {
        id: entryColorDialog
        selectedColor: editor.shownColor
        onAccepted: editor.set({ color: String(selectedColor) })
    }
    ColorDialog {
        id: fillColorDialog
        onAccepted: editor.set({ fill: { color: String(selectedColor) } })
    }
}
