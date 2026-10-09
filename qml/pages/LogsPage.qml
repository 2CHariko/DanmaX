import QtQuick
import "../theme"
import "../components"
import QtQuick.Controls.FluentWinUI3
import QtQuick.Layouts
import QtQuick.Dialogs
import "../i18n"

ColumnLayout {
    id: page
    required property QtObject backend
    readonly property bool compact: width < Ui.expandedNavigationWidth
    readonly property int matchingCount: {
        const revision = backend.logs.count
        return backend.logs.countForLevel(filter.currentIndex === 0 ? "" : filter.currentText)
    }
    function focusHeading() { heading.forceActiveFocus() }
    spacing: 20

    Label {
        id: heading
        text: I18n.logs.title
        font.pixelSize: Ui.titleSize
        font.weight: Font.DemiBold
        color: Ui.textColor
        Accessible.role: Accessible.Heading
    }

    // 顶部过滤器与操作工具栏
    ColumnLayout {
        Layout.fillWidth: true
        Layout.rightMargin: page.compact ? 16 : Ui.pagePadding
        spacing: 8

        GridLayout {
            Layout.fillWidth: true
            columns: page.width < 600 ? 1 : 2
            rowSpacing: 8

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Label { text: I18n.logs.levelLabel; color: Ui.textColor }
                AppComboBox {
                    id: filter
                    objectName: "logFilter"
                    model: [I18n.logs.filterAll, "WARNING", "ERROR"]
                    implicitWidth: 140
                    Accessible.name: I18n.logs.filterAccessible
                }
            }

            RowLayout {
                Layout.alignment: parent.columns === 1 ? Qt.AlignLeft : Qt.AlignRight
                spacing: 8
                Button { text: I18n.logs.exportBtn; onClicked: exportDialog.open() }
                Button { text: I18n.logs.clearBtn; enabled: page.backend.logs.count > 0; onClicked: page.backend.logs.clear() }
            }
        }

        Label {
            text: I18n.format(I18n.logs.statsFormat, page.backend.logs.count, page.matchingCount)
            color: Ui.secondaryText
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
    }

    // 主日志列表容器（采用全站统一 Fluent 卡片规范，充满可用宽度并保持 32px 对称留白）
    Control {
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.rightMargin: page.compact ? 16 : Ui.pagePadding
        Layout.bottomMargin: 16

        background: Rectangle {
            radius: Ui.cardRadius
            color: Ui.cardBackground
            border.width: 1
            border.color: Ui.cardBorder
        }

        contentItem: Item {
            anchors.fill: parent

            Label {
                id: emptyLabel
                objectName: "logEmptyState"
                anchors.centerIn: parent
                width: parent.width - 32
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                visible: page.matchingCount === 0
                text: page.backend.logs.count === 0 ? I18n.logs.emptyAll : I18n.logs.emptyFiltered
                color: Ui.secondaryText
            }

            ListView {
                id: list
                anchors.fill: parent
                anchors.margins: 16
                anchors.rightMargin: Ui.scrollGap
                clip: true
                model: page.backend.logs
                ScrollBar.vertical: ScrollBar {
                    id: logScrollBar
                    Accessible.name: I18n.logs.scrollBarAccessible
                }
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

                        Label {
                            text: entry.time + " · " + entry.level
                            color: entry.level === "ERROR" ? "#FF6B6B" : (entry.level === "WARNING" ? "#FFB84D" : Ui.secondaryText)
                            font.weight: entry.level === "ERROR" ? Font.DemiBold : Font.Normal
                            Layout.fillWidth: true
                            wrapMode: Text.WrapAnywhere
                        }

                        Label {
                            text: entry.message
                            color: Ui.textColor
                            wrapMode: Text.WrapAnywhere
                            Layout.fillWidth: true
                            Accessible.name: entry.time + " " + entry.level + " " + text
                        }
                    }
                }
            }
        }
    }

    FileDialog {
        id: exportDialog
        title: I18n.logs.exportDialogTitle
        fileMode: FileDialog.SaveFile
        nameFilters: [I18n.logs.exportFilter]
        onAccepted: page.backend.exportLogs(selectedFile.toString())
    }
}
