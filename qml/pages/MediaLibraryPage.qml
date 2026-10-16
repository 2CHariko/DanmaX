import QtQuick
import QtQuick.Controls.FluentWinUI3
import QtQuick.Layouts
import QtQuick.Dialogs
import "../components"
import "../theme"

PageFrame {
    id: page
    required property QtObject backend
    readonly property QtObject session: backend.media
    title: "媒体库"
    description: "打开视频，自动准备弹幕；从最近播放继续观看。"
    onVisibleChanged: if (visible) session.recent.refresh()
    function formatTime(seconds) {
        const value = Math.max(0, Math.floor(seconds || 0))
        return Math.floor(value / 60) + ":" + String(value % 60).padStart(2, "0")
    }
    VideoOpenPane { backend: page.backend }
    SettingsSection {
        title: "当前视频"
        visible: page.session.videoPath.length > 0
        SettingsRow {
            title: page.session.title
            description: page.session.status
            Button {
                text: "重新打开"
                Accessible.name: "重新打开当前视频"
                onClicked: page.session.openVideo(page.session.videoPath, 1)
            }
        }
        Label {
            Layout.fillWidth: true
            text: page.session.error
            visible: text.length > 0
            color: Ui.cautionText
            wrapMode: Text.WordWrap
            Accessible.role: Accessible.AlertMessage
        }
        RowLayout {
            visible: page.session.restoreFailed
            Layout.fillWidth: true
            Button { objectName: "retryMediaRestore"; text: "重试恢复"; onClicked: page.session.retryRestore() }
            Button { objectName: "continueMediaAtCurrentPosition"; text: "按当前位置继续"; onClicked: page.session.continueAtCurrentPosition() }
        }
        Label {
            Layout.fillWidth: true
            text: (page.session.targetSessionConnected ? "播放器已连接" : "播放器未连接")
                + " · " + page.formatTime(page.session.position) + " / " + page.formatTime(page.session.duration)
            color: Ui.secondaryText
        }
        ProgressBar {
            Layout.fillWidth: true
            from: 0; to: Math.max(1, page.session.duration); value: page.session.position
            indeterminate: page.session.restoring
            Accessible.name: "当前视频进度，只读"
        }
        SettingsRow {
            title: "视频弹幕"
            description: page.session.sourceTitle.length > 0 ? page.session.sourceTitle + " · " + page.session.total + " 条"
                : "尚未载入，可自动匹配或选择来源"
            Button { text: "选择 XML"; objectName: "chooseMediaXml"; enabled: page.session.canSelectSource; onClicked: xmlDialog.open() }
            Button { text: "搜索"; enabled: page.session.active && page.session.canSelectSource; onClicked: page.session.searchDanmaku() }
            Button { text: "卸载弹幕"; enabled: page.session.active; onClicked: page.session.unload() }
        }
        SettingsRow {
            title: "自动匹配"
            description: page.session.automatic.status
            Button { text: "重新匹配"; enabled: page.session.active; onClicked: page.session.retryMatching() }
            Button { text: "手动匹配"; enabled: page.session.active; onClicked: page.session.showAutomaticDanmakuChoice() }
        }
        Disclosure {
            title: "选择已缓存弹幕"
            CachedDanmakuPane { Layout.fillWidth: true; library: page.session.library }
        }
    }
    SettingsSection {
        title: "最近播放"
        Label {
            Layout.fillWidth: true
            text: page.session.recent.error
            visible: text.length > 0
            color: Ui.cautionText
            wrapMode: Text.WordWrap
        }
        Label {
            visible: page.session.recent.entries.length === 0
            text: "尚无最近播放。打开一个本地视频即可开始。"
            Layout.fillWidth: true
            color: Ui.secondaryText
            wrapMode: Text.WordWrap
        }
        Repeater {
            model: page.session.recent
            SettingsRow {
                required property var entry
                readonly property var modelData: entry
                Layout.fillWidth: true
                title: modelData.title
                description: (modelData.missing ? "文件不存在 · " : "") + page.formatTime(modelData.position)
                    + " / " + page.formatTime(modelData.duration) + "\n" + modelData.path
                Button {
                    objectName: "playRecentVideo"
                    text: modelData.canResume ? "继续观看" : modelData.position > 0 ? "从头播放" : "播放"
                    Accessible.name: text + " " + modelData.title
                    onClicked: page.session.openVideo(modelData.path, modelData.canResume ? 1 : 2)
                }
                Button {
                    text: "从头播放"
                    visible: modelData.canResume
                    Accessible.name: "从头播放 " + modelData.title
                    onClicked: page.session.openVideo(modelData.path, 2)
                }
                Button {
                    text: "移除"
                    Accessible.name: "从最近播放移除 " + modelData.title
                    onClicked: page.session.recent.remove(modelData.path)
                }
            }
        }
    }
    FileDialog {
        id: xmlDialog
        title: "为当前视频选择弹幕 XML"
        nameFilters: ["弹幕 XML (*.xml)"]
        onAccepted: page.session.loadFile(selectedFile)
    }
}
