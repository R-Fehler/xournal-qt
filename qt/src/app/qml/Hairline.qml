import QtQuick
import "DevicePixels.js" as DevicePixels

// A line a whole number of device pixels thin: one at 100 % to 175 %, two at 200 % (DevicePixels.js,
// qt/docs/features/hidpi.md). Its width (vertical lines) or height (horizontal ones) is `thickness`; the other side is
// set where it is used.
Rectangle {
    readonly property real thickness: DevicePixels.whole(1, Screen.devicePixelRatio)
    implicitWidth: thickness
    implicitHeight: thickness
}
