import QtQuick
import QtQuick.Controls.FluentWinUI3
import QtQuick.Layouts
import "../theme"

ColumnLayout {
    id: pane
    required property QtObject library
    signal openSettings()
    spacing: 8
    Label {
        visible: pane.library.server.length === 0
        text: "请先配置允许匿名访问的弹弹play兼容服务。"
        wrapMode: Text.WordWrap
        Layout.fillWidth: true
    }
    Button { visible: pane.library.server.length === 0; text: "打开在线服务设置"; onClicked: pane.openSettings() }
    RowLayout {
        Layout.fillWidth: true
        TextField {
            id: keyword
            objectName: "animeKeyword"
            Layout.fillWidth: true
            placeholderText: "输入动画名称"
            Accessible.name: "动画搜索关键词"
            maximumLength: 512
            onAccepted: pane.library.searchAnime(text)
        }
        Button {
            text: "搜索"
            enabled: pane.library.server.length > 0 && keyword.text.trim().length > 0
            onClicked: pane.library.searchAnime(keyword.text)
        }
    }
    Label { text: "动画"; font.weight: Font.DemiBold }
    ComboBox {
        id: animeBox
        objectName: "animeChoice"
        Layout.fillWidth: true
        model: pane.library.animes
        textRole: "title"; valueRole: "id"
        displayText: currentIndex >= 0 ? currentText : "选择搜索结果中的动画"
        Accessible.name: "动画搜索结果"
        onActivated: pane.library.selectAnime(currentValue)
        function sync() { currentIndex = indexOfValue(pane.library.selectedAnime) }
        onModelChanged: sync()
        Component.onCompleted: sync()
    }
    Label { text: "剧集"; font.weight: Font.DemiBold }
    ComboBox {
        id: episodeBox
        objectName: "episodeChoice"
        Layout.fillWidth: true
        model: pane.library.episodes
        textRole: "title"; valueRole: "id"
        displayText: currentIndex >= 0 ? currentText : "选择剧集"
        Accessible.name: "动画剧集"
        onActivated: pane.library.selectEpisode(currentValue)
        function sync() { currentIndex = indexOfValue(pane.library.selectedEpisode) }
        onModelChanged: sync()
        Component.onCompleted: sync()
    }
    Connections {
        target: pane.library
        function onChanged() { animeBox.sync(); episodeBox.sync() }
    }
    Flow {
        Layout.fillWidth: true
        spacing: 8
        Button {
            text: pane.library.selectedCached ? "载入缓存" : "下载并载入"
            enabled: !pane.library.busy && pane.library.selectedEpisode.length > 0
            onClicked: pane.library.downloadEpisode(false)
        }
        Button {
            text: "重新下载"
            enabled: !pane.library.busy && pane.library.selectedEpisode.length > 0
            onClicked: pane.library.downloadEpisode(true)
        }
        Button { text: "取消"; visible: pane.library.busy; onClicked: pane.library.cancel() }
    }
    ProgressBar {
        visible: pane.library.busy
        Layout.fillWidth: true
        indeterminate: pane.library.progress < 0
        from: 0; to: 100; value: Math.max(0, pane.library.progress)
        Accessible.name: "在线弹幕操作进度"
    }
    Label { text: pane.library.status; wrapMode: Text.WordWrap; Layout.fillWidth: true; Accessible.name: "在线状态：" + text }
    Label { visible: pane.library.error.length > 0; text: pane.library.error; wrapMode: Text.WordWrap; Layout.fillWidth: true; Accessible.name: "在线错误：" + text }
    Label { text: "下载后保留缓存并载入弹幕；请使用下方按钮启动播放。"; font.pixelSize: Ui.captionSize; wrapMode: Text.WordWrap; Layout.fillWidth: true }
}
