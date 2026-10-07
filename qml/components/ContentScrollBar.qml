import QtQuick
import QtQuick.Controls.FluentWinUI3
import "../theme"

ScrollBar {
    policy: size > 0 && size < 1 ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
    active: true
    interactive: true
    palette.mid: Ui.scrollThumb
    palette.dark: Ui.colors.accent
}
