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

    component CaptionButton: ToolButton {
        Layout.preferredWidth: 46
        Layout.preferredHeight: 32
        icon.width: 16
        icon.height: 16
        display: AbstractButton.IconOnly
        activeFocusOnTab: true
        Accessible.name: text
        ToolTip.visible: hovered
        ToolTip.text: text
    }
    CaptionButton {
        id: minBtn
        objectName: "captionMinimize"
        text: "最小化"
        icon.source: Ui.icon("subtract")
        onClicked: if (captionButtons.win) captionButtons.win.showMinimized()
    }
    CaptionButton {
        id: maxBtn
        objectName: "captionMaximize"
        text: captionButtons.isMaximized ? "向下还原" : "最大化"
        icon.source: Ui.icon(captionButtons.isMaximized ? "square-multiple" : "square")
        onClicked: {
            if (captionButtons.win) {
                if (captionButtons.isMaximized) captionButtons.win.showNormal()
                else captionButtons.win.showMaximized()
            }
        }
    }
    CaptionButton {
        id: closeBtn
        objectName: "captionClose"
        text: "关闭"
        icon.source: Ui.icon("dismiss")
        icon.color: hovered || down ? Ui.captionCloseText : palette.buttonText
        background: Rectangle {
            color: closeBtn.down ? Ui.captionClosePressed
                : closeBtn.hovered ? Ui.captionCloseHover : "transparent"
            border.width: closeBtn.visualFocus ? 2 : 0
            border.color: closeBtn.palette.accent
        }
        onClicked: if (captionButtons.win) captionButtons.win.close()
    }
}
