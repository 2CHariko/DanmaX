import QtQuick
import QtQuick.Window
import "components"

Window {
    id: toolbarWin
    objectName: "danmakuToolbarWindow"
    required property QtObject backend
    property Window overlayWindow: null
    transientParent: overlayWindow

    color: "transparent"
    flags: Qt.FramelessWindowHint | Qt.Tool | Qt.WindowDoesNotAcceptFocus

    // 屏幕居中吸顶定位
    x: overlayWindow ? (overlayWindow.x + Math.round((overlayWindow.width - width) / 2)) : 0
    y: overlayWindow ? overlayWindow.y : 0
    screen: overlayWindow ? overlayWindow.screen : null
    visible: presentationAllowed && phase !== "hidden"

    // Content intent and HWND lifetime are independent. Keep full geometry during exit.
    property string phase: "hidden"
    readonly property bool contentExpanded: toolbar.expanded
    readonly property bool overlayShown: overlayWindow && overlayWindow.visible
    readonly property bool presentationAllowed: overlayShown && backend.toolbarPresentationAllowed
    onContentExpandedChanged: syncPresentation()
    onOverlayShownChanged: syncPresentation()
    onPresentationAllowedChanged: syncPresentation()
    function syncPresentation() {
        transitionTimer.stop()
        if (!presentationAllowed) {
            phase = "hidden"
        } else if (contentExpanded) {
            phase = "opening"
            transitionTimer.start()
        } else if (phase !== "hidden") {
            phase = "closing"
            transitionTimer.start()
        }
    }

    Timer {
        id: transitionTimer
        interval: 200 // Both the 180ms slide and 160ms fade must finish before hiding.
        repeat: false
        onTriggered: {
            if (toolbar.transitionRunning) {
                restart()
                return
            }
            toolbarWin.phase = toolbar.expanded && toolbarWin.presentationAllowed ? "shown" : "hidden"
        }
    }

    width: toolbar.targetWidth
    height: toolbar.targetHeight

    Binding {
        target: toolbarWin.backend.toolbarPointer
        property: "activationWidth"
        value: Math.round(toolbar.activationWidth)
    }

    DanmakuToolbar {
        id: toolbar
        backend: toolbarWin.backend
        maximumWidth: toolbarWin.overlayWindow ? toolbarWin.overlayWindow.width : 1920
        presentationAllowed: toolbarWin.presentationAllowed && toolbarWin.backend.toolbarPointer.enabled
    }
}
