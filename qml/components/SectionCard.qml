import QtQuick
import QtQuick.Controls.FluentWinUI3
import QtQuick.Layouts
import "../theme"

Rectangle {
    id: card
    property string title
    default property alias entries: body.data
    Layout.fillWidth: true
    implicitHeight: body.implicitHeight + 32
    color: Ui.groupSurface
    radius: 8
    border.color: Ui.borderColor
    border.width: 1
    ColumnLayout {
        id: body
        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 16 }
        spacing: 12
        Label { visible: text.length > 0; text: card.title; font.pixelSize: Ui.bodySize; font.weight: Font.DemiBold; Layout.fillWidth: true }
    }
}
