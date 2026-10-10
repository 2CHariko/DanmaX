import QtQuick
import QtQuick.Controls.FluentWinUI3
import QtQuick.Layouts
import "../theme"
import "../i18n"

// The official popup has no ScrollBar; retain only public geometry/scroll adaptations.
ComboBox {
    id: control
    property bool renderFontFamily: false
    property real popupLimit: Ui.comboPopupHeight
    readonly property real desiredPopupWidth: Math.max(control.width, renderFontFamily ? 280 : 160)
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
        const actualWidth = Math.min(desiredPopupWidth, window.width - 2 * margin)
        popupOffsetX = Math.max(margin, Math.min(origin.x, window.width - margin - actualWidth)) - origin.x
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
    onDesiredPopupWidthChanged: refreshPopup()
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
    popup.width: Math.max(0, Math.min(desiredPopupWidth,
        control.Window.window ? control.Window.window.width - 2 * Ui.scrollGap : desiredPopupWidth))
    popup.x: popupOffsetX
    popup.y: popupOffsetY
    popup.topMargin: Ui.scrollGap
    popup.bottomMargin: Ui.scrollGap
    popup.leftMargin: Ui.scrollGap
    popup.rightMargin: Ui.scrollGap
    popup.topPadding: 4
    popup.bottomPadding: 4
    popup.leftPadding: 4
    popup.rightPadding: (listScrollBar ? listScrollBar.width : 0) + 2 * Ui.scrollGap

    popup.background: Rectangle {
        radius: 8
        color: Ui.dark ? "#2C2C2C" : "#FCFCFC"
        border.width: 1
        border.color: Ui.cardBorder
    }

    delegate: ItemDelegate {
        id: itemDelegate
        required property var model
        required property int index

        width: control.popupList ? control.popupList.width : control.width
        implicitHeight: 36
        highlighted: control.highlightedIndex === index
        hoverEnabled: control.hoverEnabled

        background: Rectangle {
            radius: 4
            color: itemDelegate.down ? (Ui.dark ? Qt.rgba(1, 1, 1, 0.05) : Qt.rgba(0, 0, 0, 0.08))
                 : (itemDelegate.hovered || itemDelegate.highlighted) ? (Ui.dark ? Qt.rgba(1, 1, 1, 0.08) : Qt.rgba(0, 0, 0, 0.05))
                 : "transparent"
        }

        contentItem: RowLayout {
            spacing: 8

            Rectangle {
                visible: control.currentIndex === itemDelegate.index
                Layout.preferredWidth: 3
                Layout.preferredHeight: 16
                radius: 1.5
                color: Ui.accentColor
                Layout.leftMargin: 2
            }

            Item {
                visible: control.currentIndex !== itemDelegate.index
                Layout.preferredWidth: 3
                Layout.leftMargin: 2
            }

            Label {
                id: itemLabel
                text: control.textAt(itemDelegate.index) || (itemDelegate.model && control.textRole ? itemDelegate.model[control.textRole] : (typeof itemDelegate.modelData === "string" ? itemDelegate.modelData : ""))
                color: Ui.textColor
                font.pixelSize: Ui.bodySize
                font.family: control.renderFontFamily ? itemLabel.text : ""
                elide: Text.ElideRight
                Layout.fillWidth: true
                verticalAlignment: Text.AlignVCenter
            }
        }
    }

    Component {
        id: scrollComponent
        ScrollBar {
            objectName: "comboPopupScrollBar"
            parent: control.popupList.parent
            x: control.popupList.x + control.popupList.width + Ui.scrollGap
            y: control.popupList.y
            height: control.popupList.height
            Accessible.name: I18n.common.optionsScrollBarAccessible
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
