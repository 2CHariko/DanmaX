import QtQuick
import QtQuick.Controls.FluentWinUI3
import "../theme"

// Retain official visuals and drag behavior; route wheels outside the viewport.
ScrollBar {
    id: bar
    required property Item viewport
    property Flickable scrollTarget: viewport
    property var verticalWheelHandler: null
    // Fallback belongs to the complete container, including its padding.
    property Item wheelRegion: viewport.parent
    property real wheelTop: 0
    property real wheelBottom: wheelRegion ? wheelRegion.height : 0
    property var marginWheelVertical: null
    property var marginWheelHorizontal: null
    property real trailingEdge: orientation === Qt.Vertical ? parent.width : parent.height
    parent: viewport.parent
    x: orientation === Qt.Vertical ? trailingEdge - width - Ui.scrollEdgeInset : viewport.x
    y: orientation === Qt.Horizontal ? trailingEdge - height - Ui.scrollEdgeInset : viewport.y
    width: orientation === Qt.Horizontal ? viewport.width : implicitWidth
    height: orientation === Qt.Vertical ? viewport.height : implicitHeight
    policy: ScrollBar.AsNeeded

    function scrollBy(pixels, angle) {
        const flick = scrollTarget
        if (!flick || !flick.enabled || !flick.interactive) return false
        const vertical = orientation === Qt.Vertical
        const origin = vertical ? flick.originY : flick.originX
        const leading = vertical ? flick.topMargin : flick.leftMargin
        const trailing = vertical ? flick.bottomMargin : flick.rightMargin
        const extent = vertical ? flick.contentHeight - flick.height : flick.contentWidth - flick.width
        const minimum = origin - leading
        const maximum = Math.max(minimum, origin + extent + trailing)
        const step = pixels || angle / 120 * Ui.wheelScrollStep
        const position = vertical ? flick.contentY : flick.contentX
        flick.cancelFlick()
        const next = Math.max(minimum, Math.min(maximum, position - step))
        if (vertical) flick.contentY = next
        else flick.contentX = next
        // Own the axis even at its bounds, so an outer list cannot also scroll.
        return true
    }

    function handleWheel(wheel) {
        const pixels = wheel.pixelDelta.x !== 0 || wheel.pixelDelta.y !== 0
        const dx = pixels ? wheel.pixelDelta.x : wheel.angleDelta.x
        const dy = pixels ? wheel.pixelDelta.y : wheel.angleDelta.y
        const shifted = (wheel.modifiers & Qt.ShiftModifier) !== 0
        if (orientation === Qt.Vertical)
            return !shifted && dy !== 0 && Math.abs(dy) >= Math.abs(dx)
                && scrollBy(pixels ? dy : 0, pixels ? 0 : dy)
        if (!shifted && dy !== 0 && Math.abs(dy) >= Math.abs(dx)) {
            if (typeof verticalWheelHandler !== "function") return false
            verticalWheelHandler(pixels ? dy : 0, pixels ? 0 : dy)
            return true
        }
        const delta = dx || (shifted ? dy : 0)
        return delta !== 0 && scrollBy(pixels ? delta : 0, pixels ? 0 : delta)
    }

    function handleMarginWheel(wheel, axis) {
        if (!wheelRegion || !viewport || !viewport.visible || wheel.y < wheelTop || wheel.y >= wheelBottom) return false
        const point = viewport.mapFromItem(wheelRegion, wheel.x, wheel.y)
        // Descendants retain native input handling; only uncovered padding falls back.
        if (point.x >= 0 && point.x < viewport.width && point.y >= 0 && point.y < viewport.height)
            return false
        const pixels = wheel.pixelDelta.x !== 0 || wheel.pixelDelta.y !== 0
        const dx = pixels ? wheel.pixelDelta.x : wheel.angleDelta.x
        const dy = pixels ? wheel.pixelDelta.y : wheel.angleDelta.y
        const dominant = Math.abs(dy) >= Math.abs(dx) ? Qt.Vertical : Qt.Horizontal
        return axis === dominant && handleWheel(wheel)
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.NoButton
        onWheel: wheel => { wheel.accepted = bar.handleWheel(wheel) }
    }

    Component {
        id: marginWheelFactory
        WheelHandler {
            required property QtObject ownerBar
            objectName: orientation === Qt.Vertical ? "scrollMarginVertical" : "scrollMarginHorizontal"
            parent: ownerBar ? ownerBar.wheelRegion : null
            target: null
            enabled: ownerBar && ownerBar.visible && ownerBar.enabled
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
            blocking: true
            onWheel: event => { event.accepted = ownerBar ? ownerBar.handleMarginWheel(event, orientation) : false }
        }
    }
    Component.onCompleted: {
        marginWheelVertical = marginWheelFactory.createObject(bar, {ownerBar: bar, orientation: Qt.Vertical})
        marginWheelHorizontal = marginWheelFactory.createObject(bar, {ownerBar: bar, orientation: Qt.Horizontal})
    }
    Component.onDestruction: {
        // Their event parent can outlive this scrollbar (e.g. a replaced popup list).
        if (marginWheelVertical) marginWheelVertical.destroy()
        if (marginWheelHorizontal) marginWheelHorizontal.destroy()
    }
}
