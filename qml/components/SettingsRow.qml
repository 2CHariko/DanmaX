import QtQuick
import "../theme"
import QtQuick.Controls.FluentWinUI3
import QtQuick.Layouts
Pane {
    id: row
    property string title
    property string description
    default property alias editor: editorLayout.data
    padding: 16
    Layout.fillWidth: true
    implicitHeight: grid.implicitHeight + topPadding + bottomPadding
    GridLayout {
        id: grid
        anchors.fill: parent
        columns: row.width < 560 ? 1 : 2
        columnSpacing: 24
        rowSpacing: 8
        ColumnLayout {
            Layout.fillWidth: true
            Label { id: titleLabel; text: row.title; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            Label { text: row.description; visible: text.length > 0; wrapMode: Text.WordWrap; Layout.fillWidth: true; font.pixelSize: Ui.captionSize }
        }
        RowLayout { id: editorLayout; Layout.alignment: grid.columns === 1 ? Qt.AlignLeft : Qt.AlignRight; Layout.minimumWidth: implicitWidth; Layout.maximumWidth: implicitWidth; spacing: 8 }
    }
}
