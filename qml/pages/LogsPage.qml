import QtQuick
import "../theme"
import "../components"
import QtQuick.Controls.FluentWinUI3
import QtQuick.Layouts
import QtQuick.Dialogs
ColumnLayout {
    id: page
    required property QtObject backend
    readonly property int matchingCount: {
        const revision = backend.logs.count
        return backend.logs.countForLevel(filter.currentIndex === 0 ? "" : filter.currentText)
    }
    function focusHeading() { heading.forceActiveFocus() }
    spacing: 24
    Label { id: heading; text: "日志"; font.pixelSize: Ui.titleSize; font.weight: Font.DemiBold; Accessible.role: Accessible.Heading }
    ColumnLayout {
        Layout.fillWidth: true
        Layout.maximumWidth: Ui.settingsWidth
        spacing: 8
        GridLayout {
            Layout.fillWidth: true
            columns: page.width < 600 ? 1 : 2
            rowSpacing: 8
            RowLayout {
                Layout.fillWidth: true
                Label { text: "日志级别" }
                AppComboBox { id: filter; objectName: "logFilter"; model: ["全部", "WARNING", "ERROR"]; Accessible.name: "日志过滤" }
            }
            RowLayout {
                Layout.alignment: parent.columns === 1 ? Qt.AlignLeft : Qt.AlignRight
                spacing: 8
                Button { text: "导出全部"; onClicked: exportDialog.open() }
                Button { text: "清空全部"; enabled: page.backend.logs.count > 0; onClicked: page.backend.logs.clear() }
            }
        }
        Label { text: "共 " + page.backend.logs.count + " 条 · 当前筛选 " + page.matchingCount + " 条 · 最多保留 1000 条"; color: Ui.secondaryText; wrapMode: Text.WordWrap; Layout.fillWidth: true }
    }
    Rectangle {
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.maximumWidth: Ui.settingsWidth
        color: Ui.groupSurface
        radius: 8
        border.color: Ui.borderColor
        Label {
            objectName: "logEmptyState"
            anchors.centerIn: parent
            width: parent.width - 32
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            visible: page.matchingCount === 0
            text: page.backend.logs.count === 0 ? "暂无日志" : "当前筛选无结果"
            color: Ui.secondaryText
        }
        ListView {
            id: list
            anchors.fill: parent
            anchors.margins: 16
            anchors.rightMargin: Ui.scrollGap
            clip: true
            model: page.backend.logs
            ScrollBar.vertical: ContentScrollBar { id: logScrollBar; Accessible.name: "日志滚动条" }
            delegate: Item {
                id: entry
                required property string time
                required property string level
                required property string message
                width: Math.max(0, list.width - logScrollBar.width - Ui.scrollGap)
                readonly property bool matched: filter.currentIndex === 0 || level === filter.currentText
                visible: matched
                height: matched ? line.implicitHeight + 16 : 0
                ColumnLayout {
                    id: line
                    width: parent.width
                    spacing: 4
                    Label { text: entry.time + " · " + entry.level; color: Ui.secondaryText; font.pixelSize: Ui.captionSize; Layout.fillWidth: true; wrapMode: Text.WrapAnywhere }
                    Label { text: entry.message; wrapMode: Text.WrapAnywhere; Layout.fillWidth: true; Accessible.name: entry.time + " " + entry.level + " " + text }
                }
            }
        }
    }
    FileDialog { id: exportDialog; title: "导出全部日志"; fileMode: FileDialog.SaveFile; nameFilters: ["文本文件 (*.txt)"]; onAccepted: page.backend.exportLogs(selectedFile.toString()) }
}
