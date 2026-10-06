import QtQuick
import QtQuick.Window
import LocalDanmaku.Native
Window {
    id:overlay
    required property QtObject backend
    property alias renderer:canvas
    color:"transparent"
    flags:Qt.FramelessWindowHint|Qt.Tool|Qt.WindowTransparentForInput|Qt.WindowDoesNotAcceptFocus
    DanmakuCanvas { id:canvas;anchors.fill:parent }
    Rectangle {
        visible:overlay.backend.settings.values.debug
        color:"#b0202020";radius:4
        width:debugText.implicitWidth+24;height:debugText.implicitHeight+24
        x:overlay.backend.settings.values.debugPosition.endsWith("right")?overlay.width-width-16:16
        y:overlay.backend.settings.values.debugPosition.startsWith("bottom")?overlay.height-height-16:16
        Text {
            id:debugText;anchors.centerIn:parent;color:"white";font.pixelSize:14
            text:overlay.backend.status+"\n"+overlay.backend.mediaTitle+"\n"+overlay.backend.position.toFixed(1)+" s"
                +"\n在屏 "+(overlay.backend.metrics.active||0)+" / 丢弃 "+(overlay.backend.metrics.dropped||0)
                +"\n更新 "+(overlay.backend.metrics.updatesPerSecond||0)+" Hz / P95 "+Number(overlay.backend.metrics.p95Ms||0).toFixed(1)+" ms"
                +"\n内存 "+Number(overlay.backend.metrics.memoryMiB||0).toFixed(1)+" MiB"
        }
    }
}
