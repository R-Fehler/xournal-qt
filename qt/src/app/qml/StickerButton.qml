// The sticker tool (qt/docs/stickers.md): a tap opens the sticker picker (the library's stickers and those of all
// libraries; a tap on one pastes it on the page, selected); a long press (or a right click) saves what is selected as
// a sticker. Self-contained: it brings its picker and its dialog along, so the tool bar, the phone's tool sheet or a
// toolbox can each place it as a button of its own.
import QtQuick
import QtQuick.Controls

IconButton {
    id: button
    objectName: "stickerButton"
    /// The side of the bar it is on ("top", "bottom", "left", "right"): the picker opens away from it
    property string popupSide: "top"
    readonly property alias picker: stickerPicker
    iconName: "xqt-sticker"
    label: qsTr("Stickers")
    tip: qsTr("Stickers: paste saved content (hold: save the selection as a sticker)")
    ownHold: true
    /// What its long press does (in its menu on a bar)
    readonly property string holdText: qsTr("Save the selection as a sticker…")
    onClicked: stickerPicker.open()
    onPressAndHold: stickerPicker.saveSelection()
    TapHandler {
        acceptedButtons: Qt.RightButton
        acceptedDevices: PointerDevice.Mouse  // not a finger: touch has no buttons
        onTapped: stickerPicker.saveSelection()
    }
    StickerPicker {
        id: stickerPicker
        owner: button
        ownerX: button.popupSide === "left" ? button.width : button.popupSide === "right" ? -width : 0
        ownerY: button.popupSide === "bottom" ? -height : button.popupSide === "top" ? button.height : 0
    }
}
