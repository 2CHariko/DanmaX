import QtQuick
import QtQuick.Controls.FluentWinUI3
import QtQuick.Layouts
import "../i18n"

// Optional content controlled by a standard button.
ColumnLayout {
    id: control
    property string title
    property bool expanded: false
    default property alias entries: body.data
    readonly property alias toggleButton: toggle
    Layout.fillWidth: true
    spacing: 8
    onExpandedChanged: {
        if (!expanded) {
            let item = control.Window.window ? control.Window.window.activeFocusItem : null
            while (item) {
                if (item === body) { toggle.forceActiveFocus(Qt.TabFocusReason); break }
                item = item.parent
            }
        }
    }
    Button {
        id: toggle
        text: (control.expanded ? I18n.common.disclosureCollapsePrefix : I18n.common.disclosureExpandPrefix) + control.title
        checkable: true
        checked: control.expanded
        Accessible.name: control.title
        Accessible.description: control.expanded ? I18n.common.disclosureExpanded : I18n.common.disclosureCollapsed
        onToggled: control.expanded = checked
    }
    ColumnLayout {
        id: body
        Layout.fillWidth: true
        visible: control.expanded
        enabled: control.expanded
        spacing: 8
    }
}
