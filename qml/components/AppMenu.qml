import QtQuick
import QtQuick.Controls.FluentWinUI3
import "../theme"

Menu {
    id: menu
    property bool watchingTheme: false
    popupType: Ui.embeddedVideoActive ? Popup.Window : Popup.Item
    property real revealProgress: 1
    height: implicitHeight * revealProgress
    leftInset: 0
    rightInset: 0
    topInset: 0
    bottomInset: 0
    margins: Ui.popupEdgeMargin
    background: PopupSurface { watchingTheme: menu.watchingTheme }
    enter: Transition {
        NumberAnimation {
            target: menu
            property: "revealProgress"
            from: Ui.motionEnabled ? 0.33 : 1
            to: 1
            duration: Ui.popupOpenDuration
            easing.type: Easing.OutCubic
        }
    }
}
