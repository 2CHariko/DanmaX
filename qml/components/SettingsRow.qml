import QtQuick
import QtQuick.Controls.FluentWinUI3
import QtQuick.Layouts
import "../theme"

// Independent WinUI 3 SettingsCard component for settings rows.
Control {
    id: row
    property string title: ""
    property string description: ""
    property string iconSource: ""
    default property alias editor: editors.data

    Layout.fillWidth: true
    leftPadding: Ui.cardPaddingX
    rightPadding: Ui.cardPaddingX
    topPadding: Ui.cardPaddingY
    bottomPadding: Ui.cardPaddingY
    implicitHeight: Math.max(Ui.cardMinHeight, contentItem.implicitHeight + topPadding + bottomPadding)

    background: Rectangle {
        radius: Ui.cardRadius
        color: Ui.cardBackground
        border.width: 1
        border.color: Ui.cardBorder
    }

    contentItem: GridLayout {
        id: grid
        columns: row.width < 560 * Ui.textScale ? 1 : 2
        columnSpacing: 16
        rowSpacing: 8

        RowLayout {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            spacing: 12

            Image {
                id: iconItem
                visible: row.iconSource.length > 0
                source: row.iconSource
                Layout.preferredWidth: 20
                Layout.preferredHeight: 20
                fillMode: Image.PreserveAspectFit
                Layout.alignment: Qt.AlignVCenter
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignVCenter
                spacing: 2

                Label {
                    text: row.title
                    font.pixelSize: Ui.bodySize
                    font.weight: Font.Normal
                    color: Ui.textColor
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }

                Label {
                    text: row.description
                    visible: text.length > 0
                    font.pixelSize: Ui.captionSize
                    color: Ui.secondaryText
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
            }
        }

        Item {
            Layout.fillWidth: grid.columns === 1
            Layout.alignment: grid.columns === 1 ? (Qt.AlignLeft | Qt.AlignVCenter) : (Qt.AlignRight | Qt.AlignVCenter)
            Layout.preferredWidth: editors.implicitWidth
            implicitWidth: editors.implicitWidth
            implicitHeight: editors.implicitHeight

            RowLayout {
                id: editors
                width: Math.min(implicitWidth, parent.width)
                x: grid.columns === 1 ? 0 : parent.width - width
                anchors.verticalCenter: parent.verticalCenter
                spacing: 8
            }
        }
    }
}
