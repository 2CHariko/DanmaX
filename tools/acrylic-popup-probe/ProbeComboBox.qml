import QtQuick
import QtQuick.Controls.FluentWinUI3

ComboBox {
    id: control
    property real rowHeight: Math.max(36, font.pixelSize + 20)
    property real targetY: height
    property real targetHeight: rowHeight + 8
    property real initialOffset: 0
    property real alignmentError: 0
    readonly property bool faceplateHidden: !editable && popup.visible
    function prepare() {
        const window = control.Window.window
        const origin = mapToItem(window.contentItem, 0, 0)
        const center = origin.y + height / 2
        const selected = Math.max(0, currentIndex)
        const maxRows = Math.max(1, Math.floor((Math.min(360, window.height - 16) - 8) / rowHeight))
        const above = Math.max(0, Math.floor((center - 8 - 4 - rowHeight / 2) / rowHeight))
        const below = Math.max(0, Math.floor((window.height - 8 - center - 4 - rowHeight / 2) / rowHeight))
        let before = Math.min(selected, above, Math.floor((maxRows - 1) / 2))
        const after = Math.min(Math.max(0, count - selected - 1), below, maxRows - 1 - before)
        before = Math.min(selected, above, maxRows - 1 - after)
        targetHeight = (before + after + 1) * rowHeight + 8
        targetY = height / 2 - rowHeight / 2 - 4 - before * rowHeight
        initialOffset = (selected - before) * rowHeight
        if (editable) {
            targetY = height
            targetHeight = Math.min(360, window.height - origin.y - height - 8, count * rowHeight + 8)
        }
        popup.contentItem.forceLayout()
        popup.contentItem.contentY = initialOffset
    }
    function measureAlignment() {
        const list = popup.contentItem
        alignmentError = popup.y + popup.topPadding + currentIndex * rowHeight - list.contentY + rowHeight / 2 - height / 2
    }
    onHighlightedIndexChanged: if (popup.opened) popup.contentItem.positionViewAtIndex(highlightedIndex, ListView.Contain)
    contentItem.opacity: faceplateHidden ? 0 : 1
    indicator.opacity: faceplateHidden ? 0 : 1
    background.opacity: faceplateHidden ? 0 : 1
    popup.popupType: Popup.Window
    popup.parent: control
    popup.focus: true
    popup.height: targetHeight
    popup.width: width
    popup.y: targetY
    popup.padding: 4
    popup.rightPadding: 24
    popup.topInset: 0
    popup.bottomInset: 0
    popup.leftInset: 0
    popup.rightInset: 0
    popup.enter: Transition {}
    popup.exit: Transition {}
    popup.background: ProbeSurface {}
    popup.contentItem: ListView {
        id: list
        focus: true
        Keys.forwardTo: [control]
        clip: true
        model: control.delegateModel
        currentIndex: control.highlightedIndex
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {
            parent: list.parent
            x: list.x + list.width + 8
            y: list.y
            height: list.height
            policy: ScrollBar.AsNeeded
        }
    }
    delegate: ItemDelegate {
        required property int index
        width: control.popup.availableWidth
        height: control.rowHeight
        text: control.textAt(index)
        highlighted: control.highlightedIndex === index
        topPadding: bottomPadding
        background: Rectangle { radius: 4; color: parent.highlighted ? (Application.styleHints.colorScheme === Qt.Dark ? "#18FFFFFF" : "#10000000") : "transparent" }
        Rectangle { x: 2; anchors.verticalCenter: parent.verticalCenter; width: 3; height: 16; radius: 1.5; color: control.palette.accent; visible: control.currentIndex === index }
    }
    Connections {
        target: control.popup
        function onAboutToShow() { control.prepare() }
        function onOpened() {
            control.popup.contentItem.forceLayout()
            control.popup.contentItem.contentY = control.initialOffset
            control.measureAlignment()
        }
    }
}
