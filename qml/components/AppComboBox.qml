import QtQuick
import QtQuick.Controls.FluentWinUI3
import "../theme"
import "../i18n"

// Public geometry, motion and scroll adaptations; no private Fluent properties.
ComboBox {
    id: control
    property bool renderFontFamily: false
    property string enabledRole: ""
    property real popupLimit: Ui.comboPopupHeight
    readonly property bool popupCoversControl: !editable && currentIndex >= 0 && currentIndex < count
    readonly property bool faceplateHidden: popupCoversControl && popup.visible
    readonly property real popupRowHeight: Math.max(36, Math.ceil(rowMetrics.height) + 2 * Ui.popupItemPadding)
    readonly property real desiredPopupWidth: Math.max(control.width + (popupCoversControl ? 8 : 0), renderFontFamily ? 280 : 160)
    readonly property var popupList: popup.contentItem
    property var listScrollBar: null
    property real popupRoom: 0
    property real popupOffsetX: 0
    property real popupOffsetY: height
    property bool popupDownward: true
    property real popupInitialContentY: 0
    property real popupAnchorRatio: 0
    property real popupRevealProgress: 1
    readonly property real popupTargetHeight: Math.min(desiredPopupHeight, popupRoom)
    readonly property real desiredPopupHeight: Math.min(popupLimit,
        count * popupRowHeight + popup.topPadding + popup.bottomPadding)

    FontMetrics { id: rowMetrics; font: control.font }

    function placePopup() {
        const window = control.Window.window
        if (!window) return
        const margin = Ui.popupEdgeMargin
        const origin = control.mapToItem(window.contentItem, 0, 0)
        const available = Math.max(0, window.height - 2 * margin)
        if (popupCoversControl && available >= popupRowHeight + popup.topPadding + popup.bottomPadding) {
            const center = origin.y + control.height / 2
            const padding = popup.topPadding + popup.bottomPadding
            const maxRows = Math.max(1, Math.floor((Math.min(popupLimit, available) - padding) / popupRowHeight))
            const aboveRows = Math.max(0, Math.floor((center - margin - popup.topPadding - popupRowHeight / 2) / popupRowHeight))
            const belowRows = Math.max(0, Math.floor((window.height - margin - center - popup.bottomPadding - popupRowHeight / 2) / popupRowHeight))
            let before = Math.min(currentIndex, aboveRows, Math.floor((maxRows - 1) / 2))
            const after = Math.min(count - currentIndex - 1, belowRows, maxRows - 1 - before)
            before = Math.min(currentIndex, aboveRows, maxRows - 1 - after)
            popupRoom = Math.min(available, (before + after + 1) * popupRowHeight + padding)
            const targetY = center - popup.topPadding - (before + 0.5) * popupRowHeight
            popupOffsetY = Math.max(margin, Math.min(targetY, window.height - margin - popupRoom)) - origin.y
            popupInitialContentY = (currentIndex - before) * popupRowHeight
            popupAnchorRatio = Math.max(0, Math.min(1, (center - origin.y - popupOffsetY) / popupRoom))
            popupDownward = before === 0
        } else {
            const below = Math.max(0, window.height - margin - origin.y - control.height)
            const above = Math.max(0, origin.y - margin)
            const downward = below >= desiredPopupHeight || below >= above
            popupDownward = downward
            popupRoom = Math.min(available, downward ? below : above)
            const actual = Math.min(desiredPopupHeight, popupRoom)
            const targetY = downward ? origin.y + control.height : origin.y - actual
            popupOffsetY = Math.max(margin, Math.min(targetY, window.height - margin - actual)) - origin.y
            popupAnchorRatio = downward ? 0 : 1
        }
        const actualWidth = Math.min(desiredPopupWidth, window.width - 2 * margin)
        const targetX = origin.x - (popupCoversControl ? 4 : 0)
        popupOffsetX = Math.max(margin, Math.min(targetX, window.width - margin - actualWidth)) - origin.x
    }
    function alignSelectedRow() {
        if (!popup.visible || !popupCoversControl) return
        popupList.forceLayout()
        const revealOffset = (popupTargetHeight - popup.height) * popupAnchorRatio
        popupList.contentY = popupList.originY + popupInitialContentY + revealOffset
    }
    function keepCurrentVisible() {
        const index = highlightedIndex >= 0 ? highlightedIndex : currentIndex
        if (popup.visible && index >= 0)
            popupList.positionViewAtIndex(index, ListView.Contain)
    }
    function refreshPopup() {
        if (!popup.visible) return
        placePopup()
        Qt.callLater(function() {
            if (popupCoversControl) alignSelectedRow()
            else keepCurrentVisible()
        })
    }
    onWidthChanged: refreshPopup()
    onHeightChanged: refreshPopup()
    onDesiredPopupHeightChanged: refreshPopup()
    onDesiredPopupWidthChanged: refreshPopup()
    onEditableChanged: refreshPopup()
    onPopupRevealProgressChanged: if (popup.visible && !popup.opened) alignSelectedRow()
    onHighlightedIndexChanged: Qt.callLater(keepCurrentVisible)
    onCurrentIndexChanged: Qt.callLater(keepCurrentVisible)
    Keys.onPressed: event => {
        if (event.key === Qt.Key_F4 || (event.key === Qt.Key_Down && (event.modifiers & Qt.AltModifier))) {
            if (popup.visible) popup.close()
            else popup.open()
            event.accepted = true
        }
    }

    // Only the visuals are hidden: keep the original control, focus and editor alive.
    contentItem.opacity: faceplateHidden ? 0 : 1
    indicator.opacity: faceplateHidden ? 0 : 1
    background.opacity: faceplateHidden ? 0 : 1
    popup.parent: control
    popup.height: popupTargetHeight * popupRevealProgress
    popup.width: Math.max(0, Math.min(desiredPopupWidth,
        control.Window.window ? control.Window.window.width - 2 * Ui.popupEdgeMargin : desiredPopupWidth))
    popup.x: popupOffsetX
    // Reveal around the selected row; editable/empty fields retain edge anchoring.
    popup.y: popupOffsetY + (popupTargetHeight - popup.height) * popupAnchorRatio
    popup.topMargin: Ui.popupEdgeMargin
    popup.bottomMargin: Ui.popupEdgeMargin
    popup.leftMargin: Ui.popupEdgeMargin
    popup.rightMargin: Ui.popupEdgeMargin
    popup.topPadding: 4
    popup.bottomPadding: 4
    popup.leftPadding: 4
    popup.rightPadding: Ui.scrollInset(listScrollBar ? listScrollBar.implicitWidth : 0)

    popup.background: PopupSurface {
        // The backplate spans the gutter; the text viewport and scrollbar remain separate.
        Item {
            x: 4; y: 4
            width: parent.width - 8; height: Math.max(0, parent.height - 8)
            clip: true
            Rectangle {
                objectName: "comboPopupHighlight"
                readonly property Item row: control.popupList ? control.popupList.currentItem : null
                visible: row !== null && row.enabled && (row.highlighted || row.hovered || row.down)
                y: row ? row.y - control.popupList.contentY : 0
                width: parent.width
                height: row ? row.height : 0
                radius: 4
                color: row && row.down ? Ui.popupItemPressed : Ui.popupItemHover
            }
        }
    }
    popup.leftInset: 0
    popup.rightInset: 0
    popup.topInset: 0
    popup.bottomInset: 0
    popup.enter: Transition {
        NumberAnimation {
            target: control
            property: "popupRevealProgress"
            from: Ui.motionEnabled ? 0.33 : 1
            to: 1
            duration: Ui.popupOpenDuration
            easing.type: Easing.OutCubic
        }
    }

    delegate: ItemDelegate {
        id: itemDelegate
        required property var model
        required property int index
        enabled: control.enabledRole.length === 0 || !control.model
            || !control.model[index] || control.model[index][control.enabledRole] !== false

        width: control.popupList ? control.popupList.width : control.width
        font: control.font
        // Explicit symmetric padding also avoids the disabled Fluent delegate's
        // undefined verticalOffset reference in Qt 6.11.
        topPadding: Ui.popupItemPadding
        bottomPadding: Ui.popupItemPadding
        leftPadding: Ui.popupTextInset
        rightPadding: Ui.popupItemPadding
        implicitHeight: control.popupRowHeight
        highlighted: control.highlightedIndex === index
        hoverEnabled: control.hoverEnabled

        background: Item {}
        Rectangle {
            objectName: "comboSelectionIndicator"
            visible: control.currentIndex === itemDelegate.index
            x: 2; anchors.verticalCenter: parent.verticalCenter
            width: 3; height: 16; radius: 1.5
            color: Ui.accentColor
        }
        contentItem: Label {
            id: itemLabel
            text: control.textAt(itemDelegate.index) || (itemDelegate.model && control.textRole ? itemDelegate.model[control.textRole] : (typeof itemDelegate.modelData === "string" ? itemDelegate.modelData : ""))
            color: !itemDelegate.enabled ? Ui.secondaryText
                : Ui.highContrast && (itemDelegate.highlighted || itemDelegate.hovered) ? Ui.colors.highlightedText : Ui.textColor
            // An empty family is an explicit override, not font inheritance.
            font.family: control.renderFontFamily ? itemLabel.text : itemDelegate.font.family
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
        }
    }

    Component {
        id: scrollComponent
        AppScrollBar {
            objectName: "comboPopupScrollBar"
            viewport: control.popupList
            Accessible.name: I18n.common.optionsScrollBarAccessible
        }
    }
    Component.onCompleted: {
        if (contentItem) {
            contentItem.Keys.onPressed.connect(function(event) {
                if (event.key === Qt.Key_F4 || (event.key === Qt.Key_Down && (event.modifiers & Qt.AltModifier))) {
                    if (popup.visible) popup.close()
                    else popup.open()
                    event.accepted = true
                }
            })
        }
        listScrollBar = scrollComponent.createObject(popupList)
        popupList.ScrollBar.vertical = listScrollBar
    }
    Connections {
        target: control.popup
        function onAboutToShow() { control.placePopup() }
        function onOpened() {
            if (control.popupCoversControl) control.alignSelectedRow()
            else control.keepCurrentVisible()
        }
    }
    Connections {
        target: control.Window.window
        function onWidthChanged() { control.refreshPopup() }
        function onHeightChanged() { control.refreshPopup() }
    }
}
