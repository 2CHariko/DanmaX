import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

ApplicationWindow {
    id: root
    required property QtObject backend
    visible: false
    width: 820
    height: 520
    minimumWidth: 680
    minimumHeight: 440
    title: "Local Danmaku · 开发预览"
    color: "#f4f5f7"
    font.pixelSize: 14
    palette.windowText: "#202b38"
    palette.text: "#202b38"
    palette.buttonText: "#202b38"
    palette.button: "#e6ebf0"
    palette.base: "white"
    palette.window: "#f4f5f7"
    palette.highlight: "#2761a7"
    palette.highlightedText: "white"
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 28
        spacing: 18
        Label { text: "本地弹幕"; font.pixelSize: 28; font.bold: true }
        Label { text: "C++ / Qt Quick 框架已就绪"; font.pixelSize: 16; color: "#435469" }
        Frame {
            Layout.fillWidth: true
            padding: 18
            background: Rectangle { color: "white"; border.color: "#d7dce3"; radius: 6 }
            ColumnLayout {
                anchors.fill: parent
                spacing: 12
                Label { text: "覆盖层预览"; font.bold: true }
                Label {
                    Layout.fillWidth: true
                    text: "显示一条循环滚动的示例文字。覆盖层不接收鼠标输入，可在这里关闭。"
                    wrapMode: Text.WordWrap
                }
                RowLayout {
                    Button {
                        text: root.backend.overlayVisible ? "关闭预览" : "显示预览"
                        onClicked: root.backend.overlayVisible = !root.backend.overlayVisible
                    }
                    Button {
                        enabled: root.backend.overlayVisible
                        text: root.backend.preview.running ? "暂停" : "继续"
                        onClicked: root.backend.preview.setRunning(!root.backend.preview.running)
                    }
                    Label { text: "动画时间  " + root.backend.preview.elapsedSeconds.toFixed(1) + " s" }
                }
            }
        }
        Label {
            Layout.fillWidth: true
            text: "这是框架验证版本。XML 加载、播放器会话同步和多轨弹幕引擎尚未实现。"
            wrapMode: Text.WordWrap
            color: "#5c6570"
        }
        Item { Layout.fillHeight: true }
        Label { Layout.fillWidth: true; text: root.backend.platformDescription; wrapMode: Text.WordWrap; font.pixelSize: 12 }
        Label {
            Layout.fillWidth: true
            text: "数据目录：" + root.backend.dataDirectory
            wrapMode: Text.WrapAnywhere
            font.pixelSize: 12
            color: "#5c6570"
        }
    }
    OverlayWindow { preview: root.backend.preview; visible: root.backend.overlayVisible }
    onClosing: root.backend.overlayVisible = false
}
