import QtQuick
import QtQuick.Controls.FluentWinUI3
import QtQuick.Layouts
ColumnLayout {
    id: section
    property string title
    default property alias entries: rows.data
    spacing: 8
    Label { text: section.title; font.weight: Font.DemiBold; Layout.topMargin: 16; Layout.bottomMargin: 4 }
    ColumnLayout { id: rows; Layout.fillWidth: true; spacing: 8 }
}
