// xournal-qt: the fields of a plugin's dialog, described by the plugin as data and drawn here
// (qt/docs/features/plugins.md, "Dialogs"): text, number, color (from the palette), choice, checkbox, slider, button,
// label. Fields with the same `row` share a line. `values` holds what is entered (by field id); every edit is told at
// once (a live dialog previews it), a button by its id as `action`.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ColumnLayout {
    id: form
    /// [{id, type, label, value, …}] (qt/docs/features/plugins.md)
    property var fields: []
    /// The values by field id (set it to start; it changes as the user edits)
    property var values: ({})
    /// Messages under fields, by field id ("unknown function 'sni' - did you mean sin?")
    property var errors: ({})
    /// A field changed (`id`, its new value), or a button was pressed (`action`: its id)
    signal edited(string id, var value, string action)
    spacing: 8

    function valueOf(f) {
        if (f.id !== undefined && values[f.id] !== undefined) return values[f.id]
        return f.value
    }
    function set(id, v) {
        const next = Object.assign({}, values)
        next[id] = v
        values = next
        edited(id, v, "")
    }
    /// The palette's colors (the palette chosen now; ink): [{name, color}]
    readonly property var paletteColors: {
        const ps = app.colorPalettes
        let p = null
        for (let i = 0; i < ps.length; ++i) if (ps[i].id === app.colorPalette) p = ps[i]
        if (!p && ps.length > 0) p = ps[0]
        return p ? p.roles.map(function(r) { return { name: r.name, color: String(r.ink) } }) : []
    }
    /// The fields in lines: consecutive fields with the same `row` share one
    readonly property var rows: {
        const out = []
        for (let i = 0; i < fields.length; ++i) {
            const f = fields[i]
            const last = out.length > 0 ? out[out.length - 1] : null
            if (last && f.row !== undefined && f.row !== null && f.row !== "" && last.key === f.row) last.items.push(f)
            else out.push({ key: f.row !== undefined ? f.row : "", items: [f] })
        }
        return out
    }
    function number(text) {
        const t = String(text).trim().replace(",", ".")
        if (t === "" || isNaN(Number(t))) return NaN
        return Number(t)
    }
    function shown(v) {
        if (typeof v !== "number") return String(v)
        const r = Math.round(v * 1e6) / 1e6
        return String(r).replace(".", Qt.locale().decimalPoint)
    }

    Repeater {
        model: form.rows
        delegate: RowLayout {
            id: line
            required property var modelData
            Layout.fillWidth: true
            spacing: 8
            Repeater {
                model: line.modelData.items
                delegate: ColumnLayout {
                    id: cell
                    required property var modelData
                    readonly property var f: modelData
                    readonly property string error: f.id !== undefined && form.errors && form.errors[f.id] ? form.errors[f.id] : ""
                    Layout.fillWidth: f.width === undefined && f.type !== "checkbox" && f.type !== "button" && f.type !== "color"
                    Layout.preferredWidth: f.width !== undefined ? f.width : -1
                    spacing: 2
                    Label {
                        visible: cell.f.type !== "checkbox" && cell.f.type !== "button" && cell.f.type !== "label"
                                 && (cell.f.label || "") !== ""
                        text: cell.f.label || ""
                        color: "#5f6368"
                        font.pixelSize: 12
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                    Loader {
                        Layout.fillWidth: true
                        sourceComponent: {
                            switch (cell.f.type) {
                            case "number": return numberField
                            case "color": return colorField
                            case "choice": return choiceField
                            case "checkbox": return checkField
                            case "slider": return sliderField
                            case "button": return buttonField
                            case "label": return labelField
                            default: return textField
                            }
                        }
                        onLoaded: item.field = Qt.binding(function() { return cell.f })
                    }
                    Label {
                        objectName: "pluginFieldError_" + (cell.f.id || "")
                        visible: cell.error !== ""
                        text: cell.error
                        color: "#c5221f"
                        font.pixelSize: 12
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                }
            }
        }
    }

    Component {
        id: textField
        TextField {
            property var field: ({})
            objectName: "pluginField_" + field.id
            text: String(form.valueOf(field) === undefined ? "" : form.valueOf(field))
            placeholderText: field.placeholder || ""
            selectByMouse: true
            inputMethodHints: Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase
            onTextEdited: form.set(field.id, text)
        }
    }
    Component {
        id: numberField
        TextField {
            property var field: ({})
            objectName: "pluginField_" + field.id
            text: form.shown(form.valueOf(field) === undefined ? "" : form.valueOf(field))
            selectByMouse: true
            inputMethodHints: Qt.ImhFormattedNumbersOnly
            rightPadding: unitLabel.visible ? unitLabel.width + 8 : 8
            onTextEdited: {
                const n = form.number(text)
                if (!isNaN(n)) form.set(field.id, n)
            }
            Label {
                id: unitLabel
                visible: (field.unit || "") !== ""
                text: field.unit || ""
                color: "#5f6368"
                anchors.right: parent.right
                anchors.rightMargin: 6
                anchors.verticalCenter: parent.verticalCenter
            }
        }
    }
    Component {
        id: colorField
        Flow {
            property var field: ({})
            objectName: "pluginField_" + field.id
            spacing: 4
            readonly property string current: String(form.valueOf(field) || "#000000").toLowerCase()
            Repeater {
                model: form.paletteColors
                delegate: AbstractButton {
                    required property var modelData
                    required property int index
                    objectName: "pluginField_" + field.id + "_" + index
                    width: 26
                    height: 26
                    focusPolicy: Qt.NoFocus
                    ToolTip.visible: hovered
                    ToolTip.text: modelData.name
                    ToolTip.delay: 600
                    onClicked: form.set(field.id, modelData.color)
                    contentItem: Rectangle {
                        radius: 13
                        color: parent.modelData.color
                        border.width: parent.parent.current === String(parent.modelData.color).toLowerCase() ? 3 : 1
                        border.color: border.width > 1 ? Material.accentColor : "#40000000"
                    }
                }
            }
        }
    }
    Component {
        id: choiceField
        ComboBox {
            property var field: ({})
            objectName: "pluginField_" + field.id
            readonly property var options: (field.options || []).map(function(o) {
                return typeof o === "object" ? o : { value: o, label: String(o) }
            })
            model: options.map(function(o) { return o.label })
            currentIndex: {
                const v = form.valueOf(field)
                for (let i = 0; i < options.length; ++i) if (options[i].value === v) return i
                return 0
            }
            onActivated: function(i) { form.set(field.id, options[i].value) }
        }
    }
    Component {
        id: checkField
        CheckBox {
            property var field: ({})
            objectName: "pluginField_" + field.id
            text: field.label || ""
            checked: form.valueOf(field) === true
            onToggled: form.set(field.id, checked)
        }
    }
    Component {
        id: sliderField
        RowLayout {
            property var field: ({})
            spacing: 6
            Slider {
                id: slider
                objectName: "pluginField_" + field.id
                Layout.fillWidth: true
                from: field.min !== undefined ? field.min : 0
                to: field.max !== undefined ? field.max : 10
                stepSize: field.step !== undefined ? field.step : 0
                snapMode: Slider.SnapAlways
                value: Number(form.valueOf(field) || 0)
                onMoved: form.set(field.id, Math.round(value * 1e6) / 1e6)
            }
            Label {
                text: form.shown(Number(form.valueOf(field) || 0))
                Layout.preferredWidth: 44
                horizontalAlignment: Text.AlignRight
            }
        }
    }
    Component {
        id: buttonField
        Button {
            property var field: ({})
            objectName: "pluginField_" + field.id
            text: field.label || ""
            flat: true
            icon.source: field.icon ? app.iconUrl(field.icon) : ""
            onClicked: form.edited(field.id, true, field.id)
        }
    }
    Component {
        id: labelField
        Label {
            property var field: ({})
            objectName: field.id ? "pluginField_" + field.id : ""
            text: field.text || field.label || ""
            wrapMode: Text.Wrap
            font.weight: field.style === "heading" ? Font.DemiBold : Font.Normal
            color: field.style === "heading" ? Material.accentColor : "#5f6368"
            topPadding: field.style === "heading" ? 6 : 0
        }
    }
}
