import QtQuick
import QtQuick.Window

Window {
    id: overlay
    required property QtObject preview
    color: "transparent"
    flags: Qt.FramelessWindowHint | Qt.Tool | Qt.WindowStaysOnTopHint
           | Qt.WindowTransparentForInput | Qt.WindowDoesNotAcceptFocus
    x: screen.virtualX
    y: screen.virtualY
    width: screen.width
    height: screen.height
    Text {
        text: "本地弹幕 · Qt Quick 覆盖层预览"
        font.family: "Microsoft YaHei"
        font.pixelSize: 30
        font.bold: true
        color: "white"
        style: Text.Outline
        styleColor: "#20242b"
        y: 90
        x: overlay.width - ((overlay.preview.elapsedSeconds * 180) % (overlay.width + width))
    }
}
