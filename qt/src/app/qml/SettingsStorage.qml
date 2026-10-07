// xournal-qt: Settings → Storage: the cache of this library, and removing it.
// Part of SettingsPage.qml, instantiated once there: it reads the sheet through `sheet` (SettingsPage.qml's
// context: `sheet.s` is app.settings, `sheet.narrow`, `sheet.win`); its rows are Settings*Row.qml.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtQuick.Dialogs

ScrollView {
    contentWidth: availableWidth
    onVisibleChanged: if (visible && app.library.available) app.library.measureCache()
    ColumnLayout {
        width: parent.width - 48
        x: 24
        spacing: 10
        SettingsSectionTitle { text: qsTr("Cache of this library") }
        SettingsHint {
            text: app.library.available
                  ? qsTr("“%1” keeps the previews of its documents and its search index in a hidden folder "
                         + ".xournal_library in each folder with documents, or in the app's cache folder. "
                         + "It only makes the app faster: it can be removed at any time and is built again "
                         + "when needed.").arg(app.library.name)
                  : qsTr("No library is open in this window.")
        }
        Label {
            objectName: "cacheSizeLabel"
            visible: app.library.available
            text: app.library.cacheBytes < 0
                  ? qsTr("Counting …")
                  : qsTr("It takes %1 in %2 files.").arg(sheet.sizeText(app.library.cacheBytes))
                                                      .arg(app.library.cacheFiles)
        }
        RowLayout {
            Layout.fillWidth: true
            visible: app.library.available
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: qsTr("Keep the cache in the app's cache folder, not in the library's folders")
            }
            Switch {
                objectName: "cacheInAppSwitch"
                enabled: !app.library.cacheRemoved
                checked: app.library.cacheInAppCache
                onToggled: app.library.cacheInAppCache = checked
            }
        }
        SettingsHint {
            visible: app.library.available
            text: qsTr("Recommended for folders that are synced (OneDrive, Dropbox, Nextcloud …): the "
                       + "library's folders then stay free of the app's files, and nothing of it is "
                       + "uploaded. Switching moves the cache. The app's cache folder of this library: %1")
                  .arg(app.library.appCachePath)
        }
        SettingsSectionTitle { text: qsTr("Clean up"); visible: app.library.available }
        SettingsHint {
            visible: app.library.available
            text: qsTr("Remove all cache folders of this library, e.g. to zip the library and send it. "
                       + "Where you were in each document is kept.")
        }
        Button {
            objectName: "removeCachesButton"
            visible: app.library.available
            enabled: !app.library.cacheRemoved
            text: qsTr("Remove all cache folders of this library …")
            onClicked: removeCaches.open()
        }
        SettingsHint {
            visible: app.library.cacheRemoved
            text: qsTr("Removed. They are built again the next time the library is opened.")
        }
        Item { Layout.preferredHeight: 16 }
    }

    // Removing the cache folders: says what happens (the app closes afterwards)
    AdaptiveDialog {
        id: removeCaches
        objectName: "removeCachesDialog"
        kind: "question"
        preferredWidth: 480
        title: qsTr("Remove the cache folders?")
        ColumnLayout {
            width: removeCaches.availableWidth
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: qsTr("All .xournal_library folders of “%1” are removed, and its cache in the app's cache "
                           + "folder (%2). Only files the app wrote are removed; where you were in each document is "
                           + "kept.").arg(app.library.name).arg(sheet.sizeText(Math.max(0, app.library.cacheBytes)))
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: qsTr("The app closes afterwards, so it does not build them again right away (for example while "
                           + "you zip the library to send it). The next time the library is opened, they are built "
                           + "again.")
            }
        }
        footer: DialogButtonBox {
            Button {
                objectName: "removeCachesConfirm"
                text: qsTr("Remove and close the app")
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            }
            Button {
                text: qsTr("Cancel")
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            }
        }
        onAccepted: {
            app.library.removeCaches()
            sheet.close()
            sheet.quitRequested()
        }
    }
}
