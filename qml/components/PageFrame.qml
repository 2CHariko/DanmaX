import QtQuick
import QtQuick.Controls.FluentWinUI3
import QtQuick.Layouts
import "../theme"
import "../i18n"

ScrollView {
    id: page
    property string title
    property string description
    default property alias entries: contentArea.data
    readonly property alias verticalBar: verticalBar
    readonly property alias bodyItem: body
    clip: true
    rightPadding: verticalBar.width + 2 * Ui.scrollGap
    contentWidth: availableWidth
    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
    ScrollBar.vertical: ScrollBar {
        id: verticalBar
        objectName: "pageVerticalScrollBar"
        parent: page
        x: page.width - width - Ui.scrollGap
        y: page.topPadding
        height: page.availableHeight
        Accessible.name: I18n.common.pageScrollBarAccessible
    }
    function focusHeading() { contentItem.contentY = 0; heading.forceActiveFocus() }
    WheelHandler {
        id: wheelHandler
        target: null
        acceptedDevices: PointerDevice.Mouse
        orientation: Qt.Vertical
        blocking: true
        onWheel: (event) => {
            if (event.pixelDelta.y === 0 && event.angleDelta.y !== 0) {
                const flick = page.contentItem
                if (flick && flick.contentHeight > page.availableHeight) {
                    const step = (event.angleDelta.y / 120.0) * Math.round(108 * Ui.textScale)
                    const maxContentY = flick.contentHeight - page.availableHeight
                    flick.contentY = Math.max(0, Math.min(maxContentY, flick.contentY - step))
                }
                event.accepted = true
            } else {
                event.accepted = false
            }
        }
    }
    function reveal(item) {
        const point = item.mapToItem(body, 0, 0)
        contentItem.contentY = Math.max(0, Math.min(point.y, contentItem.contentHeight - availableHeight))
        item.forceActiveFocus()
    }
    ColumnLayout {
        id: body
        width: page.availableWidth
        spacing: 0
        ColumnLayout {
            id: contentArea
            Layout.fillWidth: true
            spacing: Ui.sectionGap
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 8
                Label { id: heading; text: page.title; font.pixelSize: Ui.titleSize; font.weight: Font.DemiBold; color: Ui.textColor; Accessible.role: Accessible.Heading }
                Label { visible: text.length > 0; text: page.description; color: Ui.secondaryText; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            }
        }
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: page.width < Ui.expandedNavigationWidth ? Ui.pageMarginNarrow : Ui.pagePadding
        }
    }
}
