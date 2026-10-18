import QtQuick
import QtQuick.Window
import QtQuick.Controls.FluentWinUI3
import QtQuick.Layouts
import DanmaX.Probe

ApplicationWindow {
    id: root
    required property QtObject probe
    title: "DanmaX · 顶层弹幕跟随验证"
    width: 1120; height: 760
    minimumWidth: 720; minimumHeight: 480
    color: "#202020"
    visible: true
    onClosing: function(close) { close.accepted = false; probe.close() }
    Shortcut { sequence: "Space"; onActivated: probe.togglePause() }
    Shortcut { sequence: "F11"; onActivated: root.visibility === Window.FullScreen ? root.showNormal() : root.showFullScreen() }
    Shortcut { sequence: "Escape"; enabled: !testPopup.visible; onActivated: root.showNormal() }
    function showTestPopup() { testPopup.open() }
    function closeTestPopup() { testPopup.close() }
    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        Item {
            objectName: "followTitleBar"
            Layout.fillWidth: true; Layout.preferredHeight: 44
            Label { anchors.left: parent.left; anchors.leftMargin: 12; anchors.verticalCenter: parent.verticalCenter; text: root.title; color: "white" }
        }
        Item {
            id: viewport
            Layout.fillWidth: true; Layout.fillHeight: true
            Layout.leftMargin: 12; Layout.rightMargin: 12
            WindowContainer { anchors.fill: parent; window: root.probe.videoWindow }
            Item {
                Window {
                    id: layer
                    title: "DanmaX owned follow layer"
                    color: "transparent"
                    flags: Qt.FramelessWindowHint | Qt.Tool | Qt.WindowTransparentForInput | Qt.WindowDoesNotAcceptFocus
                    transientParent: root
                    DanmakuCanvas { id: canvas; anchors.fill: parent }
                    Rectangle { anchors.fill: parent; color: "transparent"; border.width: 2; border.color: "#50E0A0" }
                    Text { anchors.bottom: parent.bottom; anchors.left: parent.left; anchors.margins: 8; text: "OWNED OVERLAY · 原生视频区域参考线"; color: "#50E0A0" }
                    Component.onCompleted: { root.probe.attachViewport(viewport); root.probe.attach(canvas, layer, root) }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true; Layout.margins: 12
            Button { text: root.probe.paused ? "继续" : "暂停"; onClicked: root.probe.togglePause() }
            Button { text: "全屏"; onClicked: root.visibility === Window.FullScreen ? root.showNormal() : root.showFullScreen() }
            Button { text: "浮层"; onClicked: testPopup.open() }
            Button { text: "最小化"; onClicked: root.showMinimized() }
            Button { text: "最大化"; onClicked: root.visibility === Window.Maximized ? root.showNormal() : root.showMaximized() }
            Button { text: "关闭"; onClicked: root.probe.close() }
            Label { Layout.fillWidth: true; text: root.probe.status; color: "white"; wrapMode: Text.Wrap }
        }
    }
    Popup {
        id: testPopup
        parent: root.contentItem; popupType: Popup.Window; modal: true
        x: (root.width - width) / 2; y: (root.height - height) / 2
        width: Math.min(380, root.width - 32); padding: 20
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        ColumnLayout {
            anchors.fill: parent
            Label { text: "独立顶层 Fluent 浮层"; font.bold: true }
            Label { Layout.fillWidth: true; text: "视频、穿透弹幕与业务浮层分别呈现。"; wrapMode: Text.Wrap }
            Button { text: "关闭浮层"; onClicked: testPopup.close() }
        }
    }
}
