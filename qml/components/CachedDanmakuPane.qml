import QtQuick
import QtQuick.Controls.FluentWinUI3
import QtQuick.Layouts
import "../theme"

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
        TextField { id: filter; Layout.fillWidth: true; placeholderText: "筛选已缓存动画或剧集"; Accessible.name: "缓存筛选" }
        Button { text: "刷新"; enabled: !pane.library.scanning; onClicked: pane.library.refreshCache() }
    }
    Label { visible: pane.library.scanning; text: "正在读取缓存列表…"; Accessible.name: text }
    Label {
        visible: !pane.library.scanning && pane.filteredEntries.length === 0
        text: filter.text.trim().length > 0 ? "没有匹配的缓存" : "还没有在线弹幕缓存。下载剧集后会显示在这里。"
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
        ScrollBar.vertical: ContentScrollBar { id: cacheScrollBar; Accessible.name: "缓存列表滚动条" }
        delegate: ColumnLayout {
            required property var modelData
            required property int index
            width: Math.max(0, cachedList.width - cacheScrollBar.width - Ui.scrollGap)
            spacing: 4
            Label { text: modelData.animeTitle + " · " + modelData.episodeTitle; font.weight: Font.DemiBold; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            Label { text: (modelData.server || "未知来源") + " · " + (modelData.count || 0) + " 条"; color: Ui.secondaryText; font.pixelSize: Ui.captionSize; wrapMode: Text.WrapAnywhere; Layout.fillWidth: true }
            Label { text: modelData.downloadedAt ? new Date(modelData.downloadedAt).toLocaleString(Qt.locale(), Locale.ShortFormat) : "下载时间未知"; color: Ui.secondaryText; font.pixelSize: Ui.captionSize; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            Label { visible: !modelData.valid; text: modelData.error || "缓存不可载入"; wrapMode: Text.WordWrap; Layout.fillWidth: true; Accessible.name: "缓存错误：" + text }
            RowLayout {
                Button {
                    text: "载入"
                    enabled: modelData.valid && !pane.library.busy
                    Accessible.name: "载入 " + modelData.animeTitle + " " + modelData.episodeTitle
                    onClicked: { cachedList.currentIndex = index; pane.library.loadCached(modelData.key) }
                }
                Button {
                    text: "删除"
                    enabled: !pane.library.busy
                    Accessible.name: "删除缓存 " + modelData.animeTitle + " " + modelData.episodeTitle
                    onClicked: {
                        cachedList.currentIndex = index
                        pane.deleteKey = modelData.key
                        pane.deleteTitle = modelData.animeTitle + " · " + modelData.episodeTitle
                        deleteDialog.open()
                    }
                }
            }
            Rectangle { Layout.fillWidth: true; Layout.topMargin: 8; implicitHeight: 1; color: Ui.borderColor }
        }
    }
    Button { text: "取消"; visible: pane.library.busy; onClicked: pane.library.cancel() }
    Label { text: pane.library.status; Layout.fillWidth: true; wrapMode: Text.WordWrap }
    Label { text: pane.library.error; visible: text.length > 0; Layout.fillWidth: true; wrapMode: Text.WordWrap; Accessible.name: "缓存错误：" + text }
    Dialog {
        id: deleteDialog
        title: "删除这条弹幕缓存？"
        modal: true
        anchors.centerIn: Overlay.overlay
        width: Math.min(420, Overlay.overlay.width - 24)
        standardButtons: Dialog.Ok | Dialog.Cancel
        Label { width: parent.width; text: pane.deleteTitle + "\n已载入的弹幕仍可继续播放。"; wrapMode: Text.WordWrap }
        onAccepted: pane.library.removeCached(pane.deleteKey)
        onClosed: cachedList.forceActiveFocus()
    }
}
