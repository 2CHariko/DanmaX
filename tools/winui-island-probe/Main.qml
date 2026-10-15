import QtQuick
import QtQuick.Controls.FluentWinUI3
import QtQuick.Layouts

ApplicationWindow {
    id: root
    width: 920
    height: 760
    minimumWidth: 520
    minimumHeight: 400
    visible: true
    title: "DanmaX - WinUI 3 XAML Islands probe"
    property bool dark: false
    color: palette.window
    function scenario(name) {
        if (name === "modal") dialog.open()
        if (name === "clip") viewport.contentY = 350
        if (name === "popup") bridge.setPopupOpen(true)
    }
    Connections {
        target: bridge
        function onLeaveNative(reverse) {
            (reverse ? before : after).forceActiveFocus(Qt.TabFocusReason)
        }
    }
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 12
        Label { text: "Qt Quick + real WinUI 3 ComboBox"; font.pixelSize: 24 }
        Label { text: "Feasibility probe / Windows App SDK 1.8 / native child HWND"; wrapMode: Text.Wrap; Layout.fillWidth: true }
        RowLayout {
            Button { text: "Light / dark"; onClicked: { root.dark = !root.dark; bridge.setDark(root.dark) } }
            Button { text: "Select item 119"; onClicked: bridge.selectLast() }
            Button { text: "Recreate island"; onClicked: bridge.recreate() }
        }
        RowLayout {
            Button { text: "QML dialog (Item)"; onClicked: dialog.open() }
            Button { text: "QML dialog (Window)"; onClicked: windowDialog.open() }
            Button { text: "Resize"; onClicked: { root.width = root.width > 600 ? 560 : 920; root.height = root.height > 500 ? 460 : 760 } }
        }
        Flickable {
            id: viewport
            objectName: "probeViewport"
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: width
            contentHeight: body.height
            clip: true
            onContentYChanged: bridge.syncGeometry()
            ScrollBar.vertical: ScrollBar { }
            Column {
                id: body
                width: viewport.width - 28
                spacing: 12
                TextField {
                    id: before
                    objectName: "beforeIsland"
                    width: parent.width
                    placeholderText: "Qt focus before island: Tab enters native controls"
                    onActiveFocusChanged: if (activeFocus) bridge.focusQt()
                    Keys.onPressed: event => {
                        if (event.key === Qt.Key_Tab) { nativeFocus.forceActiveFocus(); bridge.focusNative(false); event.accepted = true }
                    }
                }
                FocusScope {
                    id: nativeFocus
                    objectName: "islandSlot"
                    width: parent.width
                    height: 240
                    activeFocusOnTab: true
                    onWidthChanged: bridge.syncGeometry()
                    onHeightChanged: bridge.syncGeometry()
                    KeyNavigation.backtab: before
                    KeyNavigation.tab: after
                    Rectangle { anchors.fill: parent; color: root.palette.base; border.color: root.palette.mid }
                }
                TextField {
                    id: after
                    objectName: "afterIsland"
                    width: parent.width
                    placeholderText: "Qt focus after island"
                    onActiveFocusChanged: if (activeFocus) bridge.focusQt()
                    KeyNavigation.backtab: nativeFocus
                    Keys.onPressed: event => {
                        if (event.key === Qt.Key_Backtab) { nativeFocus.forceActiveFocus(); bridge.focusNative(true); event.accepted = true }
                    }
                }
                Label { text: "Qt official Fluent ComboBox for comparison" }
                ComboBox {
                    objectName: "qtCombo"
                    width: 320
                    model: ["System / 系统", "Light / 浅色", "Dark / 深色"]
                }
                Label { text: "Native selection → Qt: " + bridge.selection; wrapMode: Text.Wrap; width: parent.width }
                Button { text: "Scroll island partly out of viewport"; onClicked: viewport.contentY = 155 }
                Rectangle {
                    width: parent.width
                    height: 420
                    color: root.palette.alternateBase
                    Label { anchors.centerIn: parent; text: "Extra scroll content / clipping test" }
                }
            }
        }
        Label { text: bridge.status; wrapMode: Text.Wrap; Layout.fillWidth: true }
    }
    Dialog {
        id: dialog
        objectName: "probeDialog"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(500, root.width - 40)
        height: 420
        modal: true
        popupType: Popup.Item
        title: "QML modal / native HWND overlap test"
        standardButtons: Dialog.Close
        Label { anchors.fill: parent; text: "Inspect whether native controls overlap this QML dialog.\nClose with Escape."; wrapMode: Text.Wrap }
        onOpened: bridge.report("dialog-item-opened")
        onClosed: bridge.report("dialog-closed")
    }
    Dialog {
        id: windowDialog
        parent: Overlay.overlay
        x: (root.width - width) / 2
        y: (root.height - height) / 2
        width: Math.min(500, root.width - 40)
        height: 420
        modal: true
        popupType: Popup.Window
        title: "QML window modal / native HWND overlap test"
        standardButtons: Dialog.Close
        Label { text: "Separate popup window above native HWND. Close with Escape."; width: parent.width; wrapMode: Text.Wrap }
        onOpened: bridge.report("dialog-window-opened")
        onClosed: bridge.report("dialog-window-closed")
    }
}
