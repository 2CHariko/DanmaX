import QtQuick
import QtQuick.Window
import QtQuick.Controls.FluentWinUI3
import QtQuick.Layouts
import DanmaX.Probe

ApplicationWindow {
    id: root
    required property QtObject probe
    title: "DanmaX · 同父子窗验证"
    width: 1120; height: 760
    minimumWidth: 720; minimumHeight: 480
    color: "#202020"
    // Defer native children until C++ has fixed the host format, before any HWND
    // can be handed to VLC. The child Quick surface still requests alpha.
    property bool surfacesReady: false
    property bool layerEnabled: true
    visible: false
    onClosing: function(close) { close.accepted = false; probe.close() }
    Shortcut { sequence: "Space"; onActivated: probe.togglePause() }
    Shortcut { sequence: "F11"; onActivated: root.visibility === Window.FullScreen ? root.showNormal() : root.showFullScreen() }
    Shortcut { sequence: "Escape"; enabled: !testPopup.visible; onActivated: root.showNormal() }
    function showTestPopup() { testPopup.open() }
    function closeTestPopup() { testPopup.close() }
    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        Label {
            Layout.fillWidth: true; Layout.margins: 12
            text: "同父窗口 · 原生视频子窗＋透明穿透弹幕子窗（绿色边框是视口参考线）"
            color: "white"; wrapMode: Text.Wrap
        }
        Item {
            id: viewport
            Layout.fillWidth: true; Layout.fillHeight: true
            Layout.leftMargin: 12; Layout.rightMargin: 12
            Loader {
                anchors.fill: parent
                active: root.surfacesReady
                sourceComponent: Component {
                    Item {
                        WindowContainer { anchors.fill: parent; window: root.probe.videoWindow }
                        WindowContainer {
                            anchors.fill: parent
                            visible: root.layerEnabled
                            window: Window {
                                id: layer
                                title: "DanmaX child danmaku surface"
                                color: "transparent"
                                flags: Qt.FramelessWindowHint | Qt.WindowTransparentForInput | Qt.WindowDoesNotAcceptFocus
                                transientParent: null
                                DanmakuCanvas { id: canvas; anchors.fill: parent }
                                Rectangle { anchors.fill: parent; color: "transparent"; border.width: 2; border.color: "#50E0A0" }
                                Text { anchors.left: parent.left; anchors.bottom: parent.bottom; anchors.margins: 8; text: "CHILD LAYER · 点击应穿透到视频"; color: "#50E0A0" }
                                Component.onCompleted: root.probe.attach(canvas, layer, root)
                            }
                        }
                    }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true; Layout.margins: 12
            Button { text: root.probe.paused ? "继续" : "暂停"; onClicked: root.probe.togglePause() }
            Button { text: "+10 秒"; onClicked: root.probe.seek(root.probe.position + 10) }
            Button { text: "全屏"; onClicked: root.visibility === Window.FullScreen ? root.showNormal() : root.showFullScreen() }
            Button { text: "浮层"; onClicked: testPopup.open() }
            Button { text: "截图"; onClicked: root.probe.capture("manual") }
            Button { text: "弹幕层"; checkable: true; checked: true; onToggled: root.layerEnabled = checked }
            Label { Layout.fillWidth: true; text: root.probe.status; color: "white"; wrapMode: Text.Wrap }
        }
    }
    Popup {
        id: testPopup
        popupType: Popup.Window
        parent: root.contentItem
        modal: true
        x: (root.width - width) / 2; y: (root.height - height) / 2
        width: Math.min(380, root.width - 32); padding: 20
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        ColumnLayout {
            anchors.fill: parent
            Label { text: "原生视频上方的 Fluent 浮层"; font.bold: true }
            Label { Layout.fillWidth: true; text: "弹幕子窗穿透输入，交互浮层使用独立 Popup.Window。"; wrapMode: Text.Wrap }
            Button { text: "关闭浮层"; onClicked: testPopup.close() }
        }
    }
}
