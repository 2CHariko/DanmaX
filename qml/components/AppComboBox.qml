import QtQuick
import QtQuick.Controls.FluentWinUI3
import "../theme"

// Keep the official control and popup ListView. Only geometry, scrollbar and
// the explicitly permitted solid popup background are adapted here.
ComboBox {
    id: control
    property real popupLimit: Ui.comboPopupHeight
    readonly property var popupList: popup.contentItem
    property var listScrollBar: null
    property real popupRoom: 0
    property real popupOffsetX: 0
    property real popupOffsetY: height
    readonly property real desiredPopupHeight: Math.min(popupLimit,
        popupList.contentHeight + popup.topPadding + popup.bottomPadding)

    function placePopup() {
        const window = control.Window.window
        if (!window) return
        const margin = Ui.scrollGap
        const origin = control.mapToItem(window.contentItem, 0, 0)
        const available = Math.max(0, window.height - 2 * margin)
        const below = Math.max(0, window.height - margin - origin.y - control.height)
        const above = Math.max(0, origin.y - margin)
        const downward = below >= desiredPopupHeight || below >= above
        popupRoom = Math.min(available, downward ? below : above)
        const actual = Math.min(desiredPopupHeight, popupRoom)
        const targetY = downward ? origin.y + control.height : origin.y - actual
        popupOffsetY = Math.max(margin, Math.min(targetY, window.height - margin - actual)) - origin.y
        popupOffsetX = Math.max(margin, Math.min(origin.x, window.width - margin - popup.width)) - origin.x
    }
    function keepCurrentVisible() {
        const index = highlightedIndex >= 0 ? highlightedIndex : currentIndex
        if (popup.visible && index >= 0)
            popupList.positionViewAtIndex(index, ListView.Contain)
    }
    function refreshPopup() {
        if (!popup.visible) return
        placePopup()
        Qt.callLater(keepCurrentVisible)
    }
    onWidthChanged: refreshPopup()
    onHeightChanged: refreshPopup()
    onDesiredPopupHeightChanged: refreshPopup()
    onHighlightedIndexChanged: Qt.callLater(keepCurrentVisible)
    onCurrentIndexChanged: Qt.callLater(keepCurrentVisible)
    Keys.onPressed: event => {
        if (event.key === Qt.Key_F4 || (event.key === Qt.Key_Down && (event.modifiers & Qt.AltModifier))) {
            if (popup.visible) popup.close()
            else popup.open()
            event.accepted = true
        }
    }

    popup.height: Math.min(desiredPopupHeight, popupRoom)
    popup.width: Math.max(0, Math.min(control.width,
        control.Window.window ? control.Window.window.width - 2 * Ui.scrollGap : control.width))
    popup.x: popupOffsetX
    popup.y: popupOffsetY
    popup.topMargin: Ui.scrollGap
    popup.bottomMargin: Ui.scrollGap
    popup.leftMargin: Ui.scrollGap
    popup.rightMargin: Ui.scrollGap
    popup.background: Rectangle {
        objectName: "comboPopupSurface"
        color: Ui.popupSurface
        border.color: Ui.popupBorder
        radius: Ui.popupRadius
    }
    delegate: ItemDelegate {
        required property int index
        width: Math.max(0, control.popupList.width - (control.listScrollBar ? control.listScrollBar.width : 0) - Ui.scrollGap)
        text: control.textAt(index)
        highlighted: control.highlightedIndex === index
        hoverEnabled: control.hoverEnabled
        Accessible.name: text
        ToolTip.visible: hovered || activeFocus
        ToolTip.text: text
        ToolTip.delay: 500
    }
    Component {
        id: scrollComponent
        ContentScrollBar {
            objectName: "comboPopupScrollBar"
            Accessible.name: "选项列表滚动条"
        }
    }
    Component.onCompleted: {
        listScrollBar = scrollComponent.createObject(popupList)
        popupList.ScrollBar.vertical = listScrollBar
    }
    Connections {
        target: control.popup
        function onAboutToShow() { control.placePopup() }
        function onOpened() { control.refreshPopup() }
    }
    Connections {
        target: control.Window.window
        function onWidthChanged() { control.refreshPopup() }
        function onHeightChanged() { control.refreshPopup() }
    }
}
