import QtQuick
import QtQuick.Controls.FluentWinUI3
import QtQuick.Layouts
import "../theme"
import "../i18n"

Control {
    id: heroRoot
    property var anime: null
    signal watchNowClicked(string title)
    signal detailsClicked(var anime)

    readonly property string animeTitle: anime ? (anime.title || "") : ""
    readonly property string animeCover: anime ? (anime.cover || "") : ""
    readonly property string animeScore: anime ? (anime.score || "") : ""
    readonly property string animeAirDate: anime ? (anime.airDate || "") : ""
    readonly property color heroSurface: Ui.dark ? "#1F1F1F" : "#F3F3F3"

    Layout.fillWidth: true
    Layout.preferredHeight: Math.round(280 * Ui.textScale)
    Layout.minimumHeight: Math.round(240 * Ui.textScale)

    background: Rectangle {
        radius: Ui.cardRadius
        color: heroRoot.heroSurface
        border.width: 1
        border.color: Ui.cardBorder
        clip: true

        // 右侧半幅大图背景
        Image {
            id: heroImage
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: Math.min(parent.width * 0.55, parent.height * 1.5)
            source: heroRoot.animeCover
            asynchronous: true
            cache: true
            fillMode: Image.PreserveAspectCrop
            opacity: status === Image.Ready ? (Ui.dark ? 0.75 : 0.60) : 0.0
        }

        // 横向线性渐变蒙层：左侧纯色承托文字，右侧向海报透明过渡
        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: heroRoot.heroSurface }
                GradientStop { position: 0.40; color: heroRoot.heroSurface }
                GradientStop { position: 0.65; color: Ui.dark ? Qt.rgba(0.12, 0.12, 0.12, 0.85) : Qt.rgba(0.95, 0.95, 0.95, 0.85) }
                GradientStop { position: 1.0; color: "transparent" }
            }
        }
    }

    contentItem: Item {
        ColumnLayout {
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.margins: Math.round(24 * Ui.textScale)
            width: Math.max(0, parent.width < 600 * Ui.textScale
                ? parent.width - 2 * anchors.margins
                : Math.min(540 * Ui.textScale, parent.width * 0.65))
            spacing: 10

            // 顶部标签行：主推胶囊、评分、首播
            RowLayout {
                spacing: 8

                Rectangle {
                    height: featuredLabel.implicitHeight + 4
                    width: featuredLabel.implicitWidth + 12
                    radius: 4
                    color: Ui.accentColor

                    Label {
                        id: featuredLabel
                        anchors.centerIn: parent
                        text: I18n.home.airingToday
                        font.pixelSize: Ui.captionSize
                        font.weight: Font.DemiBold
                        color: "#FFFFFF"
                    }
                }

                Rectangle {
                    visible: heroRoot.animeScore.length > 0
                    height: scoreLabel.implicitHeight + 4
                    width: scoreLabel.implicitWidth + 10
                    radius: 4
                    color: Ui.dark ? Qt.rgba(1, 1, 1, 0.10) : Qt.rgba(0, 0, 0, 0.08)
                    border.width: 1
                    border.color: Ui.cardBorder

                    Label {
                        id: scoreLabel
                        anchors.centerIn: parent
                        text: I18n.format(I18n.home.heroRatingFormat, heroRoot.animeScore)
                        font.pixelSize: Ui.captionSize
                        font.weight: Font.Bold
                        color: Ui.dark ? "#FFD043" : "#BC4B09"
                    }
                }

                Label {
                    text: heroRoot.animeAirDate
                    font.pixelSize: Ui.captionSize
                    color: Ui.secondaryText
                    visible: text.length > 0
                }
            }

            // 番剧主标题
            Label {
                Layout.fillWidth: true
                text: heroRoot.animeTitle
                font.pixelSize: Ui.titleSize
                font.weight: Font.Bold
                color: Ui.textColor
                elide: Text.ElideRight
                maximumLineCount: 2
                wrapMode: Text.WordWrap
            }
            Label {
                Layout.fillWidth: true
                text: heroRoot.anime ? (heroRoot.anime.originalTitle || "") : ""
                font.pixelSize: Ui.captionSize
                color: Ui.secondaryText
                elide: Text.ElideRight
                visible: text.length > 0 && text !== heroRoot.animeTitle
            }

            // 剧情大纲概要
            Label {
                Layout.fillWidth: true
                Layout.fillHeight: true
                text: heroRoot.anime ? (heroRoot.anime.summary || I18n.home.noSummary) : ""
                font.pixelSize: Ui.bodySize
                color: Ui.secondaryText
                maximumLineCount: 3
                elide: Text.ElideRight
                wrapMode: Text.WordWrap
                lineHeight: 1.25
            }

            // 动作按钮栏
            RowLayout {
                spacing: 12
                Layout.topMargin: 4

                Button {
                    enabled: heroRoot.animeTitle.length > 0
                    text: I18n.home.heroWatchNow
                    highlighted: true
                    icon.source: Ui.icon("play")
                    Accessible.name: I18n.home.heroWatchNow + "：" + heroRoot.animeTitle
                    onClicked: {
                        if (heroRoot.animeTitle.length > 0) {
                            heroRoot.watchNowClicked(heroRoot.animeTitle)
                        }
                    }
                }

                Button {
                    enabled: heroRoot.anime && Number(heroRoot.anime.id) > 0
                    text: I18n.home.heroDetails
                    icon.source: Ui.icon("info")
                    Accessible.name: I18n.home.heroDetails + "：" + heroRoot.animeTitle
                    onClicked: {
                        if (heroRoot.anime) {
                            heroRoot.detailsClicked(heroRoot.anime)
                        }
                    }
                }
            }
        }
    }
}
