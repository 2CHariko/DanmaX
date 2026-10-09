import QtQuick
import QtQuick.Controls.FluentWinUI3
import QtQuick.Layouts
import "../theme"

// Layout only: editor visuals remain owned by the Qt style.
GridLayout {
    id: row
    property string title
    property string description
    default property alias editor: editors.data
    Layout.fillWidth: true
    columns: width < 560 * Ui.textScale ? 1 : 2
    columnSpacing: 24
    rowSpacing: 8
    ColumnLayout {
        Layout.fillWidth: true
        spacing: 4
        Label { text: row.title; color: Ui.textColor; wrapMode: Text.WordWrap; Layout.fillWidth: true }
        Label { text: row.description; color: Ui.secondaryText; visible: text.length > 0; wrapMode: Text.WordWrap; Layout.fillWidth: true }
    }
    Item {
        Layout.fillWidth: true
        Layout.preferredWidth: editors.implicitWidth
        implicitHeight: editors.implicitHeight
        RowLayout {
            id: editors
            width: Math.min(implicitWidth, parent.width)
            x: row.columns === 1 ? 0 : parent.width - width
            spacing: 8
        }
    }
}
