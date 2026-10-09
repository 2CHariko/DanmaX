import QtQuick
import QtQuick.Controls.FluentWinUI3
import QtQuick.Layouts
import "../theme"

RowLayout {
    id: captionButtons
    spacing: 0
    property alias minBtn: minBtn
    property alias maxBtn: maxBtn
    property alias closeBtn: closeBtn
    readonly property Window win: Window.window
    readonly property bool isMaximized: win ? win.visibility === Window.Maximized : false

    // 最小化按钮
    Item {
        id: minBtn
        Layout.preferredWidth: 46
        Layout.preferredHeight: 32

        Rectangle {
            anchors.fill: parent
            color: minArea.pressed ? (Ui.dark ? Qt.rgba(1, 1, 1, 0.05) : Qt.rgba(0, 0, 0, 0.10))
                 : minArea.containsMouse ? (Ui.dark ? Qt.rgba(1, 1, 1, 0.08) : Qt.rgba(0, 0, 0, 0.06))
                 : "transparent"
        }

        Rectangle {
            anchors.centerIn: parent
            width: 10
            height: 1
            color: Ui.textColor
        }

        MouseArea {
            id: minArea
            anchors.fill: parent
            hoverEnabled: true
            Accessible.role: Accessible.Button
            Accessible.name: "最小化"
            onClicked: if (win) win.showMinimized()
        }
    }

    // 最大化 / 还原按钮 (支持 Win11 Snap Layouts 贴靠布局)
    Item {
        id: maxBtn
        Layout.preferredWidth: 46
        Layout.preferredHeight: 32

        Rectangle {
            anchors.fill: parent
            color: maxArea.pressed ? (Ui.dark ? Qt.rgba(1, 1, 1, 0.05) : Qt.rgba(0, 0, 0, 0.10))
                 : maxArea.containsMouse ? (Ui.dark ? Qt.rgba(1, 1, 1, 0.08) : Qt.rgba(0, 0, 0, 0.06))
                 : "transparent"
        }

        // 窗口化状态：单个方框
        Rectangle {
            visible: !captionButtons.isMaximized
            anchors.centerIn: parent
            width: 10
            height: 10
            color: "transparent"
            border.width: 1
            border.color: Ui.textColor
        }

        // 最大化状态：两个重叠方框（还原）
        Item {
            visible: captionButtons.isMaximized
            anchors.centerIn: parent
            width: 10
            height: 10

            Rectangle {
                x: 2; y: 0
                width: 8; height: 8
                color: "transparent"
                border.width: 1
                border.color: Ui.textColor
            }
            Rectangle {
                x: 0; y: 2
                width: 8; height: 8
                color: Ui.dark ? "#202020" : "#FFFFFF"
                border.width: 1
                border.color: Ui.textColor
            }
        }

        MouseArea {
            id: maxArea
            anchors.fill: parent
            hoverEnabled: true
            Accessible.role: Accessible.Button
            Accessible.name: captionButtons.isMaximized ? "向下还原" : "最大化"
            onClicked: {
                if (win) {
                    if (captionButtons.isMaximized) win.showNormal()
                    else win.showMaximized()
                }
            }
        }
    }

    // 关闭按钮
    Item {
        id: closeBtn
        Layout.preferredWidth: 46
        Layout.preferredHeight: 32

        Rectangle {
            anchors.fill: parent
            color: closeArea.pressed ? "#B32014"
                 : closeArea.containsMouse ? "#C42B1C"
                 : "transparent"
        }

        Canvas {
            id: closeCanvas
            anchors.centerIn: parent
            width: 10
            height: 10
            onPaint: {
                var ctx = getContext("2d")
                ctx.clearRect(0, 0, width, height)
                ctx.strokeStyle = closeArea.containsMouse ? "#FFFFFF" : Ui.textColor
                ctx.lineWidth = 1.1
                ctx.beginPath()
                ctx.moveTo(0, 0)
                ctx.lineTo(width, height)
                ctx.moveTo(width, 0)
                ctx.lineTo(0, height)
                ctx.stroke()
            }
            Connections {
                target: closeArea
                function onContainsMouseChanged() { closeCanvas.requestPaint() }
            }
            Connections {
                target: Ui
                function onDarkChanged() { closeCanvas.requestPaint() }
            }
        }

        MouseArea {
            id: closeArea
            anchors.fill: parent
            hoverEnabled: true
            Accessible.role: Accessible.Button
            Accessible.name: "关闭"
            onClicked: if (win) win.close()
        }
    }
}
