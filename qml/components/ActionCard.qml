import QtQuick
import QtQuick.Controls.FluentWinUI3
import QtQuick.Layouts
import "../theme"

ItemDelegate {
    id: card
    property string iconSource: ""
    property string title: ""
    property string description: ""
    property bool isExternal: true
    signal actionTriggered()

    Layout.fillWidth: true
    leftPadding: Ui.cardPaddingX
    rightPadding: Ui.cardPaddingX
    topPadding: Ui.cardPaddingY
    bottomPadding: Ui.cardPaddingY
    implicitHeight: Math.max(Ui.cardMinHeight, contentLayout.implicitHeight + topPadding + bottomPadding)

    Accessible.role: Accessible.Button
    Accessible.name: title + (description.length > 0 ? " " + description : "")

    background: Rectangle {
        radius: Ui.cardRadius
        color: card.down ? (Ui.dark ? Qt.rgba(1, 1, 1, 0.03) : Qt.rgba(0.90, 0.90, 0.90, 0.95))
             : card.hovered ? Ui.cardBackgroundHover
             : Ui.cardBackground
        border.width: card.visualFocus ? 2 : 1
        border.color: card.visualFocus ? Ui.accentColor : Ui.cardBorder

        Behavior on color { ColorAnimation { duration: 100 } }
    }

    contentItem: RowLayout {
        id: contentLayout
        spacing: 14

        FluentIcon {
            id: iconItem
            visible: card.iconSource.length > 0 && !Ui.highContrast
            source: card.iconSource
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
                text: card.title
                font.pixelSize: Ui.bodySize
                font.weight: Font.Normal
                color: Ui.textColor
                Layout.fillWidth: true
                elide: Text.ElideRight
            }

            Label {
                text: card.description
                visible: text.length > 0
                font.pixelSize: Ui.captionSize
                color: Ui.secondaryText
                Layout.fillWidth: true
                elide: Text.ElideRight
            }
        }

        FluentIcon {
            visible: card.isExternal && !Ui.highContrast
            Layout.preferredWidth: 14
            Layout.preferredHeight: 14
            Layout.alignment: Qt.AlignVCenter
            source: Ui.icon("open")
            fillMode: Image.PreserveAspectFit
            opacity: 0.7
            Accessible.ignored: true
        }
    }

    onClicked: {
        card.actionTriggered()
    }
}
