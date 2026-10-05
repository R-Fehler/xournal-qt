// Pages as files (qt/docs/page-files.md), self-contained: the window only places it and opens its dialogs.
// - "Insert pages from a file…": a PDF, a PDF with notes or a .xopp; all its pages, a range, or pages ticked on their
//   pictures; before or after a page. A protected PDF asks for its password here. The pages are inserted as pasted
//   pages are: their text stays searchable, one undo step.
// - "Extract to a new document…": the pages (the selection) as a PDF with notes or a .xopp next to the document,
//   opened in a tab; "Remove them from this document" (one undo step).
// - "Split…": every N pages, at the selected pages or at the chapters, into documents next to it.
// - "Export as pictures…": this page, the selection or all pages as PNG (or JPEG) at a resolution, into a folder,
//   named "name-p003.png"; a transparent background for ink on plain paper.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Dialogs
import QtQuick.Layouts
import QtQuick.Window

Item {
    id: root
    objectName: "pageFiles"
    visible: false

    /// Insert pages from a file next to page `page` (0-based; `after`: after it): the file is chosen first
    function chooseFile(page, after) {
        insertDialog.page = Math.max(0, page)
        insertDialog.after = after
        filePicker.open()
    }
    /// The same with the file chosen (tests, a file dropped)
    function openFile(url, page, after) {
        insertDialog.page = Math.max(0, page)
        insertDialog.after = after
        insertDialog.start(url)
    }
    function openExtract(pages) { extractDialog.openFor(pages) }
    function openSplit(pages) { splitDialog.openFor(pages) }
    function openImages(pages) { imagesDialog.openFor(pages) }

    component Hint: Label {
        Layout.fillWidth: true
        wrapMode: Text.Wrap
        font.pixelSize: 12
        color: "#6b6f75"
    }

    // --- inserting pages from a file ----------------------------------------------------------------------------
    FileDialog {
        id: filePicker
        objectName: "insertFilePicker"
        title: qsTr("Insert pages from a file")
        currentFolder: app.openFolder()
        nameFilters: [qsTr("PDFs and notes (*.pdf *.xopp *.xoj)"), qsTr("PDF files (*.pdf)"),
                      qsTr("Xournal++ files (*.xopp *.xoj)")]
        onAccepted: insertDialog.start(selectedFile)
    }

    AdaptiveDialog {
        id: insertDialog
        objectName: "insertFromFileDialog"
        preferredWidth: 640
        /// The page the new ones go next to (0-based), and where
        property int page: 0
        property bool after: true
        property url file
        /// What app.readPageFile said ({} while it reads)
        property var info: ({})
        property bool reading: false
        /// "all", "range" or "pick"
        property string mode: "all"
        property var picked: []
        readonly property int pages: info.pages || 0
        readonly property string rangeError: mode === "range" ? app.checkPageRange(rangeField.text, pages) : ""
        readonly property bool canInsert: info.ok === true && !reading
                                          && (mode === "all" || (mode === "range" && rangeError === "")
                                              || (mode === "pick" && picked.length > 0))
        title: info.name ? qsTr("Insert pages from “%1”").arg(info.name) : qsTr("Insert pages from a file")

        function start(url) {
            file = url
            info = ({})
            mode = "all"
            picked = []
            rangeField.text = ""
            passwordField.text = ""
            reading = app.readPageFile(url, "")
            if (reading) open()
        }
        function tryPassword() {
            if (passwordField.text === "") return
            reading = app.readPageFile(file, passwordField.text)
            passwordField.text = ""
        }
        function toggle(i) {
            const at = picked.indexOf(i)
            const next = picked.slice()
            if (at >= 0) next.splice(at, 1)
            else next.push(i)
            picked = next
            mode = "pick"
        }
        function insert() {
            if (!canInsert) return
            const position = after ? page + 1 : page
            app.insertPagesFromFile(mode === "range" ? rangeField.text : "", mode === "pick" ? picked : [], position)
            close()
        }
        onClosed: { reading = false; app.closePageFile() }

        Connections {
            target: app
            function onPageFileRead(info) {
                if (!insertDialog.visible) return
                insertDialog.info = info
                insertDialog.reading = false
                if (info.needsPassword) passwordField.forceActiveFocus()
            }
        }

        ColumnLayout {
            width: insertDialog.availableWidth
            spacing: 8
            RowLayout {
                visible: insertDialog.reading
                spacing: 12
                BusyIndicator { running: insertDialog.reading; implicitWidth: 32; implicitHeight: 32 }
                Label { text: qsTr("Reading the file…") }
            }
            Label {
                objectName: "insertFileError"
                visible: !insertDialog.reading && (insertDialog.info.error || "") !== ""
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: "#b3261e"
                text: qsTr("It cannot be inserted: %1").arg(insertDialog.info.error || "")
            }
            // A protected PDF: its password (kept in memory only while the dialog is open)
            ColumnLayout {
                visible: !insertDialog.reading && insertDialog.info.needsPassword === true
                Layout.fillWidth: true
                spacing: 6
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    text: insertDialog.info.wrongPassword ? qsTr("The password is not right. Try again.")
                                                          : qsTr("This PDF is protected with a password.")
                    color: insertDialog.info.wrongPassword ? "#b3261e" : Material.foreground
                }
                RowLayout {
                    Layout.fillWidth: true
                    TextField {
                        id: passwordField
                        objectName: "insertFilePassword"
                        Layout.fillWidth: true
                        echoMode: TextInput.Password
                        placeholderText: qsTr("Password")
                        inputMethodHints: Qt.ImhSensitiveData | Qt.ImhNoPredictiveText
                        Keys.onReturnPressed: insertDialog.tryPassword()
                        Keys.onEnterPressed: insertDialog.tryPassword()
                    }
                    Button {
                        objectName: "insertFileOpenButton"
                        text: qsTr("Open")
                        enabled: passwordField.text !== ""
                        onClicked: insertDialog.tryPassword()
                    }
                }
            }
            // Which pages
            ColumnLayout {
                visible: insertDialog.info.ok === true && !insertDialog.reading
                Layout.fillWidth: true
                spacing: 2
                ButtonGroup { id: which }
                RadioButton {
                    objectName: "insertFileAll"
                    ButtonGroup.group: which
                    checked: insertDialog.mode === "all"
                    text: insertDialog.pages === 1 ? qsTr("Its page") : qsTr("All %1 pages").arg(insertDialog.pages)
                    onClicked: insertDialog.mode = "all"
                }
                RowLayout {
                    Layout.fillWidth: true
                    RadioButton {
                        objectName: "insertFileRangeChoice"
                        ButtonGroup.group: which
                        checked: insertDialog.mode === "range"
                        text: qsTr("Pages")
                        onClicked: { insertDialog.mode = "range"; rangeField.forceActiveFocus() }
                    }
                    TextField {
                        id: rangeField
                        objectName: "insertFileRange"
                        Layout.fillWidth: true
                        placeholderText: qsTr("e.g. 1-3, 5, 8-")
                        inputMethodHints: Qt.ImhPreferNumbers
                        onTextEdited: insertDialog.mode = "range"
                        Keys.onReturnPressed: insertDialog.insert()
                        Keys.onEnterPressed: insertDialog.insert()
                    }
                }
                Hint {
                    visible: insertDialog.mode === "range" && rangeField.text !== "" && insertDialog.rangeError !== ""
                    color: "#b3261e"
                    text: insertDialog.rangeError
                }
                RadioButton {
                    objectName: "insertFilePickChoice"
                    ButtonGroup.group: which
                    checked: insertDialog.mode === "pick"
                    text: insertDialog.picked.length > 0 ? qsTr("The pages ticked below (%1)").arg(insertDialog.picked.length)
                                                         : qsTr("Tick the pages below")
                    onClicked: insertDialog.mode = "pick"
                }
                // The file's pages: tap to tick (pictures as the library's search draws them; a protected file: numbers)
                GridView {
                    id: thumbs
                    objectName: "insertFileGrid"
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.min(2, Math.ceil(count / Math.max(1, Math.floor(width / cellWidth)))) * cellHeight
                    clip: true
                    cellWidth: 104
                    cellHeight: 150
                    model: insertDialog.visible ? insertDialog.pages : 0
                    ScrollBar.vertical: ScrollBar {}
                    delegate: Item {
                        id: cell
                        required property int index
                        readonly property bool ticked: insertDialog.picked.indexOf(index) >= 0
                        objectName: "insertFilePage" + index
                        width: thumbs.cellWidth
                        height: thumbs.cellHeight
                        Rectangle {
                            anchors.fill: parent
                            anchors.margins: 4
                            radius: 4
                            color: "#ffffff"
                            border.width: cell.ticked ? 3 : 1
                            border.color: cell.ticked ? Material.accent : "#c8ccd0"
                            Image {
                                anchors.fill: parent
                                anchors.margins: 4
                                anchors.bottomMargin: 22
                                fillMode: Image.PreserveAspectFit
                                asynchronous: true
                                cache: false  // (the provider keeps its own, with a limit)
                                sourceSize.width: 160
                                source: (insertDialog.info.thumbnails || "") !== "" ? insertDialog.info.thumbnails + "/" + cell.index : ""
                            }
                            Label {
                                anchors.bottom: parent.bottom
                                anchors.horizontalCenter: parent.horizontalCenter
                                anchors.bottomMargin: 3
                                text: (cell.ticked ? "✓ " : "") + (cell.index + 1)
                                font.pixelSize: 12
                            }
                        }
                        TapHandler { onTapped: insertDialog.toggle(cell.index) }
                    }
                }
                ComboBox {
                    objectName: "insertFileWhere"
                    Layout.topMargin: 6
                    Layout.preferredWidth: 220
                    model: [qsTr("before page %1").arg(insertDialog.page + 1), qsTr("after page %1").arg(insertDialog.page + 1)]
                    currentIndex: insertDialog.after ? 1 : 0
                    onActivated: insertDialog.after = currentIndex === 1
                }
                Hint { text: qsTr("PDF pages stay PDF pages: their text can still be searched and selected.") }
            }
        }

        footer: DialogButtonBox {
            Button {
                objectName: "insertFileButton"
                text: qsTr("Insert")
                highlighted: true
                enabled: insertDialog.canInsert
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            }
            Button { text: qsTr("Cancel"); flat: true; DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
            onAccepted: insertDialog.insert()
            onRejected: insertDialog.close()
        }
    }

    // --- extract -------------------------------------------------------------------------------------------------
    AdaptiveDialog {
        id: extractDialog
        objectName: "extractDialog"
        preferredWidth: 520
        property var pages: []
        property var draft: ({})
        title: pages.length === 1 ? qsTr("Extract page %1").arg(pages[0] + 1) : qsTr("Extract %1 pages").arg(pages.length)
        function openFor(list) {
            pages = list.length > 0 ? list : [Math.max(0, app.pageNumber - 1)]
            draft = app.extractDraft(pages)
            if (!draft.offered) return false
            nameField.text = draft.name
            asPdf.checked = draft.asPdf === true || draft.xoppAllowed !== true
            asXopp.checked = !asPdf.checked
            removeBox.checked = false
            open()
            return true
        }
        onOpened: { nameField.forceActiveFocus(); nameField.selectAll() }
        onAccepted: app.extractPages(pages, nameField.text, asPdf.checked, removeBox.checked)

        ColumnLayout {
            width: extractDialog.availableWidth
            spacing: 6
            Label { text: qsTr("Name"); color: "#5f6368"; font.pixelSize: 13 }
            TextField {
                id: nameField
                objectName: "extractName"
                Layout.fillWidth: true
                selectByMouse: true
                Keys.onReturnPressed: if (nameField.text.trim() !== "") extractDialog.accept()
                Keys.onEnterPressed: if (nameField.text.trim() !== "") extractDialog.accept()
            }
            Hint { text: qsTr("A new document in “%1”, opened in a tab.").arg(extractDialog.draft.folder || "") }
            ButtonGroup { id: extractType }
            RadioButton {
                id: asPdf
                objectName: "extractAsPdf"
                ButtonGroup.group: extractType
                text: qsTr("PDF with notes (any PDF app opens it)")
            }
            RadioButton {
                id: asXopp
                objectName: "extractAsXopp"
                ButtonGroup.group: extractType
                enabled: extractDialog.draft.xoppAllowed === true
                text: qsTr("Xournal++ file (.xopp, its PDF pages next to it)")
            }
            Hint {
                visible: extractDialog.draft.protectedDocument === true
                text: qsTr("This document is protected with a password: the new PDF is protected with the same one.")
            }
            CheckBox {
                id: removeBox
                objectName: "extractRemove"
                enabled: extractDialog.pages.length < app.pageCount
                text: extractDialog.pages.length === 1 ? qsTr("Remove the page from this document")
                                                       : qsTr("Remove the pages from this document")
            }
        }
        footer: DialogButtonBox {
            Button {
                objectName: "extractButton"
                text: qsTr("Extract")
                highlighted: true
                enabled: nameField.text.trim() !== ""
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            }
            Button { text: qsTr("Cancel"); flat: true; DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
        }
    }

    // --- split ---------------------------------------------------------------------------------------------------
    AdaptiveDialog {
        id: splitDialog
        objectName: "splitDialog"
        preferredWidth: 540
        title: qsTr("Split the document")
        property var pages: []
        property var draft: ({})
        /// "every", "selected" or "chapters"
        property string mode: "every"
        readonly property bool selectedOffered: pages.some(function(p) { return p > 0 })
        readonly property var plan: visible ? app.splitPlan(mode, everyBox.value, pages) : ({ parts: [], error: "" })
        function openFor(list) {
            pages = list
            draft = app.extractDraft([])
            if (!draft.offered) return false
            mode = selectedOffered ? "selected" : "every"
            everyBox.value = Math.max(1, Math.min(10, Math.ceil(app.pageCount / 2)))
            splitPdf.checked = draft.asPdf === true || draft.xoppAllowed !== true
            splitXopp.checked = !splitPdf.checked
            open()
            return true
        }
        onAccepted: app.splitDocument(mode, everyBox.value, pages, splitPdf.checked)

        ColumnLayout {
            width: splitDialog.availableWidth
            spacing: 4
            ButtonGroup { id: splitHow }
            RowLayout {
                RadioButton {
                    objectName: "splitEveryChoice"
                    ButtonGroup.group: splitHow
                    checked: splitDialog.mode === "every"
                    text: qsTr("Every")
                    onClicked: splitDialog.mode = "every"
                }
                SpinBox {
                    id: everyBox
                    objectName: "splitEveryBox"
                    from: 1
                    to: Math.max(1, app.pageCount)
                    editable: true
                    onValueModified: splitDialog.mode = "every"
                }
                Label { text: everyBox.value === 1 ? qsTr("page") : qsTr("pages") }
            }
            RadioButton {
                objectName: "splitSelectedChoice"
                ButtonGroup.group: splitHow
                enabled: splitDialog.selectedOffered
                checked: splitDialog.mode === "selected"
                text: qsTr("At the selected pages (each starts a document)")
                onClicked: splitDialog.mode = "selected"
            }
            RadioButton {
                objectName: "splitChaptersChoice"
                ButtonGroup.group: splitHow
                enabled: app.outline.available
                checked: splitDialog.mode === "chapters"
                text: qsTr("At the chapters")
                onClicked: splitDialog.mode = "chapters"
            }
            Label {
                objectName: "splitPlanLabel"
                Layout.topMargin: 6
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                font.weight: Font.DemiBold
                color: splitDialog.plan.error !== "" ? "#b3261e" : Material.foreground
                text: splitDialog.plan.error !== "" ? splitDialog.plan.error
                      : qsTr("%1 documents in “%2”:").arg(splitDialog.plan.parts.length).arg(splitDialog.draft.folder || "")
            }
            Repeater {
                model: splitDialog.plan.parts.slice(0, 8)
                delegate: Label {
                    required property var modelData
                    Layout.fillWidth: true
                    elide: Text.ElideMiddle
                    font.pixelSize: 13
                    text: qsTr("%1 (pages %2)").arg(modelData.name).arg(modelData.range)
                }
            }
            Hint {
                visible: splitDialog.plan.parts.length > 8
                text: qsTr("and %1 more").arg(splitDialog.plan.parts.length - 8)
            }
            ButtonGroup { id: splitType }
            RadioButton {
                id: splitPdf
                objectName: "splitAsPdf"
                Layout.topMargin: 6
                ButtonGroup.group: splitType
                text: qsTr("PDFs with notes")
            }
            RadioButton {
                id: splitXopp
                objectName: "splitAsXopp"
                ButtonGroup.group: splitType
                enabled: splitDialog.draft.xoppAllowed === true
                text: qsTr("Xournal++ files (.xopp)")
            }
            Hint { text: qsTr("This document stays as it is.") }
        }
        footer: DialogButtonBox {
            Button {
                objectName: "splitButton"
                text: qsTr("Split")
                highlighted: true
                enabled: splitDialog.plan.parts.length > 1
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            }
            Button { text: qsTr("Cancel"); flat: true; DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
        }
    }

    // --- pages as pictures ---------------------------------------------------------------------------------------
    AdaptiveDialog {
        id: imagesDialog
        objectName: "imageExportDialog"
        preferredWidth: 520
        title: qsTr("Export pages as pictures")
        property var selection: []
        property int current: 0
        /// "page", "selection" or "all"
        property string scope: "page"
        property var draft: ({})
        property url folder
        /// Resolutions offered; the screen's: as the page shows at 100 %
        readonly property int screenDpi: Math.round(Screen.pixelDensity * 25.4 * Screen.devicePixelRatio)
        readonly property var dpis: [72, 96, 150, 200, 300, 400, 600]
        readonly property var exported: scope === "all" ? app.allPages() : scope === "selection" ? selection : [current]
        function openFor(list) {
            draft = app.imageExportDraft()
            if (!draft.offered) return false
            if ((draft.refused || "") !== "") {
                app.message(qsTr("Export pages as pictures"), draft.refused, true)
                return false
            }
            current = Math.max(0, app.pageNumber - 1)
            selection = list.length > 0 ? list : [current]
            scope = selection.length > 1 ? "selection" : "page"
            if (selection.length === 1) current = selection[0]
            folder = draft.folder
            const at = dpis.indexOf(draft.dpi)
            dpiBox.currentIndex = at >= 0 ? at : (draft.dpi === screenDpi ? dpis.length : dpis.indexOf(300))
            pngChoice.checked = true
            transparentBox.checked = false
            open()
            return true
        }
        readonly property int dpi: dpiBox.currentIndex < dpis.length ? dpis[dpiBox.currentIndex] : screenDpi
        onAccepted: app.exportPageImages(exported, folder, dpi, transparentBox.checked && pngChoice.checked,
                                         pngChoice.checked ? "png" : "jpg")

        FolderDialog {
            id: folderPicker
            objectName: "imageFolderPicker"
            title: qsTr("Folder for the pictures")
            currentFolder: imagesDialog.folder
            onAccepted: imagesDialog.folder = selectedFolder
        }

        ColumnLayout {
            width: imagesDialog.availableWidth
            spacing: 4
            ButtonGroup { id: imageScope }
            RadioButton {
                objectName: "imagesThisPage"
                ButtonGroup.group: imageScope
                checked: imagesDialog.scope === "page"
                text: qsTr("Page %1").arg(imagesDialog.current + 1)
                onClicked: imagesDialog.scope = "page"
            }
            RadioButton {
                objectName: "imagesSelection"
                visible: imagesDialog.selection.length > 1
                ButtonGroup.group: imageScope
                checked: imagesDialog.scope === "selection"
                text: qsTr("The %1 selected pages").arg(imagesDialog.selection.length)
                onClicked: imagesDialog.scope = "selection"
            }
            RadioButton {
                objectName: "imagesAllPages"
                ButtonGroup.group: imageScope
                checked: imagesDialog.scope === "all"
                text: qsTr("All %1 pages").arg(app.pageCount)
                onClicked: imagesDialog.scope = "all"
            }
            RowLayout {
                Layout.topMargin: 6
                spacing: 12
                Label { text: qsTr("Resolution") }  // (remembered: also for Copy page as image)
                ComboBox {
                    id: dpiBox
                    objectName: "imagesDpi"
                    Layout.preferredWidth: 220
                    model: imagesDialog.dpis.map(function(d) { return qsTr("%1 dpi").arg(d) })
                                   .concat([qsTr("The screen's (%1 dpi)").arg(imagesDialog.screenDpi)])
                }
            }
            RowLayout {
                spacing: 12
                ButtonGroup { id: imageFormat }
                RadioButton { id: pngChoice; objectName: "imagesPng"; ButtonGroup.group: imageFormat; text: "PNG" }
                RadioButton { objectName: "imagesJpeg"; ButtonGroup.group: imageFormat; text: "JPEG" }
            }
            CheckBox {
                id: transparentBox
                objectName: "imagesTransparent"
                enabled: pngChoice.checked
                text: qsTr("Transparent background (no paper behind the ink)")
            }
            Hint { text: qsTr("PDF pages and background pictures stay. The pictures have the normal colours, also in dark mode. One page is also put on the clipboard.") }
            RowLayout {
                Layout.topMargin: 6
                Layout.fillWidth: true
                Label {
                    objectName: "imagesFolder"
                    Layout.fillWidth: true
                    elide: Text.ElideMiddle
                    text: qsTr("Into %1").arg(decodeURIComponent(imagesDialog.folder.toString().replace(/^file:\/\//, "")))
                }
                Button { objectName: "imagesChooseFolder"; text: qsTr("Choose…"); flat: true; onClicked: folderPicker.open() }
            }
            Hint {
                text: qsTr("Named “%1”, … by their page.").arg((imagesDialog.draft.name || "") + "-p001." + (pngChoice.checked ? "png" : "jpg"))
            }
        }
        footer: DialogButtonBox {
            Button {
                objectName: "imagesExportButton"
                text: imagesDialog.exported.length === 1 ? qsTr("Export") : qsTr("Export %1").arg(imagesDialog.exported.length)
                highlighted: true
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            }
            Button { text: qsTr("Cancel"); flat: true; DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
        }
    }
}
