import QtQuick
import QtQuick.Controls.FluentWinUI3
import QtQuick.Layouts

ColumnLayout {
    id: root
    property string title
    property alias expanded: toggle.checked
    default property alias entries: body.data
    Layout.fillWidth: true
    spacing: 12
    Button {
        id: toggle
        objectName: "disclosureToggle"
        text: (checked ? "收起：" : "展开：") + root.title
        checkable: true
        Accessible.name: root.title
        Accessible.description: checked ? "已展开" : "已收起"
        onCheckedChanged: {
            if (!checked && body.containsFocus()) forceActiveFocus()
        }
    }
    ColumnLayout {
        id: body
        visible: toggle.checked
        Layout.fillWidth: true
        spacing: 8
        function containsFocus() {
            let item = root.Window.window ? root.Window.window.activeFocusItem : null
            while (item) { if (item === body) return true; item = item.parent }
            return false
        }
    }
}
