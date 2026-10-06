import QtQuick
import "../theme"
import QtQuick.Controls.FluentWinUI3
import QtQuick.Layouts
import QtQuick.Dialogs
ColumnLayout {
    id: page
    required property QtObject backend
    spacing: 16
    Label { text: "日志"; font.pixelSize: Ui.titleSize; font.weight: Font.DemiBold }
    Flow {
        Layout.fillWidth: true
        spacing: 8
        Button { text: "清空"; onClicked: page.backend.logs.clear() }
        Button { text: "导出"; onClicked: exportDialog.open() }
        ComboBox { id: filter; model: ["全部", "WARNING", "ERROR"]; Accessible.name: "日志过滤" }
        Label { text: page.backend.logs.count + " 条 · 最多保留 1000 条" }
    }
    ListView {
        id: list
        Layout.fillWidth: true; Layout.fillHeight: true
        clip: true
        model: page.backend.logs
        spacing: 0
        ScrollBar.vertical: ScrollBar {}
        delegate: Item {
            id: entry
            required property string time
            required property string level
            required property string message
            width: list.width - 16
            readonly property bool matched: filter.currentIndex === 0 || level === filter.currentText
            visible: matched
            height: matched ? line.implicitHeight + 8 : 0
            Label {
                id: line
                width: parent.width
                text: entry.time + "  [" + entry.level + "]  " + entry.message
                wrapMode: Text.WrapAnywhere
                Accessible.name: text
            }
        }
    }
    FileDialog { id: exportDialog; title: "导出日志"; fileMode: FileDialog.SaveFile; nameFilters: ["文本文件 (*.txt)"]; onAccepted: page.backend.exportLogs(selectedFile.toString()) }
}
