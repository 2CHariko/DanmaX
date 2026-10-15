import QtQuick
import QtQuick.Controls.FluentWinUI3
import QtQuick.Layouts
import "../theme"
import "../i18n"

Item {
    id: cardRoot
    property var animeData: null
    required property QtObject coverCache
    signal clicked()

    readonly property var effectiveData: animeData
    readonly property string cardTitle: effectiveData ? (effectiveData.title || "") : ""
    readonly property string cardScore: effectiveData ? (effectiveData.score || "") : ""
    readonly property string cardCover: effectiveData ? (effectiveData.cover || "") : ""
    readonly property string cardAirDate: effectiveData ? (effectiveData.airDate || "") : ""
    readonly property string cardBadge: effectiveData && effectiveData.badge ? effectiveData.badge : ""

    width: Math.round(140 * Ui.textScale)
    height: Math.round(210 * Ui.textScale)

    activeFocusOnTab: true
    Accessible.role: Accessible.Button
    Accessible.name: cardTitle + (cardScore.length > 0 ? " " + I18n.format(I18n.home.heroRatingFormat, cardScore) : "")
    Accessible.onPressAction: cardRoot.clicked()

    Keys.onReturnPressed: cardRoot.clicked()
    Keys.onSpacePressed: cardRoot.clicked()

    Rectangle {
        id: cardBg
        anchors.fill: parent
        radius: Ui.cardRadius
        color: Ui.cardBackground
        clip: true

        border.width: cardRoot.activeFocus ? 2 : 1
        border.color: cardRoot.activeFocus ? Ui.accentColor :
                      (hoverArea.containsMouse ? (Ui.dark ? Qt.rgba(1, 1, 1, 0.28) : Qt.rgba(0, 0, 0, 0.22)) : Ui.cardBorder)

        // 海报大图（异步加载，限制解码宽度以节约纹理显存）
        CachedCoverImage {
            id: posterImg
            anchors.fill: parent
            cacheService: cardRoot.coverCache
            remoteSource: cardCover
            asynchronous: true
            cache: true
            sourceSize.width: 280
            fillMode: Image.PreserveAspectCrop
        }

        // 底部渐变半透明暗影遮罩，确保任意封面下的白色文字清晰可辨
        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: Math.round(72 * Ui.textScale)
            gradient: Gradient {
                GradientStop { position: 0.0; color: "transparent" }
                GradientStop { position: 0.35; color: Qt.rgba(0, 0, 0, 0.45) }
                GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.88) }
            }
        }

        // 底部标题与播出信息
        ColumnLayout {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 8
            spacing: 2

            Label {
                Layout.fillWidth: true
                text: cardTitle
                font.pixelSize: Ui.captionSize
                font.weight: Font.DemiBold
                color: "#FFFFFF"
                maximumLineCount: 2
                wrapMode: Text.WordWrap
                elide: Text.ElideRight
            }

            Label {
                Layout.fillWidth: true
                text: cardAirDate
                font.pixelSize: Math.max(9, Math.round(10 * Ui.textScale))
                color: Qt.rgba(1, 1, 1, 0.75)
                elide: Text.ElideRight
                visible: text.length > 0
            }
        }

        // 右上角评分胶囊
        Rectangle {
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.margins: 6
            width: ratingLabel.implicitWidth + 8
            height: ratingLabel.implicitHeight + 4
            radius: 4
            color: Qt.rgba(0, 0, 0, 0.65)
            border.width: 1
            border.color: Qt.rgba(1, 1, 1, 0.15)
            visible: cardScore.length > 0

            Label {
                id: ratingLabel
                anchors.centerIn: parent
                text: I18n.format(I18n.home.heroRatingFormat, cardScore)
                font.pixelSize: Math.max(9, Math.round(10 * Ui.textScale))
                font.weight: Font.Bold
                color: "#FFD043"
            }
        }

        // 左上角角标徽章（本地缓存、连载中等）
        Rectangle {
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.margins: 6
            width: badgeLabel.implicitWidth + 8
            height: badgeLabel.implicitHeight + 4
            radius: 4
            color: Ui.accentColor
            visible: cardBadge.length > 0

            Label {
                id: badgeLabel
                anchors.centerIn: parent
                text: cardBadge
                font.pixelSize: Math.max(9, Math.round(10 * Ui.textScale))
                font.weight: Font.DemiBold
                color: "#FFFFFF"
            }
        }
    }

    MouseArea {
        id: hoverArea
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: { cardRoot.forceActiveFocus(); cardRoot.clicked() }
    }
}
