import QtQuick
import QtQuick.Controls.FluentWinUI3
import QtQuick.Layouts
import "../theme"

ColumnLayout {
    id: section
    property string title: ""
    default property alias entries: contentArea.data
    Layout.fillWidth: true
    spacing: 8

    // 1. 外置独立分组小标题（与关于页面“反馈”小标题完全统一）
    Label {
        text: section.title
        visible: section.title.length > 0
        font.pixelSize: Ui.sectionHeaderSize
        font.weight: Font.DemiBold
        color: Ui.textColor
        Layout.fillWidth: true
    }

    // 2. 与关于页完全一致的 Fluent 卡片容器（半透明底色 + 1px 细微边框 + 6px 圆角）
    Control {
        id: cardContainer
        Layout.fillWidth: true
        leftPadding: 16
        rightPadding: 16
        topPadding: 16
        bottomPadding: 16

        background: Rectangle {
            radius: Ui.cardRadius
            color: Ui.cardBackground
            border.width: 1
            border.color: Ui.cardBorder
        }

        contentItem: ColumnLayout {
            id: contentArea
            width: parent.width
            spacing: 16
        }
    }
}
