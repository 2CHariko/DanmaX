import QtQuick
import QtQuick.Controls.FluentWinUI3
import QtQuick.Layouts
ColumnLayout {
    id: nav
    property bool compact: false
    property int selected: 0
    property bool fluentIcons: false
    signal navigate(int page)
    spacing: 4
    Repeater {
        model: [{title:"播放", glyph:"\ue768", page:0}, {title:"日志", glyph:"\ue9d9", page:1}]
        ItemDelegate {
            required property var modelData
            Layout.fillWidth: true
            text: nav.compact && nav.fluentIcons ? modelData.glyph : modelData.title
            font.family: nav.compact && nav.fluentIcons ? "Segoe Fluent Icons" : Qt.application.font.family
            highlighted: nav.selected === modelData.page
            Accessible.name: modelData.title
            Accessible.selected: highlighted
            ToolTip.visible: hovered && nav.compact
            ToolTip.text: modelData.title
            onClicked: nav.navigate(modelData.page)
        }
    }
    Item { Layout.fillHeight: true }
    ItemDelegate {
        Layout.fillWidth: true
        text: nav.compact && nav.fluentIcons ? "\ue713" : "设置"
        font.family: nav.compact && nav.fluentIcons ? "Segoe Fluent Icons" : Qt.application.font.family
        highlighted: nav.selected === 2
        Accessible.name: "设置"
        Accessible.selected: highlighted
        ToolTip.visible: hovered && nav.compact
        ToolTip.text: "设置"
        onClicked: nav.navigate(2)
    }
}
