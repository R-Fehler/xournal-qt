import QtQuick

// A page in a list of pages: its sketch at once (drawn in advance, kept in memory, see PageSketches), the sharp
// thumbnail on top of it when that is drawn. While the list races through its pages no sharp one is asked for (it
// would be scrolled away before it is drawn); once asked for, it stays.
Item {
    id: picture
    property string sketch
    property string thumbnail
    property int sourceWidth: 160
    readonly property alias sourceSize: sharp.sourceSize
    property bool racing: false
    property bool sharpWanted: false
    readonly property bool sharpShown: sharp.status === Image.Ready

    Component.onCompleted: sharpWanted = !racing
    onRacingChanged: if (!racing) sharpWanted = true

    Image {
        objectName: "pageSketch"
        anchors.fill: parent
        visible: !picture.sharpShown
        source: picture.sketch
        cache: false
        fillMode: Image.PreserveAspectFit
        smooth: true
    }
    Image {
        id: sharp
        objectName: "pageSharp"
        anchors.fill: parent
        source: picture.sharpWanted ? picture.thumbnail : ""
        asynchronous: true
        cache: false
        sourceSize.width: picture.sourceWidth
        fillMode: Image.PreserveAspectFit
        smooth: true
    }
}
