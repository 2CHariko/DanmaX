import QtQuick
import QtQuick.Layouts
import "../theme"
import "../i18n"

Item {
    id: placeholder
    property bool loading: false
    property bool hasContent: false
    property string appearance: "rows"
    readonly property bool pending: loading && !hasContent
    readonly property bool showing: visible && revealed
    readonly property bool pulsing: showing && Ui.motionEnabled
    property bool revealed: false
    property bool wasShown: false
    property real pulse: 1
    signal contentReady()

    Layout.fillWidth: true
    visible: pending
    implicitHeight: {
        if (appearance === "field") return 36 * Ui.textScale
        if (appearance === "episodes") {
            const columns = Math.max(1, Math.floor((width + 8) / (52 * Ui.textScale + 8)))
            return Math.ceil(12 / columns) * (36 * Ui.textScale + 8) - 8
        }
        return (appearance === "hero" ? 280 : appearance === "cards" ? 258 : appearance === "rows" ? 228 : 220) * Ui.textScale
    }
    Accessible.role: Accessible.StaticText
    Accessible.name: I18n.common.loading
    Accessible.ignored: !showing

    // Reserve the layout immediately, but do not flash placeholders for cache hits.
    onPendingChanged: {
        if (!pending) {
            const animate = wasShown && hasContent && parent && parent.visible
            wasShown = false
            revealed = false
            if (animate) contentReady()
        }
    }
    onVisibleChanged: {
        if (!visible && pending) { revealed = false; wasShown = false }
    }
    Timer {
        interval: Ui.skeletonDelay
        running: placeholder.visible && placeholder.pending && !placeholder.revealed
        onTriggered: { placeholder.revealed = true; placeholder.wasShown = true }
    }
    SequentialAnimation on pulse {
        running: placeholder.pulsing
        loops: Animation.Infinite
        NumberAnimation { from: 1; to: 0.55; duration: 800; easing.type: Easing.InOutSine }
        NumberAnimation { from: 0.55; to: 1; duration: 800; easing.type: Easing.InOutSine }
    }
    readonly property var blocks: {
        const w = Math.max(0, width), s = Ui.textScale, shapes = []
        function block(x, y, bw, bh) { shapes.push({ x: x, y: y, w: Math.max(0, bw), h: bh }) }
        if (appearance === "field") block(0, 0, w, 36 * s)
        else if (appearance === "episodes") {
            const cw = 52 * s, ch = 36 * s, cols = Math.max(1, Math.floor((w + 8) / (cw + 8)))
            for (let i = 0; i < 12; ++i) block((i % cols) * (cw + 8), Math.floor(i / cols) * (ch + 8), Math.min(cw, w), ch)
        } else if (appearance === "cards") {
            const cw = Math.min(w, 140 * s), count = Math.min(6, Math.max(1, Math.floor((w + 12) / (cw + 12))))
            block(0, 0, Math.min(w, 180 * s), 20 * s)
            for (let i = 0; i < count; ++i) {
                const x = i * (cw + 12)
                block(x, 40 * s, cw, 168 * s)
                block(x, 220 * s, cw * 0.85, 12 * s)
                block(x, 242 * s, cw * 0.55, 10 * s)
            }
        } else if (appearance === "hero") {
            const poster = Math.min(w * 0.45, 280 * s), tw = Math.max(0, w - poster - 24)
            block(w - poster, 0, poster, 280 * s)
            block(0, 24 * s, tw * 0.9, 24 * s)
            block(0, 72 * s, tw * 0.65, 12 * s)
            block(0, 112 * s, tw, 12 * s)
            block(0, 140 * s, tw * 0.8, 12 * s)
            block(0, 212 * s, tw * 0.6, 36 * s)
        } else if (appearance === "rows") {
            for (let i = 0; i < 3; ++i) {
                const y = i * 76 * s, x = Math.min(60 * s, w * 0.3), textWidth = Math.max(0, w - x)
                block(0, y, Math.min(48 * s, w * 0.24), 64 * s)
                block(x, y + 8 * s, textWidth * 0.85, 14 * s)
                block(x, y + 34 * s, textWidth * 0.6, 10 * s)
            }
        } else {
            const poster = Math.min(130 * s, w * 0.35), x = Math.min(w, poster + 16), tw = Math.max(0, w - x)
            block(0, 0, poster, 180 * s)
            block(x, 8 * s, tw * 0.9, 24 * s)
            block(x, 52 * s, tw * 0.65, 12 * s)
            block(x, 84 * s, tw, 12 * s)
            block(x, 112 * s, tw * 0.8, 12 * s)
            block(x, 152 * s, tw * 0.5, 28 * s)
        }
        return shapes
    }
    Item {
        anchors.fill: parent
        opacity: placeholder.showing ? (placeholder.pulsing ? placeholder.pulse : 1) : 0
        Repeater {
            model: placeholder.blocks
            delegate: Rectangle {
                required property var modelData
                x: modelData.x; y: modelData.y
                width: modelData.w; height: modelData.h
                radius: Ui.cardRadius
                color: Ui.skeletonFill
                Accessible.ignored: true
            }
        }
    }
}
