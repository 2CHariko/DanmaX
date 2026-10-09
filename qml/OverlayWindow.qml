import QtQuick
import QtQuick.Window
import DanmaX.Native
import "i18n"
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
            text: overlay.backend.status + "\n" + overlay.backend.mediaTitle + "\n" + overlay.backend.position.toFixed(1) + " s"
                + "\n" + I18n.format(I18n.overlay.activeDanmakuFormat, overlay.backend.metrics.active || 0, overlay.backend.metrics.dropped || 0)
                + "\n" + I18n.format(I18n.overlay.frequencyFormat, Number(overlay.backend.metrics.presentedPerSecond || 0).toFixed(1), Number(overlay.backend.metrics.updatesPerSecond || 0).toFixed(1))
                + "\n" + I18n.format(I18n.overlay.intervalFormat, Number(overlay.backend.metrics.p95Ms || 0).toFixed(1))
                + "\n" + I18n.format(I18n.overlay.memoryFormat, Number(overlay.backend.metrics.memoryMiB || 0).toFixed(1))
                + "\n" + I18n.format(I18n.overlay.textureBudgetFormat, Number((overlay.backend.metrics.textureEstimatedBytes || 0) / 1048576).toFixed(1), Number((overlay.backend.metrics.textureBudgetBytes || 0) / 1048576).toFixed(0))
                + "\n" + I18n.format(I18n.overlay.fallbackFormat, overlay.backend.metrics.textFallbackNodes || 0, ((overlay.backend.metrics.textureFallbackReasons || {}).capacity || 0))
        }
    }
}
