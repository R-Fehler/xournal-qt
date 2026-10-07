// xournal-qt: the window's insets (Main.qml's state, `win.insets`; qt/docs/adaptive-layout.md, "Safe areas" and
// "The soft keyboard"): the safe area, where the controls over the pages may go, the bottom sheets of the phone classes
// and the soft keyboard. The window's root keeps safeTop/Right/Bottom/Left, fakeKeyboardHeight, keyboardTop and
// keyboardHeight as aliases: main.cpp and the tests set and read them there.
import QtQuick
import QtQuick.Window

QtObject {
    id: insets
    objectName: "safeInsets"
    /// The window whose insets these are
    required property Window window
    /// Room at the right side of the content that the controls keep clear of (the presenter's panel)
    property real rightReserve: 0

    /// The parts of the window under the system's bars and a camera cut-out (edge to edge on Android and iOS; set by
    /// main.cpp from the window's safe area margins on Qt 6.9+, 0 elsewhere; the tests set them by hand): the status
    /// bar at the top, the navigation or gesture bar at the bottom, a cut-out at a side when the phone is held
    /// sideways. The controls stay clear of them; the pages are drawn under them (edge to edge).
    property real top: 0
    property real right: 0
    property real bottom: 0
    property real left: 0

    /// Where the controls over the pages may go, in the content item's coordinates (the pages under the header and
    /// above the footer): clear of the safe area's insets, and above the soft keyboard
    readonly property real controlsLeft: left
    readonly property real controlsRight: window.contentItem.width - right - rightReserve
    readonly property real controlsTop: Math.max(0, top - window.contentItem.y)
    readonly property real controlsBottom: Math.min(window.contentItem.height, window.height - bottom - window.contentItem.y,
                                                    keyboardTop - window.contentItem.y)
    /// How much of the content item's bottom lies under the bottom inset (0 where the footer took it: the dock, the
    /// tool bar at the bottom, the room for the keyboard)
    readonly property real contentBottomInset: Math.max(0, window.contentItem.y + window.contentItem.height
                                                           - (window.height - bottom))

    /// A bottom sheet of the phone classes (the overlay's coordinates): as wide as the safe area (at most 640 px) and
    /// centred in it, resting on the soft keyboard while it is open, else on the window's edge with room for the
    /// navigation bar below its content (MenuSheet, the page menu, the palette and the widths)
    readonly property real sheetWidth: Math.min(window.width - left - right, 640)
    readonly property real sheetX: left + Math.round((window.width - left - right - sheetWidth) / 2)
    readonly property real sheetBottom: keyboardTop
    readonly property real sheetBottomPadding: keyboardOpen ? 0 : bottom

    /// A soft keyboard of this height at the window's bottom instead of the real one (the tests; XQT_FAKE_KEYBOARD)
    property real fakeKeyboardHeight: 0
    /// The top of the soft keyboard in the window, while it is open (else the window's height). Android reports the
    /// keyboard in the screen's pixels. Where the platform makes the window smaller instead, it is the window's height.
    readonly property real keyboardTop: {
        if (fakeKeyboardHeight > 0) return window.height - fakeKeyboardHeight
        const r = Qt.inputMethod.keyboardRectangle
        if (!Qt.inputMethod.visible || r.height <= 0) return window.height
        const ratio = Qt.platform.os === "android" && window.screen ? window.screen.devicePixelRatio : 1
        return Math.max(0, Math.min(window.height, r.y / ratio))
    }
    /// How much of the window the keyboard covers from below (0: none). The footer makes room for it: the pages, the
    /// Markdown source and the pills end above it (as Android's adjustResize would), the dock goes while it is open,
    /// and on a phone the format bar sits right above it.
    readonly property real keyboardHeight: Math.max(0, window.height - keyboardTop)
    readonly property bool keyboardOpen: keyboardHeight > 0
}
