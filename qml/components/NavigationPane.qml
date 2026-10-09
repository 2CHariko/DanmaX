import QtQuick
import QtQuick.Controls.FluentWinUI3
import QtQuick.Layouts
import "../theme"
import "../i18n"

ColumnLayout {
    id: nav
    property bool compact: false
    property int selected: 0
    property bool showBrandHeader: true
    signal startSystemMove()
    signal toggleMaximized()
    signal navigate(int page)
    spacing: 4

    // 顶部品牌区（高度 36px，与右侧标题栏对齐，支持拖拽移动窗口与双击最大化）
    Item {
        visible: nav.showBrandHeader
        Layout.fillWidth: true
        Layout.preferredHeight: 36
        Layout.topMargin: 0
        Layout.bottomMargin: 4

        MouseArea {
            anchors.fill: parent
            onPressed: nav.startSystemMove()
            onDoubleClicked: nav.toggleMaximized()
        }

        RowLayout {
            anchors.fill: parent
            spacing: 10

            Item {
                Layout.preferredWidth: nav.compact ? parent.width : 28
                Layout.fillHeight: true

                Image {
                    anchors.centerIn: parent
                    width: 20
                    height: 20
                    source: "qrc:/branding/DanmaX.png"
                    fillMode: Image.PreserveAspectFit
                    mipmap: true
                }
            }

            Label {
                visible: !nav.compact
                text: I18n.nav.appTitle
                font.pixelSize: Ui.subtitleSize
                font.weight: Font.DemiBold
                color: Ui.textColor
                Layout.fillWidth: true
                elide: Text.ElideRight
            }
        }
    }
    Repeater {
        model: [
            {
                title: I18n.nav.play,
                icon: "play",
                page: 0
            },
            {
                title: I18n.nav.logs,
                icon: "logs",
                page: 1
            }
        ]
        ItemDelegate {
            id: navigationItem
            required property var modelData
            objectName: "navigation" + modelData.page
            Layout.fillWidth: true
            Layout.preferredHeight: 40
            text: modelData.title
            icon.source: Ui.icon(modelData.icon)
            display: nav.compact ? AbstractButton.IconOnly : AbstractButton.TextBesideIcon
            highlighted: nav.selected === modelData.page
            Accessible.name: modelData.title
            Accessible.selected: highlighted
            ToolTip { visible: navigationItem.hovered && nav.compact; text: navigationItem.Accessible.name }
            onClicked: nav.navigate(modelData.page)

        }
    }

    Item {
        Layout.fillHeight: true
    }

    // 2. 底部系统导航项 (关于与设置)
    ItemDelegate {
        id: aboutItem
        objectName: "navigation3"
        Layout.fillWidth: true
        Layout.preferredHeight: 40
        text: I18n.nav.about
        icon.source: Ui.icon("info")
        display: nav.compact ? AbstractButton.IconOnly : AbstractButton.TextBesideIcon
        highlighted: nav.selected === 3
        Accessible.name: I18n.nav.about
        Accessible.selected: highlighted
        ToolTip { visible: aboutItem.hovered && nav.compact; text: aboutItem.Accessible.name }
        onClicked: nav.navigate(3)

    }

    ItemDelegate {
        id: settingsItem
        objectName: "navigation2"
        Layout.fillWidth: true
        Layout.preferredHeight: 40
        text: I18n.nav.settings
        icon.source: Ui.icon("settings")
        display: nav.compact ? AbstractButton.IconOnly : AbstractButton.TextBesideIcon
        highlighted: nav.selected === 2
        Accessible.name: I18n.nav.settings
        Accessible.selected: highlighted
        ToolTip { visible: settingsItem.hovered && nav.compact; text: settingsItem.Accessible.name }
        onClicked: nav.navigate(2)

    }
}
