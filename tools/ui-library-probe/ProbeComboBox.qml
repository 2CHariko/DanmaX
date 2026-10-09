import QtQuick
import QtQuick.Templates as T
import FluentUI

// Geometry and interaction only: original Fluent popup/list/delegate/background.
FluComboBox {
    id: control
    property real popupLimit: 360
    readonly property var list: popup.contentItem
    property var bar: null
    property real room: 0
    property real popupX: 0
    property real popupY: height
    function place() {
        const w = control.Window.window
        if (!w) return
        const origin = control.mapToItem(w.contentItem, 0, 0)
        const desired = Math.min(popupLimit, list.contentHeight + popup.topPadding + popup.bottomPadding)
        const below = Math.max(0, w.height - origin.y - height - 8)
        const above = Math.max(0, origin.y - 8)
        const down = below >= desired || below >= above
        room = down ? below : above
        popupY = down ? height : -Math.min(desired, room)
        popupX = Math.max(8, Math.min(origin.x, w.width - popup.width - 8)) - origin.x
    }
    function refresh() {
        if (!popup.visible) return
        place()
        Qt.callLater(() => list.positionViewAtIndex(Math.max(0, currentIndex), ListView.Contain))
    }
    popup.height: Math.min(popupLimit, room, list.contentHeight + popup.topPadding + popup.bottomPadding)
    popup.width: Math.min(control.width, control.Window.window ? control.Window.window.width - 16 : control.width)
    popup.x: popupX
    popup.y: popupY
    popup.topMargin: 8
    popup.bottomMargin: 8
    delegate: FluItemDelegate {
        width: Math.max(0, control.list.width - (control.bar ? control.bar.width : 0) - 8)
        text: control.textRole ? (Array.isArray(control.model) ? modelData[control.textRole] : model[control.textRole]) : modelData
        palette.text: control.palette.text
        font: control.font
        palette.highlightedText: control.palette.highlightedText
        highlighted: control.highlightedIndex === index
        hoverEnabled: control.hoverEnabled
    }
    onWidthChanged: refresh()
    Connections {
        target: control.Window.window
        function onWidthChanged() { control.refresh() }
        function onHeightChanged() { control.refresh() }
    }
    Connections {
        target: control.popup
        function onAboutToShow() { control.place() }
        function onOpened() { control.refresh() }
    }
    Component {
        id: scrollbar
        FluScrollBar {
            objectName: "popupScrollBar"
            Accessible.name: "选项滚动条"
            policy: size < 1 ? T.ScrollBar.AlwaysOn : T.ScrollBar.AlwaysOff
        }
    }
    Component.onCompleted: {
        bar = scrollbar.createObject(list)
        list.T.ScrollBar.vertical = bar
    }
}
