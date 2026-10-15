import QtQuick
import "../theme"

NumberAnimation {
    id: animation
    property: "opacity"
    from: 0
    to: 1
    duration: Ui.motionEnabled ? Ui.contentTransitionDuration : 0
    easing.type: Easing.OutCubic
    // A setting change or navigation must not leave content half transparent.
    onRunningChanged: if (!running && target) target.opacity = 1
    property Connections preferences: Connections {
        target: Ui
        function onMotionEnabledChanged() { if (!Ui.motionEnabled) animation.complete() }
    }
    property Connections visibility: Connections {
        target: animation.target
        function onVisibleChanged() { if (animation.target && !animation.target.visible) animation.complete() }
    }
}
