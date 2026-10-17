import QtQuick
import QtQuick.Window
import QtQuick.Controls.FluentWinUI3
import QtQuick.Layouts
import DanmaX.Probe

ApplicationWindow {
    id: root
    required property QtObject probe
    title: "DanmaX · libVLC 最小验证"
    width: 1120
    height: 760
    minimumWidth: 720
    minimumHeight: 480
    visible: true
    onClosing: function(close) { close.accepted = false; probe.close() }
    Shortcut { sequence: "Space"; onActivated: probe.togglePause() }
    Shortcut { sequence: "F11"; onActivated: root.visibility === Window.FullScreen ? root.showNormal() : root.showFullScreen() }
    Shortcut { sequence: "Escape"; onActivated: root.showNormal() }
    function showTestPopup() { testPopup.open() }
    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        Label {
            Layout.fillWidth: true
            Layout.margins: 16
            text: "libVLC 原生视频窗口 ＋ Qt Quick 独立覆盖窗 ＋ 现有 DanmakuItem"
            wrapMode: Text.Wrap
        }
        Item {
            id: viewport
            Layout.fillWidth: true
            Layout.fillHeight: true
            // The native video is never uploaded through QImage or a frame callback.
            WindowContainer { anchors.fill: parent; window: root.probe.videoWindow }
            Item {
                Window {
                    id: layer
                    title: "DanmaX libVLC probe composition layer"
                    color: "transparent"
                    flags: Qt.FramelessWindowHint | Qt.Tool
                    transientParent: root
                    DanmakuCanvas { id: canvas; anchors.fill: parent }
                    Button {
                        id: popupButton
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        anchors.margins: 16
                        text: "视频上方浮层"
                        onClicked: testPopup.open()
                    }
                    Popup {
                        id: testPopup
                        x: Math.max(8, (layer.width - width) / 2)
                        y: Math.max(8, (layer.height - height) / 2)
                        width: Math.min(360, layer.width - 16)
                        padding: 20
                        ColumnLayout {
                            anchors.fill: parent
                            Label { text: "视频上方的 Fluent 浮层"; font.bold: true }
                            Label { Layout.fillWidth: true; text: "此浮层和弹幕位于同一独立透明 Qt Quick 覆盖窗。"; wrapMode: Text.Wrap }
                            Button { text: "关闭浮层"; onClicked: testPopup.close() }
                        }
                    }
                    Component.onCompleted: root.probe.attach(canvas, layer, root)
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 12
            Button { text: root.probe.paused ? "继续" : "暂停"; onClicked: root.probe.togglePause() }
            Button { text: "+10 秒"; onClicked: root.probe.seek(root.probe.position + 10) }
            Button { text: "全屏"; onClicked: root.visibility === Window.FullScreen ? root.showNormal() : root.showFullScreen() }
            Button { text: "截图"; onClicked: root.probe.capture("manual") }
            Label { Layout.fillWidth: true; text: root.probe.status; wrapMode: Text.Wrap }
            Label { text: root.probe.position.toFixed(2) + " / " + root.probe.duration.toFixed(2) + " s" }
        }
    }
}
