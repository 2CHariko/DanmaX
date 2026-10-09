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
    leftPadding: 16
    rightPadding: 16
    topPadding: 10
    bottomPadding: 10
    implicitHeight: Math.max(52, contentLayout.implicitHeight + topPadding + bottomPadding)

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

        Image {
            id: iconItem
            visible: card.iconSource.length > 0
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

        Item {
            visible: card.isExternal
            Layout.preferredWidth: 14
            Layout.preferredHeight: 14
            Layout.alignment: Qt.AlignVCenter

            Canvas {
                id: extIcon
                anchors.fill: parent
                onPaint: {
                    var ctx = getContext("2d")
                    ctx.clearRect(0, 0, width, height)
                    ctx.strokeStyle = Ui.secondaryText
                    ctx.fillStyle = Ui.secondaryText
                    ctx.lineWidth = 1.3
                    ctx.lineCap = "round"
                    ctx.lineJoin = "round"

                    ctx.beginPath()
                    ctx.moveTo(width - 5, 5)
                    ctx.lineTo(width - 5, height - 2)
                    ctx.lineTo(2, height - 2)
                    ctx.lineTo(2, 5)
                    ctx.lineTo(5, 5)
                    ctx.stroke()

                    ctx.beginPath()
                    ctx.moveTo(5, height - 5)
                    ctx.lineTo(width - 2, 2)
                    ctx.stroke()

                    ctx.beginPath()
                    ctx.moveTo(width - 6, 2)
                    ctx.lineTo(width - 2, 2)
                    ctx.lineTo(width - 2, 6)
                    ctx.stroke()
                }
                Connections {
                    target: Ui
                    function onDarkChanged() { extIcon.requestPaint() }
                    function onHighContrastChanged() { extIcon.requestPaint() }
                }
            }
        }
    }

    onClicked: {
        card.actionTriggered()
    }
}
