import QtQuick
import QtQuick.Controls.FluentWinUI3
import QtQuick.Layouts

ApplicationWindow {
    required property QtObject probe
    id: toolbar
    title: "DanmaX PotPlayer lifecycle probe"
    visible: probe.layerVisible
    width: 500
    height: 156
    x: 80
    y: 80
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        Label { text: toolbar.probe.stage; Layout.fillWidth: true; wrapMode: Text.WordWrap }
        Label { text: "独立验证窗口，不是正式播放界面" }
        Button {
            text: "结束播放（关闭本次播放器）"
            enabled: toolbar.probe.stage === "toolbar-close"
            onClicked: toolbar.probe.endPlayback()
        }
    }
    Window {
        title: "DanmaX lifecycle probe overlay"
        visible: toolbar.probe.layerVisible
        width: 600; height: 70
        x: 620; y: 80
        color: "transparent"
        flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowTransparentForInput | Qt.WindowDoesNotAcceptFocus
        Text {
            anchors.centerIn: parent
            text: "弹幕层生命周期探针 · " + toolbar.probe.stage
            color: "white"
            style: Text.Outline
            styleColor: "black"
            font.pixelSize: 24
        }
    }
}
