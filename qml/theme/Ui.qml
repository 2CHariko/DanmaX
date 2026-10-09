pragma Singleton
import QtQuick
QtObject {
    readonly property SystemPalette colors: SystemPalette { colorGroup: SystemPalette.Active }
    property bool micaActive: false
    readonly property color windowSurface: micaActive && !highContrast ? "transparent" : colors.window
    readonly property color contentAreaSurface: highContrast ? "transparent"
        : (micaActive ? (dark ? Qt.rgba(1, 1, 1, 0.035) : Qt.rgba(1, 1, 1, 0.75))
                      : (dark ? Qt.rgba(1, 1, 1, 0.04) : colors.base))
    readonly property color contentSurface: contentAreaSurface
    readonly property bool dark: Application.styleHints.colorScheme === Qt.Dark
    readonly property int sectionGap: 24
    function icon(name) { return "qrc:/fluent/" + name + (dark ? "-dark" : "") + ".png" }
    readonly property color textColor: highContrast ? colors.windowText : (dark ? "#FFFFFF" : "#1A1A1A")
    readonly property bool highContrast: Application.styleHints.accessibility.contrastPreference === Qt.HighContrast
    readonly property color secondaryText: highContrast ? colors.text : (dark ? "#9D9D9D" : "#5D5D5D")
    readonly property int pagePadding: 32
    readonly property int bodySize: Math.round(14 * textScale)
    readonly property real textScale: Math.max(1, Qt.application.font.pointSize / 9)
    readonly property int titleSize: Math.round(28 * textScale)
    readonly property int subtitleSize: Math.round(20 * textScale)
    readonly property int comboPopupHeight: Math.round(360 * textScale)
    readonly property int scrollGap: 8
    readonly property int expandedNavigationWidth: 1008
    readonly property int minimalNavigationWidth: 640
    readonly property int settingsWidth: 1040
    readonly property int contentMaxWidth: 920
    readonly property color cardBackground: highContrast ? colors.window : (dark ? Qt.rgba(1, 1, 1, 0.05) : Qt.rgba(1, 1, 1, 0.70))
    readonly property color cardBackgroundHover: highContrast ? colors.highlight : (dark ? Qt.rgba(1, 1, 1, 0.08) : Qt.rgba(0.95, 0.95, 0.95, 0.90))
    readonly property color cardBorder: highContrast ? colors.windowText : (dark ? Qt.rgba(1, 1, 1, 0.08) : Qt.rgba(0, 0, 0, 0.06))
    readonly property color accentColor: highContrast ? colors.highlight : (dark ? "#60CDFF" : "#005FB8")
    readonly property int sectionHeaderSize: Math.round(15 * textScale)
    readonly property int captionSize: Math.round(12 * textScale)
    readonly property int cardGap: 4
    readonly property int cardRadius: 6
    readonly property int pageMarginWide: 32
    readonly property int pageMarginNarrow: 16
}
