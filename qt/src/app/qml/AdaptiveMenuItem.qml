// An entry of an AdaptiveMenu (qt/docs/features/adaptive-layout.md, "Menus").
// `offered` says whether the entry is there at all. (Not `visible`: every entry of a closed menu reads as invisible,
// and the sheet of the phone classes asks while the menu itself stays closed.) It is as tall as a finger needs in the
// touch profile (48), else 40.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

MenuItem {
    id: control
    property bool offered: true
    visible: offered
    height: offered ? implicitHeight : 0
    readonly property int rowHeight: win.adaptive.minTarget
    implicitHeight: Math.max(rowHeight, implicitContentHeight + topPadding + bottomPadding,
                             implicitIndicatorHeight + topPadding + bottomPadding)
    verticalPadding: 6
}
