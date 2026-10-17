import QtQuick

Item {
    id: surface
    implicitWidth: 240
    implicitHeight: 40
    readonly property bool accepted: { bridge.revision; return bridge.effective(surface.Window.window) }
    Component.onCompleted: bridge.watch(surface)
    Rectangle {
        anchors.fill: parent
        radius: 8
        color: surface.accepted ? "transparent" : (Application.styleHints.colorScheme === Qt.Dark ? "#2C2C2C" : "#FCFCFC")
        border.width: 1
        border.color: Application.styleHints.colorScheme === Qt.Dark ? "#40FFFFFF" : "#24000000"
    }
}
