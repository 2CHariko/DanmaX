import QtQuick
import "../theme"

Item {
    id: placeholder
    property bool loading: false
    property bool revealed: false
    property bool registered: false
    readonly property bool pulsing: visible && loading && revealed && Ui.motionEnabled
    Accessible.ignored: true

    function syncAnimation() {
        if (registered === pulsing) return
        Ui.activeCoverPlaceholders = Math.max(0, Ui.activeCoverPlaceholders + (pulsing ? 1 : -1))
        registered = pulsing
    }
    onPulsingChanged: syncAnimation()
    onLoadingChanged: revealed = false
    onVisibleChanged: if (!visible) revealed = false
    Component.onCompleted: syncAnimation()
    Component.onDestruction: if (registered) Ui.activeCoverPlaceholders = Math.max(0, Ui.activeCoverPlaceholders - 1)
    Timer {
        interval: Ui.skeletonDelay
        running: placeholder.visible && placeholder.loading && !placeholder.revealed
        onTriggered: placeholder.revealed = true
    }
    Rectangle {
        anchors.fill: parent
        visible: !placeholder.loading || placeholder.revealed
        radius: Ui.cardRadius
        color: Ui.skeletonFill
        opacity: placeholder.pulsing ? Ui.coverPulse : 1
        Column {
            anchors.centerIn: parent
            width: parent.width * 0.6
            spacing: 8 * Ui.textScale
            Rectangle { width: parent.width; height: 10 * Ui.textScale; radius: 4; color: Ui.skeletonFill }
            Rectangle { width: parent.width * 0.65; height: 10 * Ui.textScale; radius: 4; color: Ui.skeletonFill }
        }
    }
}
