pragma Singleton
import QtQuick
QtObject {
    readonly property SystemPalette colors: SystemPalette { colorGroup: SystemPalette.Active }
    readonly property color windowSurface: colors.window
    readonly property color contentSurface: colors.base
    readonly property color groupSurface: colors.button
    readonly property color textColor: colors.windowText
    readonly property bool highContrast: Application.styleHints.accessibility.contrastPreference === Qt.HighContrast
    function blend(foreground, background, amount) {
        return Qt.rgba(foreground.r * amount + background.r * (1 - amount), foreground.g * amount + background.g * (1 - amount), foreground.b * amount + background.b * (1 - amount), 1)
    }
    readonly property color secondaryText: highContrast ? colors.text : blend(colors.text, colors.button, 0.78)
    readonly property color borderColor: highContrast ? colors.windowText : blend(colors.windowText, colors.button, 0.12)
    readonly property int pagePadding: 24
    readonly property int groupPadding: 16
    readonly property int bodySize: Math.round(14 * textScale)
    readonly property real textScale: Math.max(1, Qt.application.font.pointSize / 9)
    readonly property int titleSize: Math.round(28 * textScale)
    readonly property int subtitleSize: Math.round(20 * textScale)
    readonly property int comboPopupHeight: Math.round(360 * textScale)
    readonly property int scrollGap: 8
    readonly property int popupRadius: 8
    readonly property color popupSurface: highContrast ? colors.window : colors.base
    readonly property color popupBorder: highContrast ? colors.windowText : borderColor
    readonly property color scrollThumb: highContrast ? colors.windowText : secondaryText
    readonly property int captionSize: Math.round(12 * textScale)
    readonly property int expandedNavigationWidth: 1008
    readonly property int minimalNavigationWidth: 640
    readonly property int settingsWidth: 1040
}
