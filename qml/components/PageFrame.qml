import QtQuick
import QtQuick.Controls.FluentWinUI3
import QtQuick.Layouts
import "../theme"

ScrollView {
    id: page
    property string title
    property string description
    default property alias entries: body.data
    readonly property alias verticalBar: verticalBar
    readonly property alias bodyItem: body
    clip: true
    rightPadding: verticalBar.width + 2 * Ui.scrollGap
    contentWidth: availableWidth
    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
    ScrollBar.vertical: ContentScrollBar {
        id: verticalBar
        objectName: "pageVerticalScrollBar"
        parent: page
        x: page.width - width - Ui.scrollGap
        y: page.topPadding
        height: page.availableHeight
        Accessible.name: "页面滚动条"
    }
    function focusHeading() { contentItem.contentY = 0; heading.forceActiveFocus() }
    function reveal(item) {
        const point = item.mapToItem(body, 0, 0)
        contentItem.contentY = Math.max(0, Math.min(point.y, contentItem.contentHeight - availableHeight))
        item.forceActiveFocus()
    }
    ColumnLayout {
        id: body
        width: Math.min(page.availableWidth, Ui.settingsWidth)
        spacing: 16
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 8
            Label { id: heading; text: page.title; font.pixelSize: Ui.titleSize; font.weight: Font.DemiBold; Accessible.role: Accessible.Heading }
            Label { visible: text.length > 0; text: page.description; color: Ui.secondaryText; wrapMode: Text.WordWrap; Layout.fillWidth: true }
        }
    }
}
