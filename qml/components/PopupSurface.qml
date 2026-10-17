import QtQuick
import QtQuick.Effects
import "../theme"

// Geometry stays sharp at every DPI; this is an opaque fallback, not Acrylic.
Rectangle {
    id: surface
    objectName: "popupSurface"
    implicitWidth: 200
    implicitHeight: 30
    radius: 8
    color: Ui.popupSurface
    border.width: Ui.highContrast ? 2 : 1
    border.color: Ui.popupBorder
    property bool shadowEnabled: !Ui.highContrast
        && GraphicsInfo.api !== GraphicsInfo.Software
        && GraphicsInfo.api !== GraphicsInfo.Unknown
        && GraphicsInfo.api !== GraphicsInfo.Null
    Loader {
        objectName: "popupShadowLoader"
        z: -1
        anchors.fill: parent
        active: surface.visible && surface.shadowEnabled
        sourceComponent: RectangularShadow {
            radius: surface.radius
            blur: Ui.popupShadowBlur
            offset.y: Ui.popupShadowOffset
            color: Ui.popupShadowColor
        }
    }
}
