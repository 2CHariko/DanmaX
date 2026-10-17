import QtQuick
import QtQuick.Controls.FluentWinUI3
import QtQuick.Layouts
import "../theme"
import "../components"

PageFrame {
    id: page
    required property QtObject backend
    readonly property QtObject player: backend.nowPlaying
    signal openVideo()
    title: "播放"
    description: "当前内容、弹幕与播放控制。"
    ColumnLayout {
        Layout.fillWidth: true
        Layout.topMargin: 16
        Layout.bottomMargin: 24
        spacing: 12
        Item {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: 144 * Ui.textScale
            Layout.preferredHeight: 192 * Ui.textScale
            CachedCoverImage {
                anchors.fill: parent
                visible: page.player.coverUrl.length > 0
                cacheService: page.backend.metadata.coverCache
                remoteSource: page.player.coverUrl
                fillMode: Image.PreserveAspectFit
            }
            FluentIcon {
                anchors.centerIn: parent
                width: 72; height: 72
                visible: page.player.coverUrl.length === 0 && !Ui.highContrast
                source: Ui.icon(page.player.video ? "window" : "play")
                fillMode: Image.PreserveAspectFit
            }
        }
        Label {
            objectName: "nowPlayingTitle"
            Layout.fillWidth: true
            text: page.player.title
            textFormat: Text.PlainText
            font.pixelSize: Ui.subtitleSize
            font.weight: Font.DemiBold
            wrapMode: Text.WrapAnywhere
            horizontalAlignment: Text.AlignHCenter
            Accessible.role: Accessible.Heading
        }
        Label { Layout.fillWidth: true; text: page.player.subtitle; textFormat: Text.PlainText; color: Ui.secondaryText; wrapMode: Text.WordWrap; horizontalAlignment: Text.AlignHCenter }
        Label { Layout.fillWidth: true; text: page.player.source.length > 0 ? "弹幕来源 · " + page.player.source : ""; visible: text.length > 0; textFormat: Text.PlainText; color: Ui.secondaryText; wrapMode: Text.WrapAnywhere; horizontalAlignment: Text.AlignHCenter }
        Label {
            Layout.fillWidth: true
            text: "弹幕 " + page.player.count + " 条" + (page.player.video ? " · " + page.backend.videoOpen.playerName : "")
            color: Ui.secondaryText
            horizontalAlignment: Text.AlignHCenter
        }
        Label { Layout.fillWidth: true; text: page.player.status; textFormat: Text.PlainText; wrapMode: Text.WordWrap; horizontalAlignment: Text.AlignHCenter; color: Ui.secondaryText }
        Label { Layout.fillWidth: true; text: page.player.error; textFormat: Text.PlainText; visible: text.length > 0; color: Ui.cautionText; wrapMode: Text.WordWrap; Accessible.role: Accessible.AlertMessage }
        BusyIndicator { Layout.alignment: Qt.AlignHCenter; visible: page.player.busy; running: visible }
        RowLayout {
            visible: page.player.openingStandalone
            Layout.fillWidth: true
            Label { text: "正在准备独立弹幕…"; Layout.fillWidth: true; wrapMode: Text.WordWrap }
            Button { objectName: "cancelStandaloneOpen"; text: "取消"; onClicked: page.player.cancelStandaloneOpen() }
        }
        Flow {
            visible: !page.player.active
            Layout.fillWidth: true
            spacing: 8
            Button { text: "打开视频"; objectName: "openVideoButton"; onClicked: page.openVideo() }
        }
        RowLayout {
            visible: page.player.video && page.backend.media.restoreFailed
            Layout.fillWidth: true
            Button { text: "重试恢复"; onClicked: page.backend.media.retryRestore() }
            Button { text: "按当前位置继续"; onClicked: page.backend.media.continueAtCurrentPosition() }
        }
        RowLayout {
            visible: page.backend.videoOpen.pending || page.backend.videoOpen.needsPlayer
            Button { text: "取消打开"; objectName: "cancelVideoOpenButton"; onClicked: page.backend.videoOpen.cancel() }
        }
    }
}
