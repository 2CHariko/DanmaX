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
}
