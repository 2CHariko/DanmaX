pragma Singleton
import QtQuick
QtObject {
    readonly property real textScale: Math.max(1, Qt.application.font.pointSize / 9)
    readonly property int titleSize: Math.round(28 * textScale)
    readonly property int captionSize: Math.round(12 * textScale)
    readonly property int expandedNavigationWidth: 1008
    readonly property int minimalNavigationWidth: 640
    readonly property int settingsWidth: 1040
}
