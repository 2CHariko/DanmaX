import QtQuick
import QtQuick.Controls.FluentWinUI3
import QtQuick.Layouts
import "../theme"

Control {
    id: bar
    objectName: "nowPlayingBar"
    required property QtObject backend
    readonly property QtObject player: backend.nowPlaying
    signal openVideo()
    signal openDanmaku()
    signal replaceXml()
    signal showSources()
    signal showPlayer()
    signal showSettings()
    signal showPlaying()
    function time(value) {
        const seconds = Math.max(0, Math.floor(value || 0))
        return Math.floor(seconds / 60) + ":" + String(seconds % 60).padStart(2, "0")
    }
    padding: 12
    implicitHeight: contentItem.implicitHeight + topPadding + bottomPadding
    background: Rectangle { color: Ui.cardBackground; border.width: 1; border.color: Ui.cardBorder }
    contentItem: ColumnLayout {
        spacing: 4
        RowLayout {
            Layout.fillWidth: true
            Label { text: bar.time(bar.player.position); color: Ui.secondaryText; Accessible.name: "当前播放位置 " + text }
            Slider {
                id: progress
                objectName: "nowPlayingSeek"
                Layout.fillWidth: true
                enabled: bar.player.canSeek
                from: 0; to: Math.max(1, bar.player.duration)
                Binding on value { value: bar.player.position; when: !progress.pressed }
                Accessible.name: enabled ? "播放进度" : "播放进度，只读"
                property bool moved: false
                onMoved: { moved = true; if (!pressed) { moved = false; bar.player.seek(value) } }
                onPressedChanged: if (!pressed && moved) { moved = false; bar.player.seek(value) }
                onEnabledChanged: if (!enabled) moved = false
                ToolTip.visible: hovered && !enabled
                ToolTip.text: "当前播放方式暂不支持拖动定位"
            }
            Label { text: bar.time(bar.player.duration); color: Ui.secondaryText; Accessible.name: "总时长 " + text }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            ToolButton {
                objectName: "nowPlayingToggle"
                text: bar.player.playing ? "暂停" : "播放"
                icon.source: Ui.icon(bar.player.playing ? "pause" : "play")
                enabled: bar.player.canPlay
                Accessible.description: bar.player.controlHint
                ToolTip.visible: hovered
                ToolTip.text: bar.player.controlHint
                onClicked: bar.player.togglePlayback()
            }
            Label {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                text: bar.player.title
                textFormat: Text.PlainText
                elide: Text.ElideRight
                maximumLineCount: 1
                Accessible.name: "当前播放 " + text
                MouseArea { anchors.fill: parent; onClicked: bar.showPlaying(); cursorShape: Qt.PointingHandCursor }
            }
            ToolButton {
                text: "选集"
                enabled: bar.player.canSelectEpisode
                onClicked: bar.player.selectEpisode()
                ToolTip.visible: hovered
                ToolTip.text: bar.player.video ? "尚无本地视频选集列表" : "选择弹幕分集"
            }
            ToolButton { objectName: "nowPlayingDanmaku"; text: "弹幕"; onClicked: danmakuMenu.popup() }
            ToolButton { text: "播放器"; onClicked: bar.player.video ? playerMenu.popup() : bar.showPlayer() }
            ToolButton { objectName: "nowPlayingMore"; text: "更多"; onClicked: moreMenu.popup() }
        }
    }
    AppMenu {
        id: danmakuMenu
        objectName: "nowPlayingDanmakuMenu"
        focus: true
        onOpened: currentIndex = 0
        MenuItem { objectName: "nowPlayingVisibility"; text: "显示弹幕"; checkable: true; checked: bar.backend.danmakuVisible; onTriggered: bar.backend.danmakuVisible = checked }
        MenuItem { text: "更换弹幕 XML"; onTriggered: bar.replaceXml() }
        MenuItem { text: "搜索弹幕"; onTriggered: bar.player.searchDanmaku() }
        MenuItem { text: "选择缓存与来源"; onTriggered: bar.showSources() }
        MenuSeparator {}
        MenuItem { objectName: "unloadDanmaku"; text: "卸载弹幕"; enabled: bar.player.count > 0; onTriggered: bar.player.unloadDanmaku() }
    }
    AppMenu {
        id: playerMenu
        focus: true
        onOpened: currentIndex = 1
        MenuItem { text: "当前播放器 · " + bar.backend.videoOpen.playerName; enabled: false }
        MenuItem { text: "外部播放器设置"; onTriggered: bar.showSettings() }
        MenuItem { text: "跟随其他播放器播放弹幕"; onTriggered: bar.showPlayer() }
    }
    AppMenu {
        id: moreMenu
        objectName: "nowPlayingMoreMenu"
        focus: true
        onOpened: currentIndex = 0
        MenuItem { text: "打开视频"; onTriggered: bar.openVideo() }
        MenuItem { text: "独立播放弹幕 XML"; onTriggered: bar.openDanmaku() }
        MenuSeparator {}
        MenuItem { objectName: "stopPlayback"; text: "结束本次播放"; enabled: bar.player.active; onTriggered: bar.player.endPlayback() }
    }
}
