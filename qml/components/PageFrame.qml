import QtQuick
import QtQuick.Controls.FluentWinUI3
import QtQuick.Layouts
import "../theme"
import "../i18n"

ScrollView {
    id: page
    property string title
    property string description
    property bool headingVisible: true
    default property alias entries: contentArea.data
    readonly property alias verticalBar: verticalBar
    readonly property alias bodyItem: body
    readonly property real contentInset: Ui.scrollInset(verticalBar.implicitWidth,
        Ui.pageMargin(Window.window ? Window.window.width : width))
    clip: true
    leftPadding: contentInset
    rightPadding: contentInset
    contentWidth: availableWidth
    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
    ScrollBar.vertical: AppScrollBar {
        id: verticalBar
        objectName: "pageVerticalScrollBar"
        viewport: page.contentItem
        wheelRegion: page
        Accessible.name: I18n.common.pageScrollBarAccessible
    }
    function focusHeading() { contentItem.contentY = 0; heading.forceActiveFocus() }
    function scrollVertical(pixels, angle) {
        verticalBar.scrollBy(pixels, angle)
    }
    WheelHandler {
        id: wheelHandler
        // This handler intentionally preserves content-area mouse wheel behavior.
        // AppScrollBar supplies the separate full-container padding fallback.
        parent: page.contentItem ? page.contentItem.contentItem : null
        target: null
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        orientation: Qt.Vertical
        blocking: true
        onWheel: (event) => {
            const pixels = event.pixelDelta.x !== 0 || event.pixelDelta.y !== 0
            const dx = pixels ? event.pixelDelta.x : event.angleDelta.x
            const dy = pixels ? event.pixelDelta.y : event.angleDelta.y
            if (!(event.modifiers & Qt.ShiftModifier) && dy !== 0 && Math.abs(dy) >= Math.abs(dx)) {
                page.scrollVertical(pixels ? dy : 0, pixels ? 0 : dy)
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
                visible: page.headingVisible
                spacing: 8
                Label { id: heading; objectName: "pageHeading"; text: page.title; font.pixelSize: Ui.titleSize; font.weight: Font.DemiBold; color: Ui.textColor; Accessible.role: Accessible.Heading }
                Label { visible: text.length > 0; text: page.description; color: Ui.secondaryText; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            }
        }
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: page.contentInset
        }
    }
}
