import QtQuick
import QtQuick.Controls.FluentWinUI3
import QtQuick.Layouts
import "../theme"
import "../i18n"

ColumnLayout {
    id: pane
    required property QtObject library
    property string deleteKey: ""
    property string deleteTitle: ""
    readonly property var filteredEntries: {
        const query = filter.text.trim().toLowerCase()
        return library.cachedEntries.filter(entry => query.length === 0 ||
            (entry.animeTitle + " " + entry.episodeTitle + " " + (entry.server || "")).toLowerCase().includes(query))
    }
    spacing: 8
    RowLayout {
        Layout.fillWidth: true
        TextField { id: filter; Layout.fillWidth: true; placeholderText: I18n.cache.filterPlaceholder; Accessible.name: I18n.cache.filterAccessible }
        Button { text: I18n.cache.refreshBtn; enabled: !pane.library.scanning; onClicked: pane.library.refreshCache() }
    }
    Label { visible: pane.library.scanning; text: I18n.cache.scanningHint; Accessible.name: text }
    Label {
        visible: !pane.library.scanning && pane.filteredEntries.length === 0
        text: filter.text.trim().length > 0 ? I18n.cache.emptyMatched : I18n.cache.emptyHint
        Layout.fillWidth: true; wrapMode: Text.WordWrap
    }
    ListView {
        id: cachedList
        objectName: "cachedDanmakuList"
        Layout.fillWidth: true
        Layout.preferredHeight: pane.filteredEntries.length > 0 ? Math.min(260 * Ui.textScale, contentHeight) : 0
        clip: true
        spacing: 8
        model: pane.filteredEntries
        keyNavigationEnabled: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { id: cacheScrollBar; Accessible.name: I18n.cache.scrollBarAccessible }
        delegate: ColumnLayout {
            required property var modelData
            required property int index
            width: Math.max(0, cachedList.width - cacheScrollBar.width - Ui.scrollGap)
            spacing: 4
            Label { text: modelData.animeTitle + " · " + modelData.episodeTitle; font.weight: Font.DemiBold; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            Label { text: (modelData.server || I18n.cache.unknownServer) + " · " + I18n.format(I18n.cache.itemCountFormat, modelData.count || 0); color: Ui.secondaryText; wrapMode: Text.WrapAnywhere; Layout.fillWidth: true }
            Label { text: modelData.downloadedAt ? new Date(modelData.downloadedAt).toLocaleString(Qt.locale(), Locale.ShortFormat) : I18n.cache.unknownDownloadTime; color: Ui.secondaryText; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            Label { visible: !modelData.valid; text: modelData.error || I18n.cache.invalidHint; wrapMode: Text.WordWrap; Layout.fillWidth: true; Accessible.name: I18n.cache.errorPrefix + text }
            RowLayout {
                Button {
                    text: I18n.cache.loadBtn
                    enabled: modelData.valid && !pane.library.busy
                    Accessible.name: I18n.format(I18n.cache.loadAccessibleFormat, modelData.animeTitle, modelData.episodeTitle)
                    onClicked: { cachedList.currentIndex = index; pane.library.loadCached(modelData.key) }
                }
                Button {
                    text: I18n.cache.deleteBtn
                    enabled: !pane.library.busy
                    Accessible.name: I18n.format(I18n.cache.deleteAccessibleFormat, modelData.animeTitle, modelData.episodeTitle)
                    onClicked: {
                        cachedList.currentIndex = index
                        pane.deleteKey = modelData.key
                        pane.deleteTitle = modelData.animeTitle + " · " + modelData.episodeTitle
                        deleteDialog.open()
                    }
                }
            }
            ToolSeparator { orientation: Qt.Horizontal; Layout.fillWidth: true }
        }
    }
    Button { text: I18n.cache.cancelBtn; visible: pane.library.busy; onClicked: pane.library.cancel() }
    Label { text: pane.library.status; Layout.fillWidth: true; wrapMode: Text.WordWrap }
    Label { text: pane.library.error; visible: text.length > 0; Layout.fillWidth: true; wrapMode: Text.WordWrap; Accessible.name: I18n.cache.errorPrefix + text }
    Dialog {
        id: deleteDialog
        title: I18n.cache.deleteDialogTitle
        modal: true

        width: Math.min(420, Overlay.overlay.width - 24)
        standardButtons: Dialog.Ok | Dialog.Cancel
        contentItem: Label { text: pane.deleteTitle + I18n.cache.deleteDialogContentSuffix; wrapMode: Text.WordWrap }
        onAccepted: pane.library.removeCached(pane.deleteKey)
        onClosed: cachedList.forceActiveFocus()
    }
}
