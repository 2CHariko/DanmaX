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

    component NavigationItem: ItemDelegate {
        required property var modelData
        objectName: "navigation" + modelData.page
        Layout.fillWidth: true
        Layout.preferredHeight: Math.max(Ui.navigationItemHeight, implicitHeight)
        text: modelData.title
        icon.source: Ui.icon(modelData.icon)
        icon.width: Ui.navigationIconSize
        icon.height: Ui.navigationIconSize
        spacing: Ui.navigationIconGap
        leftPadding: nav.compact ? Math.max(0, (width - icon.width) / 2) : 16
        rightPadding: nav.compact ? leftPadding : 12
        display: nav.compact ? AbstractButton.IconOnly : AbstractButton.TextBesideIcon
        highlighted: nav.selected === modelData.page
        activeFocusOnTab: true
        Accessible.name: modelData.title
        Accessible.selected: highlighted
        ToolTip.visible: hovered && nav.compact
        ToolTip.text: text
        onClicked: nav.navigate(modelData.page)
    }

    Item {
        visible: nav.showBrandHeader
        Layout.fillWidth: true
        Layout.preferredHeight: Math.max(36, brandLabel.implicitHeight + 8)
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
                    Accessible.ignored: true
                }
            }
            Label {
                id: brandLabel
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
            { title: I18n.nav.home, icon: "home", page: 0 },
            { title: I18n.nav.play, icon: "play", page: 1 },
            { title: "媒体库", icon: "window", page: 5 }
        ]
        NavigationItem {}
    }
    Item { Layout.fillHeight: true }
    Repeater {
        model: [
            { title: I18n.nav.logs, icon: "logs", page: 2 },
            { title: I18n.nav.about, icon: "info", page: 4 },
            { title: I18n.nav.settings, icon: "settings", page: 3 }
        ]
        NavigationItem {}
    }
}
