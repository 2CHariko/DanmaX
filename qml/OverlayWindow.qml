import QtQuick
import QtQuick.Window
import DanmaX.Native
import "i18n"
Window {
    id:overlay
    required property QtObject backend
    transientParent: null
    objectName: "danmakuOverlay"
    property alias renderer:canvas
    color:"transparent"
    flags: Qt.FramelessWindowHint | Qt.Tool | Qt.WindowTransparentForInput | Qt.WindowDoesNotAcceptFocus
    DanmakuCanvas { id: canvas; anchors.fill: parent; visible: overlay.backend ? overlay.backend.danmakuVisible : true }
    Rectangle {
        id: debugPanel
        objectName: "overlayDebugPanel"
        visible:overlay.backend.settings.values.debug
        color:"#b0202020";radius:4
        readonly property real edgeMargin: Math.min(16, overlay.width / 2)
        property real textPixelSize: 14
        width: Math.min(320 * textPixelSize / 14, Math.max(0, overlay.width - 2 * edgeMargin))
        height: debugContent.height + 24
        x:overlay.backend.settings.values.debugPosition.endsWith("right")?overlay.width-width-edgeMargin:edgeMargin
        y:overlay.backend.settings.values.debugPosition.startsWith("bottom")?overlay.height-height-16:16

        component HudText: Text {
            width: parent.width
            color: "white"
            font.pixelSize: debugPanel.textPixelSize
            textFormat: Text.PlainText
            wrapMode: Text.WrapAnywhere
        }

        Column {
            id: debugContent
            x: 12; y: 12
            width: Math.max(0, parent.width - 24)
            HudText {
                objectName: "overlayDebugStatus"
                text: overlay.backend.status
                wrapMode: Text.NoWrap
                elide: Text.ElideRight
                maximumLineCount: 1
            }
            HudText {
                objectName: "overlayDebugTitle"
                // Normalize presentation only; media identity and reports keep the full title.
                text: overlay.backend.mediaTitle.replace(/[\r\n\t\u2028\u2029]+/g, " ").trim()
                visible: text.length > 0
                maximumLineCount: 2
                elide: Text.ElideRight
            }
            HudText {
                objectName: "overlayDebugMetrics"
                text: overlay.backend.position.toFixed(1) + " s"
                + "\n" + I18n.format(I18n.overlay.activeDanmakuFormat, overlay.backend.metrics.active || 0, overlay.backend.metrics.dropped || 0)
                + "\n" + I18n.format(I18n.overlay.frequencyFormat, Number(overlay.backend.metrics.presentedPerSecond || 0).toFixed(1), Number(overlay.backend.metrics.updatesPerSecond || 0).toFixed(1))
                + "\n" + I18n.format(I18n.overlay.framePolicyFormat,
                    Number(overlay.backend.metrics.effectiveFrameRateLimit || 0).toFixed(1),
                    Number(overlay.backend.metrics.effectiveTargetFrameRate || 0).toFixed(1),
                    overlay.backend.metrics.admissionLimit || 0, overlay.backend.metrics.adaptiveSkipped || 0)
                + "\n" + I18n.settings.adaptiveStates[overlay.backend.metrics.adaptiveFrameRateState || "disabled"]
                + "\n" + I18n.format(I18n.overlay.presentedIntervalFormat, Number(overlay.backend.metrics.presentedP95Ms || 0).toFixed(1))
                + "\n" + I18n.format(I18n.overlay.intervalFormat, Number(overlay.backend.metrics.p95Ms || 0).toFixed(1))
                + "\n" + I18n.format(I18n.overlay.memoryFormat, Number(overlay.backend.metrics.memoryMiB || 0).toFixed(1))
                + "\n" + I18n.format(I18n.overlay.textureBudgetFormat, Number((overlay.backend.metrics.textureEstimatedBytes || 0) / 1048576).toFixed(1), Number((overlay.backend.metrics.textureBudgetBytes || 0) / 1048576).toFixed(0))
                + "\n" + I18n.format(I18n.overlay.fallbackFormat, overlay.backend.metrics.textFallbackNodes || 0, ((overlay.backend.metrics.textureFallbackReasons || {}).capacity || 0))
            }
        }
    }
}
