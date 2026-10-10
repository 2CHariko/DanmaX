import QtQuick
import QtQuick.Controls.FluentWinUI3
import QtQuick.Layouts
import "../theme"

ColumnLayout {
    id: section
    property string title: ""
    property string description: ""
    property bool card: false
    default property alias entries: contentArea.data
    Layout.fillWidth: true
    spacing: 8

    // 1. 外置独立分组小标题与说明
    ColumnLayout {
        Layout.fillWidth: true
        spacing: 4
        visible: section.title.length > 0 || section.description.length > 0

        Label {
            text: section.title
            visible: section.title.length > 0
            font.pixelSize: Ui.sectionHeaderSize
            font.weight: Font.DemiBold
            color: Ui.textColor
            Layout.fillWidth: true
        }

        Label {
            text: section.description
            visible: section.description.length > 0
            font.pixelSize: Ui.captionSize
            color: Ui.secondaryText
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
    }

    // 2. 内容区域容器（根据 card 属性决定是轻量垂直流还是大卡片底板）
    Control {
        id: cardContainer
        Layout.fillWidth: true
        leftPadding: section.card ? Ui.cardPaddingX : 0
        rightPadding: section.card ? Ui.cardPaddingX : 0
        topPadding: section.card ? Ui.cardPaddingY : 0
        bottomPadding: section.card ? Ui.cardPaddingY : 0

        background: Rectangle {
            visible: section.card
            opacity: section.card ? 1 : 0
            radius: Ui.cardRadius
            color: Ui.cardBackground
            border.width: 1
            border.color: Ui.cardBorder
        }

        contentItem: ColumnLayout {
            id: contentArea
            width: parent.width
            spacing: section.card ? 16 : Ui.cardGap
        }
    }
}
