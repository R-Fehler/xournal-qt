// The introduction (qt/docs/onboarding.md): a few short pages, swiped or stepped through with Next, that end in the
// question how documents are kept (DocumentModeCards, as the first-start question and Settings → Documents have
// them). Shown once at the first start in place of that question (app.askIntro), and again from Help (⋮ → Help,
// Settings → Help). An AdaptiveDialog: in the middle of a desktop or tablet window, the whole screen of a phone.
//   - first start: Skip goes to the last page (the choice has to be made); only Continue closes it and stores the
//     choice; nothing else closes it (× on a phone goes to the last page too);
//   - later: Skip, Esc and × close it; Done stores the way chosen on the last page (if it changed).
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

AdaptiveDialog {
    id: intro
    objectName: "introDialog"
    preferredWidth: 560
    closePolicy: firstStart ? Popup.NoAutoClose : Popup.CloseOnEscape
    title: titles[pages.currentIndex] || ""

    /// Shown at the first start (the way to keep documents is not chosen yet)
    property bool firstStart: false
    /// The first start's choice is stored (Continue)
    property bool finished: false
    /// The first start is over (the window goes on with what waited: the recovery question)
    signal chosen()

    readonly property var titles: [qsTr("Write on notes and PDFs"), qsTr("Markdown: text that stays text"),
        qsTr("Your folders are your library"), qsTr("How do you want to keep your documents?")]
    readonly property int lastPage: titles.length - 1
    readonly property bool atLastPage: pages.currentIndex === lastPage

    /// At the first start: from the first page, the recommendation chosen
    function openFirstStart() {
        firstStart = true
        finished = false
        pages.setCurrentIndex(0)
        modeCards.mode = "pdf"  // (the recommendation)
        open()
    }
    /// From Help: from the first page, the way documents are kept now chosen
    function show() {
        firstStart = false
        pages.setCurrentIndex(0)
        modeCards.mode = app.documentMode
        open()
    }
    function skip() {
        if (firstStart)
            pages.setCurrentIndex(lastPage)
        else
            close()
    }
    function finish() {
        if (firstStart || modeCards.mode !== app.documentMode)
            app.documentMode = modeCards.mode
        app.introSeen = true
        finished = true
        close()
        if (firstStart)
            chosen()
    }
    // (the keys: Esc closes it from Help, also when it was opened as another sheet closed)
    onOpened: forceActiveFocus()
    onClosed: if (!firstStart) app.introSeen = true
    // × of the full-screen sheet at the first start: to the choice instead (it has to be made)
    onRejected: {
        if (firstStart && !finished)
            Qt.callLater(function() { pages.setCurrentIndex(intro.lastPage); intro.open() })
    }

    // --- the parts of a page ---
    component Heading: Label {
        Layout.fillWidth: true
        wrapMode: Text.Wrap
        font.pixelSize: 15
        font.weight: Font.DemiBold
    }
    component Para: Label {
        Layout.fillWidth: true
        wrapMode: Text.Wrap
        textFormat: Text.StyledText
        lineHeight: 1.1
    }
    /// A row of the app's icons: what the page is about
    component Icons: Row {
        property var names: []
        Layout.alignment: Qt.AlignHCenter
        Layout.bottomMargin: 4
        spacing: 14
        Repeater {
            model: parent.names
            delegate: Image {
                required property string modelData
                source: app.iconUrl(modelData)
                sourceSize.width: 32
                sourceSize.height: 32
                width: 32
                height: 32
            }
        }
    }

    ColumnLayout {
        width: intro.availableWidth
        spacing: 12

        SwipeView {
            id: pages
            objectName: "introPages"
            Layout.fillWidth: true
            // (as high as the highest page, so the dialog does not jump between pages)
            Layout.preferredHeight: Math.max(page0.implicitHeight, page1.implicitHeight, page2.implicitHeight,
                                             page3.implicitHeight)
            clip: true

            // 1. Notes, and PDFs that can be written on
            ColumnLayout {
                id: page0
                objectName: "introPage0"
                spacing: 10
                Icons { names: ["xopp-tool-pencil", "xopp-tool-highlighter", "xopp-tool-eraser", "xopp-select-lasso",
                                "xqt-mark-text"] }
                Para {
                    text: qsTr("Xournal Qt is made for writing with a pen: on pages of notes, and on PDFs.")
                }
                Heading { text: qsTr("PDFs are not read-only here") }
                Para {
                    text: qsTr("Write and draw on any PDF, highlight or underline its text, add blank pages or room "
                               + "for notes beside the slides, and move or delete pages.")
                }
                Heading { text: qsTr("Everything stays editable") }
                Para {
                    text: qsTr("What you write can be selected later, moved, resized or erased, also after the "
                               + "document was saved and opened again. With a pen, the pen writes and a finger "
                               + "scrolls and zooms.")
                }
            }

            // 2. Markdown documents, and a Markdown text inside a PDF
            ColumnLayout {
                id: page1
                objectName: "introPage1"
                spacing: 10
                Icons { names: ["xqt-markdown", "xqt-page-text", "xopp-document-export-pdf"] }
                Para {
                    text: qsTr("Type formatted text in Markdown: headings, lists, tables, formulas and pictures, "
                               + "shown formatted while you type. The formatting bar helps if you do not know the "
                               + "syntax.")
                }
                Heading { text: qsTr("Markdown files") }
                Para {
                    text: qsTr("A <b>.md</b> file opens as a document of its own and stays a plain text file, "
                               + "for other Markdown apps too.")
                }
                Heading { text: qsTr("A text inside a PDF") }
                Para {
                    text: qsTr("A text document can also be a <b>PDF with notes</b>: write on it with the pen and "
                               + "keep editing the text with the keyboard. Other PDF apps show it as a normal PDF, "
                               + "and the Markdown travels inside. <b>⋮ → Document → Open as PDF document</b> makes "
                               + "one from a .md; the .md stays as it is.")
                }
            }

            // 3. Libraries (any folder) and search
            ColumnLayout {
                id: page2
                objectName: "introPage2"
                spacing: 10
                Icons { names: ["xqt-library", "xqt-folder-tree", "xqt-search", "xqt-tabs-grid"] }
                Heading { text: qsTr("Any folder can be a library") }
                Para {
                    text: qsTr("Your documents stay ordinary files in your folders: nothing is imported, and any "
                               + "sync app can keep them. The app starts with a library of its own in your "
                               + "Documents folder; open another folder from the menu under the library's name.")
                }
                Heading { text: qsTr("Search on three levels") }
                Para {
                    text: qsTr("This document (Ctrl+F), all open documents, or the whole library, the text of "
                               + "PDFs included. Hits are marked on the pages. Turn on <b>Fuzzy</b> to find words "
                               + "with letters left out or mistyped.")
                }
                Heading { text: qsTr("Tabs") }
                Para {
                    text: qsTr("Keep several documents open in tabs, see them all at once, and put one beside "
                               + "another as a reference.")
                }
            }

            // 4. The way documents are kept (the first-start question)
            ColumnLayout {
                id: page3
                objectName: "introPage3"
                spacing: 12
                DocumentModeCards {
                    id: modeCards
                    objectName: "introModeCards"
                    Layout.fillWidth: true
                    selfSelect: true
                }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    color: "#6b6f75"
                    font.pixelSize: 13
                    text: qsTr("You can change this later in Settings → Documents. Copies for Xournal++ are always "
                               + "available through Share → “For Xournal++”.")
                }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    color: "#6b6f75"
                    font.pixelSize: 13
                    text: qsTr("Help (⋮ → Help, or Settings → Help) has a tutorial to try everything on, this "
                               + "introduction and the keyboard shortcuts.")
                }
            }
        }

        PageIndicator {
            objectName: "introPageIndicator"
            Layout.alignment: Qt.AlignHCenter
            count: pages.count
            currentIndex: pages.currentIndex
        }
    }

    footer: Item {
        implicitHeight: buttons.implicitHeight + 24
        RowLayout {
            id: buttons
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: intro.fullScreen ? 8 : 16
            anchors.rightMargin: intro.fullScreen ? 8 : 16
            spacing: 4
            Button {
                objectName: "introSkip"
                flat: true
                visible: !intro.atLastPage
                text: qsTr("Skip")
                onClicked: intro.skip()
            }
            Item { Layout.fillWidth: true }
            Button {
                objectName: "introBack"
                flat: true
                visible: pages.currentIndex > 0
                text: qsTr("Back")
                onClicked: pages.decrementCurrentIndex()
            }
            Button {
                objectName: "introNext"
                highlighted: true
                text: !intro.atLastPage ? qsTr("Next") : intro.firstStart ? qsTr("Continue") : qsTr("Done")
                onClicked: intro.atLastPage ? intro.finish() : pages.incrementCurrentIndex()
            }
        }
    }
}
