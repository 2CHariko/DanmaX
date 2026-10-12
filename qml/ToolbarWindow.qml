import QtQuick
import QtQuick.Window
import "components"

Window {
    id: toolbarWin
    required property QtObject backend
    property Window overlayWindow: null

    color: "transparent"
    flags: Qt.FramelessWindowHint | Qt.Tool | Qt.WindowDoesNotAcceptFocus

    // 屏幕居中吸顶定位
    x: overlayWindow ? (overlayWindow.x + Math.round((overlayWindow.width - width) / 2)) : 0
    y: overlayWindow ? overlayWindow.y : 0
    screen: overlayWindow ? overlayWindow.screen : null
    visible: overlayWindow ? overlayWindow.visible : false

    // 窗口物理尺寸：展开时扩大为工具栏尺寸，收起时平滑收缩为 80x10 微型把手
    property bool windowExpanded: toolbar.expanded
    onWindowExpandedChanged: {
        if (toolbar.expanded) {
            shrinkTimer.stop()
            windowExpanded = true
        } else {
            shrinkTimer.restart()
        }
    }

    Timer {
        id: shrinkTimer
        interval: 200 // 等待内部平滑淡出动画结束后再收缩窗口物理边界
        repeat: false
        onTriggered: {
            if (!toolbar.expanded)
                toolbarWin.windowExpanded = false
        }
    }

    width: windowExpanded ? toolbar.targetWidth : 80
    height: windowExpanded ? toolbar.targetHeight : 10

    DanmakuToolbar {
        id: toolbar
        backend: toolbarWin.backend
    }
}
