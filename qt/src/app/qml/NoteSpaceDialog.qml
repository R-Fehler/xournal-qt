// Space for notes beside slides (qt/docs/note-space.md): the page grows by the amounts on any of its sides, the slide
// stays as it is (the PDF drawn at its place, the ink with it). Amounts in % of the slide or in cm, presets, a
// preview to scale, and which pages. One undo step; all 0 takes the space away again.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Dialog {
    id: dlg
    objectName: "noteSpaceDialog"
    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    title: qsTr("Space for notes")
    width: Math.min(parent ? parent.width * 0.94 : 560, 560)

    /// 0-based pages it was opened for (the page menu: the page or the selection)
    property var pages: []
    /// 0: these pages, 1: all pages, 2: all pages with a PDF background
    property int scope: 0
    /// Amounts in % of the slide (else in mm, shown as cm)
    property bool percent: true
    /// The slide of the first page (points), for the preview and for converting units
    property real slideWidth: 842
    property real slideHeight: 474
    readonly property var targets: visible ? app.noteSpacePages(scope, pages) : []

    readonly property real mmPerPoint: 25.4 / 72

    function openFor(list, allPages) {
        pages = list && list.length > 0 ? list : [app.pageNumber - 1]
        scope = allPages ? (app.pagesHavePdfBackground(app.noteSpacePages(1, [])) ? 2 : 1) : 0
        const s = app.noteSpaceOf(pages[0])
        slideWidth = s.slideWidth > 0 ? s.slideWidth : 842
        slideHeight = s.slideHeight > 0 ? s.slideHeight : 474
        percent = true
        setPoints(s.left || 0, s.top || 0, s.right || 0, s.bottom || 0)
        open()
    }
    // Amounts in points into the boxes (in the unit shown)
    function setPoints(l, t, r, b) {
        leftBox.value = toUnit(l, slideWidth)
        topBox.value = toUnit(t, slideHeight)
        rightBox.value = toUnit(r, slideWidth)
        bottomBox.value = toUnit(b, slideHeight)
    }
    function toUnit(points, of) { return Math.round(percent ? points / of * 100 : points * mmPerPoint) }
    function toPoints(v, of) { return percent ? v / 100 * of : v / mmPerPoint }
    function setUnit(toPercent) {
        if (toPercent === percent) return
        const l = toPoints(leftBox.value, slideWidth), t = toPoints(topBox.value, slideHeight)
        const r = toPoints(rightBox.value, slideWidth), b = toPoints(bottomBox.value, slideHeight)
        percent = toPercent
        setPoints(l, t, r, b)
    }
    function preset(l, t, r, b) {  // fractions of the slide
        setPoints(l * slideWidth, t * slideHeight, r * slideWidth, b * slideHeight)
    }
    function apply() {
        if (percent) {
            app.applyNoteSpace(scope, pages, leftBox.value / 100, topBox.value / 100, rightBox.value / 100,
                               bottomBox.value / 100, true)
        } else {
            app.applyNoteSpace(scope, pages, leftBox.value / mmPerPoint, topBox.value / mmPerPoint,
                               rightBox.value / mmPerPoint, bottomBox.value / mmPerPoint, false)
        }
        dlg.close()
    }

    component Amount: SpinBox {
        from: 0
        to: dlg.percent ? 400 : 1000
        stepSize: 5
        editable: true
        Layout.preferredWidth: 150
        textFromValue: function(v, locale) {
            return dlg.percent ? v + " %" : Number(v / 10).toLocaleString(locale, 'f', 1) + " cm"
        }
        valueFromText: function(text, locale) {
            const n = Number.fromLocaleString(locale, text.replace(/[^0-9.,]/g, "").replace(",", locale.decimalPoint))
            return isNaN(n) ? 0 : Math.round(dlg.percent ? n : n * 10)
        }
    }

    ColumnLayout {
        width: dlg.availableWidth
        spacing: 10

        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            color: "#555a60"
            text: qsTr("The page grows around the slide; the slide and what is written on it stay together.")
        }
        Flow {
            Layout.fillWidth: true
            spacing: 6
            Button { objectName: "noteSpacePresetRight"; text: qsTr("Half the width on the right"); flat: true; onClicked: dlg.preset(0, 0, 0.5, 0) }
            Button { objectName: "noteSpacePresetBelow"; text: qsTr("Below: as high as the slide"); flat: true; onClicked: dlg.preset(0, 0, 0, 1) }
            Button { objectName: "noteSpacePresetNone"; text: qsTr("None"); flat: true; onClicked: dlg.preset(0, 0, 0, 0) }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            // The amounts around a small picture of the page, where they go
            GridLayout {
                columns: 2
                rowSpacing: 4
                columnSpacing: 8
                Label { text: qsTr("Left") }
                Amount { id: leftBox; objectName: "noteSpaceLeft" }
                Label { text: qsTr("Right") }
                Amount { id: rightBox; objectName: "noteSpaceRight" }
                Label { text: qsTr("Top") }
                Amount { id: topBox; objectName: "noteSpaceTop" }
                Label { text: qsTr("Bottom") }
                Amount { id: bottomBox; objectName: "noteSpaceBottom" }
            }
            // Preview to scale: the page, the slide in it
            Item {
                id: preview
                objectName: "noteSpacePreview"
                Layout.fillWidth: true
                Layout.preferredHeight: 150
                readonly property real l: dlg.toPoints(leftBox.value, dlg.slideWidth)
                readonly property real r: dlg.toPoints(rightBox.value, dlg.slideWidth)
                readonly property real t: dlg.toPoints(topBox.value, dlg.slideHeight)
                readonly property real b: dlg.toPoints(bottomBox.value, dlg.slideHeight)
                readonly property real pageW: l + dlg.slideWidth + r
                readonly property real pageH: t + dlg.slideHeight + b
                readonly property real scale: Math.min(width / pageW, height / pageH)
                Rectangle {
                    id: paper
                    anchors.centerIn: parent
                    width: preview.pageW * preview.scale
                    height: preview.pageH * preview.scale
                    color: "#ffffff"
                    border.width: 1
                    border.color: "#c4c8cd"
                    Rectangle {
                        objectName: "noteSpaceSlide"
                        x: preview.l * preview.scale
                        y: preview.t * preview.scale
                        width: dlg.slideWidth * preview.scale
                        height: dlg.slideHeight * preview.scale
                        color: "#dfe9f7"
                        border.width: 1
                        border.color: "#8fa9cc"
                        Label { anchors.centerIn: parent; text: qsTr("Slide"); color: "#4a6285"; font.pixelSize: 11 }
                    }
                }
            }
        }
        RowLayout {
            spacing: 8
            Label { text: qsTr("Amounts in") }
            ButtonGroup { id: units }
            Button { objectName: "noteSpacePercent"; text: qsTr("% of the slide"); checkable: true; checked: dlg.percent; flat: true; ButtonGroup.group: units; onClicked: dlg.setUnit(true) }
            Button { objectName: "noteSpaceCm"; text: qsTr("cm"); checkable: true; checked: !dlg.percent; flat: true; ButtonGroup.group: units; onClicked: dlg.setUnit(false) }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Label { text: qsTr("For") }
            ComboBox {
                objectName: "noteSpaceScope"
                Layout.fillWidth: true
                model: [dlg.pages.length > 1 ? qsTr("The %1 selected pages").arg(dlg.pages.length)
                                             : qsTr("This page (%1)").arg(dlg.pages.length ? dlg.pages[0] + 1 : ""),
                        qsTr("All pages"), qsTr("All pages with a PDF background")]
                currentIndex: dlg.scope
                onActivated: dlg.scope = currentIndex
            }
            Label { text: dlg.targets.length === 1 ? qsTr("1 page") : qsTr("%1 pages").arg(dlg.targets.length); color: "#555a60" }
        }
    }

    footer: DialogButtonBox {
        Button {
            objectName: "noteSpaceBlankPages"
            text: qsTr("A blank page after each instead")
            flat: true
            DialogButtonBox.buttonRole: DialogButtonBox.ActionRole
            onClicked: { app.insertBlankAfterPages(dlg.scope, dlg.pages); dlg.close() }
        }
        Button { text: qsTr("Cancel"); flat: true; DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
        Button { objectName: "noteSpaceApply"; text: qsTr("Apply"); highlighted: true; DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole }
        onAccepted: dlg.apply()
        onRejected: dlg.close()
    }
}
