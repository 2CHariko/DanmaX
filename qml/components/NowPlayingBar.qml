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
    signal openStandalone()
    signal replaceXml()
    signal showSources()
    signal showSettings()
    signal showPlaying()
    readonly property QtObject presentation: backend.presentation
    property bool watchingTheme: presentation.watching
    readonly property bool compactActions: width < 1008 * Ui.textScale
    readonly property bool interacting: progress.pressed || danmakuMenu.visible || playerMenu.visible || moreMenu.visible || keyboardFocus
    property bool keyboardFocus: false
    property int hostGeneration: 0
    function focusTransport() { keyboardFocus = true; toggleButton.forceActiveFocus(Qt.TabFocusReason) }
    function prepareHostChange() {
        danmakuMenu.close(); playerMenu.close(); moreMenu.close()
        progress.moved = false; hostGeneration++; keyboardFocus = false
    }
    function time(value) {
        const seconds = Math.max(0, Math.floor(value || 0))
        const rest = String(seconds % 60).padStart(2, "0")
        return seconds >= 3600 ? Math.floor(seconds / 3600) + ":" + String(Math.floor(seconds / 60) % 60).padStart(2, "0") + ":" + rest
            : Math.floor(seconds / 60) + ":" + rest
    }
    Keys.onPressed: function(event) { keyboardFocus = true; presentation.interact(); event.accepted = false }
    HoverHandler { onHoveredChanged: if (!hovered) bar.keyboardFocus = false }
    padding: 12
    implicitHeight: contentItem.implicitHeight + topPadding + bottomPadding
    palette.window: watchingTheme ? Ui.playbackSurface : Ui.colors.window
    palette.base: watchingTheme ? Ui.playbackSurface : Ui.colors.base
    palette.text: watchingTheme ? Ui.playbackText : Ui.colors.text
    palette.buttonText: watchingTheme ? Ui.playbackText : Ui.colors.buttonText
    palette.button: watchingTheme ? Ui.playbackSurface : Ui.colors.button
    palette.accent: watchingTheme ? Ui.playbackAccent : Ui.colors.highlight
    palette.highlight: watchingTheme ? Ui.playbackAccent : Ui.colors.highlight
    palette.highlightedText: watchingTheme ? Ui.playbackHighlightText : Ui.colors.highlightedText
    palette.disabled.buttonText: watchingTheme ? Ui.playbackDisabled : Ui.colors.mid
    background: Rectangle { color: bar.watchingTheme ? Ui.playbackSurface : Ui.cardBackground; border.width: bar.watchingTheme ? 0 : 1; border.color: Ui.cardBorder }
    contentItem: ColumnLayout {
        spacing: 4
        RowLayout {
            Layout.fillWidth: true
            Label { text: bar.time(progress.pressed ? progress.draftValue : bar.player.seekPreviewPosition); color: bar.watchingTheme ? Ui.playbackSecondary : Ui.secondaryText; Accessible.name: "播放位置 " + text }
            Slider {
                id: progress
                objectName: "nowPlayingSeek"
                Layout.fillWidth: true
                enabled: bar.player.canSeek
                from: 0; to: Math.max(1, bar.player.duration)
                Binding on value { value: bar.player.seekPreviewPosition; when: !progress.pressed }
                Accessible.name: enabled ? "播放进度" : "播放进度，只读"
                property bool moved: false
                property real draftValue: value
                property int draftGeneration: -1
                onMoved: { draftValue = value; moved = true; if (!pressed) { moved = false; bar.player.seek(draftValue) } }
                onPressedChanged: {
                    if (pressed) draftGeneration = bar.hostGeneration
                    else if (moved) { moved = false; if (draftGeneration === bar.hostGeneration) bar.player.seek(draftValue) }
                }
                onEnabledChanged: if (!enabled) moved = false
                ToolTip.visible: hovered && !enabled
                ToolTip.text: "当前播放方式暂不支持拖动定位"
            }
            Label { text: bar.time(bar.player.duration); color: bar.watchingTheme ? Ui.playbackSecondary : Ui.secondaryText; Accessible.name: "总时长 " + text }
        }
        Item {
            id: transport
            Layout.fillWidth: true
            readonly property bool stacked: width < center.implicitWidth + 2 * actions.implicitWidth + 24
            implicitHeight: Math.max(center.implicitHeight, textInfo.implicitHeight) + (stacked ? actions.implicitHeight + 8 : 0)
            Column {
                id: textInfo
                x: 0; anchors.verticalCenter: parent.verticalCenter
                width: Math.max(0, (parent.width - center.width) / 2 - 12)
                visible: !transport.stacked && width >= 80 * Ui.textScale
                ItemDelegate {
                    objectName: "nowPlayingTitleLink"
                    width: parent.width
                    text: bar.player.title
                    display: AbstractButton.TextOnly
                    Accessible.name: "当前播放 " + text + "，转到播放页"
                    onClicked: bar.showPlaying()
                    ToolTip.visible: hovered
                    ToolTip.text: bar.player.title
                }
                Label {
                    width: parent.width; visible: !bar.compactActions
                    text: bar.player.error.length ? bar.player.error : bar.player.status
                    textFormat: Text.PlainText; elide: Text.ElideRight; maximumLineCount: 1
                    color: bar.watchingTheme ? Ui.playbackSecondary : Ui.secondaryText
                    font.pixelSize: Ui.captionSize
                }
            }
            Row {
                id: center
                objectName: "nowPlayingTransport"
                x: (parent.width - width) / 2
                y: transport.stacked ? 0 : (parent.height - height) / 2
                spacing: 4
                ToolButton {
                    id: backwardButton
                    width: Math.max(implicitWidth, forwardButton.implicitWidth)
                    objectName: "nowPlayingBackward"
                    text: "后退10秒"; icon.source: Ui.icon("chevron-left", bar.watchingTheme || Ui.dark)
                    enabled: bar.player.canSeek; onClicked: bar.player.seekRelative(-10)
                    ToolTip.visible: hovered; ToolTip.text: text
                    display: bar.compactActions ? AbstractButton.IconOnly : AbstractButton.TextBesideIcon
                    Accessible.name: text
                }
                ToolButton {
                    id: toggleButton
                    objectName: "nowPlayingToggle"
                    text: bar.player.completed ? "重新播放" : bar.player.playing ? "暂停" : "播放"
                    icon.source: Ui.icon(bar.player.playing ? "pause" : "play", bar.watchingTheme || Ui.dark)
                    icon.width: 28 * Ui.textScale; icon.height: 28 * Ui.textScale
                    width: 56 * Ui.textScale; height: Math.max(48 * Ui.textScale, implicitHeight)
                    display: AbstractButton.IconOnly
                    enabled: bar.player.canPlay
                    Accessible.name: text
                    Accessible.description: bar.player.controlHint
                    ToolTip.visible: hovered; ToolTip.text: text + " · " + bar.player.controlHint
                    onClicked: bar.player.togglePlayback()
                }
                ToolButton {
                    id: forwardButton
                    width: Math.max(implicitWidth, backwardButton.implicitWidth)
                    objectName: "nowPlayingForward"
                    text: "前进30秒"; icon.source: Ui.icon("chevron-right", bar.watchingTheme || Ui.dark)
                    enabled: bar.player.canSeek; onClicked: bar.player.seekRelative(30)
                    ToolTip.visible: hovered; ToolTip.text: text
                    display: bar.compactActions ? AbstractButton.IconOnly : AbstractButton.TextBesideIcon
                    Accessible.name: text
                }
            }
            Row {
                id: actions
                x: transport.stacked ? (parent.width - width) / 2 : parent.width - width
                y: transport.stacked ? center.height + 8 : (parent.height - height) / 2
                spacing: 4
                ToolButton { objectName: "nowPlayingDanmaku"; text: "弹幕"; icon.source: Ui.icon("logs", bar.watchingTheme || Ui.dark); display: bar.compactActions ? AbstractButton.IconOnly : AbstractButton.TextBesideIcon; enabled: bar.player.canSelectSource; onClicked: danmakuMenu.popup(); ToolTip.visible: hovered; ToolTip.text: text; Accessible.name: text }
                ToolButton { text: "播放器"; visible: !bar.player.standalone && !bar.compactActions; onClicked: playerMenu.popup() }
                ToolButton { objectName: "nowPlayingFullscreen"; text: bar.presentation.fullscreen ? "退出全屏" : "全屏"; visible: bar.presentation.watching; icon.source: Ui.icon(bar.presentation.fullscreen ? "full-screen-exit" : "full-screen", bar.watchingTheme || Ui.dark); display: AbstractButton.IconOnly; onClicked: bar.presentation.toggleFullscreen("button"); Accessible.name: text; ToolTip.visible: hovered; ToolTip.text: text + " (F11)" }
                ToolButton { objectName: "nowPlayingMore"; text: "更多"; icon.source: Ui.icon("chevron-down", bar.watchingTheme || Ui.dark); display: bar.compactActions ? AbstractButton.IconOnly : AbstractButton.TextBesideIcon; onClicked: moreMenu.popup(); ToolTip.visible: hovered; ToolTip.text: text; Accessible.name: text }
            }
        }
    }
    AppMenu {
        id: danmakuMenu
        watchingTheme: bar.watchingTheme
        palette: bar.palette
        objectName: "nowPlayingDanmakuMenu"
        focus: true
        onOpened: currentIndex = 1
        MenuItem { text: "当前来源 · " + (bar.player.source.length ? bar.player.source : "未选择弹幕"); enabled: false }
        MenuItem { objectName: "nowPlayingVisibility"; text: "显示弹幕"; checkable: true; checked: bar.backend.danmakuVisible; onTriggered: bar.backend.danmakuVisible = checked }
        MenuItem { objectName: "loadCurrentXml"; text: bar.player.count > 0 ? "更换弹幕 XML" : "加载弹幕 XML"; enabled: bar.player.canSelectSource; onTriggered: bar.replaceXml() }
        MenuItem { text: "搜索弹幕"; onTriggered: bar.player.searchDanmaku() }
        MenuItem { objectName: "selectCachedDanmaku"; text: "选择缓存弹幕"; onTriggered: bar.showSources() }
        MenuItem { text: "重新匹配弹幕"; visible: bar.player.video; height: visible ? implicitHeight : 0; onTriggered: bar.backend.media.retryMatching() }
        MenuSeparator {}
        MenuItem { objectName: "unloadDanmaku"; text: "卸载弹幕"; enabled: bar.player.count > 0; onTriggered: bar.player.unloadDanmaku() }
    }
    AppMenu {
        id: playerMenu
        watchingTheme: bar.watchingTheme
        palette: bar.palette
        focus: true
        onOpened: currentIndex = 1
        MenuItem { text: "当前播放器 · " + bar.backend.videoOpen.playerName; enabled: false }
        MenuItem { text: "播放器设置"; onTriggered: bar.showSettings() }
    }
    AppMenu {
        id: moreMenu
        watchingTheme: bar.watchingTheme
        palette: bar.palette
        objectName: "nowPlayingMoreMenu"
        focus: true
        onOpened: currentIndex = bar.compactActions && !bar.player.standalone ? 0 : 1
        MenuItem { text: "播放器设置"; visible: bar.compactActions && !bar.player.standalone; height: visible ? implicitHeight : 0; onTriggered: bar.showSettings() }
        MenuItem { text: "打开视频"; onTriggered: bar.openVideo() }
        MenuItem { objectName: "openStandaloneDanmaku"; text: "独立播放弹幕 XML"; onTriggered: bar.openStandalone() }
        MenuSeparator {}
        MenuItem { objectName: "stopPlayback"; text: bar.backend.embeddedVideo ? "结束内置播放" : bar.player.ownsPlayer ? "结束播放并关闭播放器" : bar.player.standalone ? "结束独立弹幕播放" : "结束本次播放"; enabled: bar.player.active || bar.player.openingStandalone; onTriggered: bar.player.endPlayback() }
    }
}
