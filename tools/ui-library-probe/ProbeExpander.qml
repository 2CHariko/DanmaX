import QtQuick
import FluentUI

// Preserve the upstream visuals, add a public focus/accessible activation target.
FluExpander {
    id: control
    activeFocusOnTab: true
    Accessible.role: Accessible.Button
    Accessible.name: headerText
    Accessible.checkable: true
    Accessible.checked: expand
    Accessible.description: expand ? "已展开，按空格收起" : "已收起，按空格展开"
    Accessible.onPressAction: expand = !expand
    Keys.onSpacePressed: expand = !expand
    Keys.onReturnPressed: expand = !expand
    function ownsFocus() {
        let item = control.Window.window ? control.Window.window.activeFocusItem : null
        while (item) {
            if (item === control) return true
            item = item.parent
        }
        return false
    }
    onExpandChanged: {
        if (!expand && ownsFocus()) control.forceActiveFocus(Qt.TabFocusReason)
    }
    FluFocusRectangle { parent: control; anchors.fill: parent; visible: control.activeFocus }
}
