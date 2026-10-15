import QtQuick
import QtQuick.Controls.FluentWinUI3
import "../theme"

// Layout adapter only: retain the official visuals, hit area and interaction.
ScrollBar {
    required property Item viewport
    property real trailingEdge: orientation === Qt.Vertical ? parent.width : parent.height
    parent: viewport.parent
    x: orientation === Qt.Vertical ? trailingEdge - width - Ui.scrollEdgeInset : viewport.x
    y: orientation === Qt.Horizontal ? trailingEdge - height - Ui.scrollEdgeInset : viewport.y
    width: orientation === Qt.Horizontal ? viewport.width : implicitWidth
    height: orientation === Qt.Vertical ? viewport.height : implicitHeight
    policy: ScrollBar.AsNeeded
}
