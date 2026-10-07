// xournal-qt: Settings → Documents: how documents are saved, PDFs with notes, autosave,
// the library.
// Part of SettingsPage.qml, instantiated once there: it reads the sheet through `sheet` (SettingsPage.qml's
// context: `sheet.s` is app.settings, `sheet.narrow`, `sheet.win`); its rows are Settings*Row.qml.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtQuick.Dialogs

ScrollView {
    contentWidth: availableWidth
    ColumnLayout {
        width: parent.width - 48
        x: 24
        spacing: 10
        SettingsSectionTitle { text: qsTr("Keep documents as") }
        DocumentModeCards {
            objectName: "documentModeCards"
            Layout.fillWidth: true
            Layout.maximumWidth: 640
            mode: app.documentMode
            onPicked: function(mode) { app.documentMode = mode }
        }
        SettingsHint {
            text: qsTr("Documents you have keep their format: a .xopp stays a .xopp until you save it as a "
                       + "PDF with notes (Save as…). Copies for Xournal++: Share → “For Xournal++”.")
        }
        SettingsComboRow {
            objectName: "newTextDocumentsRow"
            key: "newTextDocuments"
            dependsOn: app.documentMode  // (while not chosen, it follows the mode)
            text: qsTr("New text documents")
            options: [
                { text: qsTr("PDF document"), value: "pdf" },
                { text: qsTr("Markdown file"), value: "md" }
            ]
        }
        SettingsHint {
            text: qsTr("A PDF document opens in any PDF app and carries its text as a Markdown file inside. "
                       + "Markdown files you have stay Markdown files.")
        }
        // Quick note (qt/docs/features/quick-note.md): Ctrl+Alt+N, the home screen, ⋮, --quick-note
        SettingsComboRow {
            objectName: "quickNoteRow"
            key: "quickNote"
            text: qsTr("Quick note")
            options: [
                { text: qsTr("A new note"), value: "note" },
                { text: qsTr("A line in today's Markdown note"), value: "daily" }
            ]
        }
        SettingsHint {
            text: qsTr("Quick notes go into the folder “Inbox” of the library: a new note is named by the date "
                       + "and time; today's Markdown note by the date, a line with the time added each time.")
        }
        SettingsSectionTitle { text: qsTr("Start") }
        SettingsSwitchRow { key: "restoreSession"; text: qsTr("Reopen the documents of the last session") }
        SettingsSwitchRow { key: "resumeAtLastPage"; text: qsTr("Open documents where they were left off") }
        SettingsSectionTitle { text: qsTr("Links") }
        SettingsComboRow {
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
        // The library's To-dos (qt/docs/features/todos.md): which check boxes of Markdown are to-dos
        SettingsSectionTitle { text: qsTr("To-dos") }
        SettingsComboRow {
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
        SettingsHint {
            text: qsTr("A check box line (- [ ] …) of a Markdown text, a sticky note or a Markdown file is a "
                       + "to-do when it has the marker anywhere in it (upper or lower case); the list does not "
                       + "show the marker. Check-box stamps for handwritten to-dos always count. A due date: "
                       + "📅 2026-10-12 or due:2026-10-12.")
        }
        // Looking up selected text, the papers of references, arXiv (qt/docs/features/citations.md)
        SettingsSectionTitle { text: qsTr("Web and citations") }
        SettingsSwitchRow {
            objectName: "webConfirmRow"
            key: "webConfirm"
            text: qsTr("Ask before opening a web address (it is shown whole)")
        }
        SettingsComboRow {
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
        SettingsHint {
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
        SettingsHint {
            visible: translatorRow.custom
            text: qsTr("{text} is replaced by the selected text, {lang} by the language below.")
        }
        SettingsComboRow {
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
        SettingsSectionTitle { text: qsTr("Hybrid PDF") }
        SettingsHint {
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
        SettingsSwitchRow {
            key: "hybridExportXopp"
            text: qsTr("On every save of a hybrid PDF, also write a .xopp for Xournal++")
        }
        SettingsComboRow {
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
        // Version history (qt/docs/features/hybrid-pdf.md): per document in the sidebar's History panel; this is
        // for PDFs with notes that are new
        SettingsSectionTitle { text: qsTr("Version history") }
        SettingsHint {
            text: qsTr("A PDF with notes can keep its versions inside itself: one for each day you save "
                       + "it, plus milestones you name. Turn it on for a document in the sidebar's History "
                       + "panel, or here for every new one.")
        }
        RowLayout {
            Layout.fillWidth: true
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: qsTr("Keep versions of new PDFs with notes")
            }
            Switch {
                objectName: "keepVersionsSwitch"
                checked: (sheet.s.revision, sheet.s.get("keepVersionsOfNewPdfs"))
                onToggled: sheet.s.set("keepVersionsOfNewPdfs", checked)
            }
        }
        SettingsSectionTitle { text: qsTr("Autosave") }
        SettingsSwitchRow { key: "autosaveEnabled"; text: qsTr("Save a backup of unsaved changes regularly") }
        SettingsSliderRow {
            enabled: (sheet.s.revision, sheet.s.get("autosaveEnabled"))
            key: "autosaveMinutes"; text: qsTr("Every")
            from: 1; to: 30; stepSize: 1; decimals: 0; suffix: " min"
        }
        SettingsSectionTitle { text: qsTr("Memory") }
        SettingsSliderRow {
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
        SettingsSliderRow {
            key: "previewMemory"; text: qsTr("Page previews (sidebar, overviews)")
            from: 64; to: 1024; stepSize: 64; decimals: 0; suffix: " MB"
        }
        SettingsSectionTitle { text: qsTr("File names") }
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
        SettingsHint { text: qsTr("%F is the date (2026-09-19), %H-%M the time.") }
        // Pictures put on the clipboard (qt/docs/features/snip.md, qt/docs/features/page-files.md): chosen here once,
        // not asked with every snip or copied page
        SettingsSectionTitle { text: qsTr("Pictures copied to the clipboard") }
        RowLayout {
            Layout.fillWidth: true
            Label { text: qsTr("Snip"); Layout.preferredWidth: sheet.narrow ? -1 : 220 }
            ComboBox {
                objectName: "snipResolutionBox"
                Layout.fillWidth: true
                readonly property var choices: sheet.inWindow && win.toolGroups ? win.toolGroups.snipResolutions : []
                model: choices.map(function(c) { return c.name })
                currentIndex: Math.max(0, choices.map(function(c) { return c.key }).indexOf(app.snipResolution))
                onActivated: app.snipResolution = choices[currentIndex].key
            }
        }
        SettingsHint { text: qsTr("A snip of a large area gets fewer pixels than asked (at most about 36 megapixels at 300 or 600 dpi, 4 at the screen's); the note after a snip says its size.") }
        RowLayout {
            Layout.fillWidth: true
            Label { text: qsTr("Pages"); Layout.preferredWidth: sheet.narrow ? -1 : 220 }
            ComboBox {
                objectName: "pageImageDpiBox"
                readonly property var dpis: [150, 200, 300, 400, 600]
                model: dpis.map(function(d) { return qsTr("%1 dpi").arg(d) })
                currentIndex: Math.max(0, dpis.indexOf(app.pageImageDpi))
                onActivated: app.pageImageDpi = dpis[currentIndex]
            }
        }
        SettingsHint { text: qsTr("Copy page as image (Ctrl+Shift+C) puts the page on the clipboard at this resolution; a screenshot has the screen's. Export as pictures starts with it.") }
        // Audio recordings (qt/docs/features/audio.md)
        SettingsSectionTitle { visible: app.audio.available; text: qsTr("Audio recordings") }
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
        SettingsHint {
            visible: app.audio.available
            text: qsTr("Recordings are kept in the app's audio folder, as Xournal++ keeps them; a PDF with notes carries its own.")
        }
        Item { Layout.preferredHeight: 16 }
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
}
