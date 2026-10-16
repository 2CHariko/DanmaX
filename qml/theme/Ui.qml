pragma Singleton
import QtQuick
QtObject {
    id: ui
    property int activeCoverPlaceholders: 0
    property real coverPulse: 1
    // All visible covers share one animation, regardless of list length.
    property SequentialAnimation coverAnimation: SequentialAnimation {
        running: ui.activeCoverPlaceholders > 0 && ui.motionEnabled
        loops: Animation.Infinite
        NumberAnimation { target: ui; property: "coverPulse"; from: 1; to: 0.55; duration: 800; easing.type: Easing.InOutSine }
        NumberAnimation { target: ui; property: "coverPulse"; from: 0.55; to: 1; duration: 800; easing.type: Easing.InOutSine }
    }
    readonly property SystemPalette colors: SystemPalette { colorGroup: SystemPalette.Active }
    property bool micaActive: false
    property bool systemAnimationsEnabled: true
    readonly property bool motionEnabled: systemAnimationsEnabled && !highContrast
    readonly property int contentTransitionDuration: 180
    readonly property int dragFeedbackDuration: 120
    readonly property int dragBlurMax: 40
    readonly property real dragBackgroundSaturation: -0.65
    readonly property int dragSnapshotMaxDimension: 1600
    readonly property int dragPromptMaxWidth: 640
    readonly property color dragScrim: highContrast ? colors.window
        : Qt.rgba(colors.window.r, colors.window.g, colors.window.b, dark ? 0.75 : 0.80)
    readonly property color dragFallbackSurface: colors.window
    readonly property int skeletonDelay: 150
    readonly property color skeletonFill: highContrast ? colors.windowText
        : (dark ? Qt.rgba(1, 1, 1, 0.10) : Qt.rgba(0, 0, 0, 0.08))
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
    readonly property int titleBarVerticalPadding: 8
    readonly property int pageHeaderGap: 8
    readonly property int bodySize: Math.round(14 * textScale)
    readonly property real textScale: Math.max(1, Qt.application.font.pointSize / 9)
    readonly property int titleSize: Math.round(28 * textScale)
    readonly property int subtitleSize: Math.round(20 * textScale)
    readonly property int comboPopupHeight: Math.round(360 * textScale)
    readonly property int scrollGap: 8
    readonly property int wheelScrollStep: Math.round(108 * textScale)
    readonly property int scrollEdgeInset: 4
    function scrollInset(thickness, minimum = 0) {
        return Math.ceil(Math.max(minimum, thickness + scrollGap + scrollEdgeInset) / 4) * 4
    }
    function pageMargin(windowWidth) {
        return windowWidth < expandedNavigationWidth ? pageMarginNarrow : pageMarginWide
    }
    readonly property int expandedNavigationWidth: 1008
    readonly property int minimalNavigationWidth: 640
    readonly property int settingsWidth: 1040
    readonly property int contentMaxWidth: 920
    readonly property color cardBackground: highContrast ? colors.window : (dark ? Qt.rgba(1, 1, 1, 0.05) : Qt.rgba(1, 1, 1, 0.70))
    readonly property color cardBackgroundHover: highContrast ? colors.highlight : (dark ? Qt.rgba(1, 1, 1, 0.08) : Qt.rgba(0.95, 0.95, 0.95, 0.90))
    readonly property color cardBorder: highContrast ? colors.windowText : (dark ? Qt.rgba(1, 1, 1, 0.08) : Qt.rgba(0, 0, 0, 0.06))
    readonly property color accentColor: highContrast ? colors.highlight : (dark ? "#60CDFF" : "#005FB8")
    readonly property color successText: highContrast ? colors.windowText : (dark ? "#6CCB5F" : "#107C10")
    readonly property color cautionText: highContrast ? colors.windowText : (dark ? "#EAA300" : "#BC4B09")
    readonly property int sectionHeaderSize: Math.round(15 * textScale)
    readonly property int captionSize: Math.round(12 * textScale)
    readonly property int cardGap: 4
    readonly property int cardRadius: 6
    readonly property int cardMinHeight: Math.round(70 * textScale)
    readonly property int cardPaddingX: 16
    readonly property int cardPaddingY: 16
    readonly property int pageMarginWide: 32
    readonly property int pageMarginNarrow: 16
}
