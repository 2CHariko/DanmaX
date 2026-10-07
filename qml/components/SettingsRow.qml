import QtQuick
import "../theme"
import QtQuick.Controls.FluentWinUI3
import QtQuick.Layouts
Rectangle {
    id: row
    property string title
    property string description
    default property alias editor: editorLayout.data
    color: Ui.groupSurface
    radius: 8
    border.color: Ui.borderColor
    Layout.fillWidth: true
    implicitHeight: grid.implicitHeight + 32
    GridLayout {
        id: grid
        anchors.fill: parent
        anchors.margins: 16
        columns: row.width < 680 * Ui.textScale ? 1 : 2
        columnSpacing: 24
        rowSpacing: 8
        ColumnLayout {
            Layout.fillWidth: true
            Label { id: titleLabel; text: row.title; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            Label { text: row.description; color: Ui.secondaryText; visible: text.length > 0; wrapMode: Text.WordWrap; Layout.fillWidth: true; font.pixelSize: Ui.captionSize }
        }
        Item {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.preferredWidth: 320
            Layout.maximumWidth: grid.columns === 1 ? grid.width : 320
            implicitHeight: editorLayout.implicitHeight
            RowLayout {
                id: editorLayout
                width: Math.min(implicitWidth, parent.width)
                x: grid.columns === 1 ? 0 : parent.width - width
                spacing: 8
            }
        }
    }
}
