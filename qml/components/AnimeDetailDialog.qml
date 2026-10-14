import QtQuick
import QtQuick.Controls.FluentWinUI3
import QtQuick.Layouts
import "../theme"
import "../i18n"

Dialog {
    id: dialogRoot
    objectName: "animeDetailDialog"
    property var detail: ({})
    property bool loading: false
    property string error: ""
    property bool ownerVisible: true
    readonly property bool hasValidDetail: Boolean(detail && Number(detail.id) > 0)
    signal closeRequested()
    signal retryRequested()
    signal watchNowRequested(string animeTitle)
    signal episodeSelected(string animeTitle, string episodeTitle, real episodeNumber)

    parent: Overlay.overlay
    x: parent ? Math.max(8, (parent.width - width) / 2) : 0
    y: parent ? Math.max(8, (parent.height - height) / 2) : 0
    width: parent ? Math.min(parent.width - 16, 760 * Ui.textScale) : 760
    height: parent ? Math.min(parent.height - 16, 640 * Ui.textScale) : 640
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape
    visible: ownerVisible && (hasValidDetail || loading || error.length > 0)
    title: loading && !hasValidDetail ? I18n.common.loading : ((detail && detail.title) || I18n.home.heroDetails)
    standardButtons: Dialog.Close
    onRejected: closeRequested()

    contentItem: ScrollView {
        id: detailScroll
        clip: true
        contentWidth: availableWidth
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        rightPadding: 16 + Ui.scrollGap
        ColumnLayout {
            width: detailScroll.availableWidth
            spacing: 12
            BusyIndicator { running: dialogRoot.loading; visible: running; Layout.alignment: Qt.AlignHCenter }
            Label {
                Layout.fillWidth: true
                visible: dialogRoot.error.length > 0
                text: dialogRoot.error
                wrapMode: Text.WordWrap
                Accessible.role: Accessible.AlertMessage
            }
            Button {
                visible: dialogRoot.error.length > 0 && !dialogRoot.loading
                text: I18n.common.refresh
                onClicked: dialogRoot.retryRequested()
            }
            Image {
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredWidth: Math.min(160 * Ui.textScale, detailScroll.availableWidth)
                Layout.preferredHeight: 210 * Ui.textScale
                visible: source.toString().length > 0
                source: (dialogRoot.detail && dialogRoot.detail.cover) || ""
                sourceSize.width: 320
                fillMode: Image.PreserveAspectFit
                asynchronous: true
            }
            Label {
                Layout.fillWidth: true
                visible: dialogRoot.hasValidDetail
                text: (dialogRoot.detail && dialogRoot.detail.summary) || I18n.home.noSummary
                wrapMode: Text.Wrap
                color: Ui.textColor
            }
            Button {
                visible: dialogRoot.hasValidDetail
                text: I18n.home.searchDanmakuTitle
                onClicked: dialogRoot.watchNowRequested(dialogRoot.detail.title)
            }
            Label {
                visible: dialogRoot.hasValidDetail
                text: I18n.format(I18n.home.episodesCountFormat, ((dialogRoot.detail && dialogRoot.detail.episodes) || []).length)
                color: Ui.secondaryText
            }
            Flow {
                Layout.fillWidth: true
                Layout.preferredHeight: childrenRect.height
                spacing: 8
                Repeater {
                    model: (dialogRoot.detail && dialogRoot.detail.episodes) || []
                    delegate: Button {
                        required property var modelData
                        objectName: "animeEpisode" + modelData.sort
                        width: Math.min(implicitWidth, detailScroll.availableWidth)
                        text: I18n.format(I18n.home.episodeLoadDanmaku, modelData.sort) + " · " + modelData.title
                        Accessible.name: text
                        onClicked: dialogRoot.episodeSelected(dialogRoot.detail.title, modelData.title, Number(modelData.sort))
                    }
                }
            }
        }
    }
}
