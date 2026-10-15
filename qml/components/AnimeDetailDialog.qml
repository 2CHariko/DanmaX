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
    property bool loadingSubject: false
    property bool loadingEpisodes: false
    property bool episodesAvailable: false
    required property QtObject coverCache
    property bool stale: false
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
    width: parent ? Math.min(parent.width - 24, Math.round(780 * Ui.textScale)) : 780
    height: parent ? Math.min(parent.height - 24, Math.round(620 * Ui.textScale)) : 620
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape
    visible: ownerVisible && (hasValidDetail || loading || error.length > 0)
    title: loading && !hasValidDetail ? I18n.common.loading : ((detail && detail.title) || I18n.home.heroDetails)
    standardButtons: Dialog.Close
    onRejected: closeRequested()
    Binding { target: dialogRoot.enter; property: "enabled"; value: Ui.motionEnabled }
    Binding { target: dialogRoot.exit; property: "enabled"; value: Ui.motionEnabled }
    ContentReveal { id: headerReveal; target: detailHeader }
    ContentReveal { id: episodesReveal; target: episodesFlow }

    contentItem: ScrollView {
        id: detailScroll
        clip: true
        contentWidth: availableWidth
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        rightPadding: 16 + Ui.scrollGap

        ColumnLayout {
            width: detailScroll.availableWidth
            spacing: 16

            RowLayout {
                Layout.fillWidth: true
                BusyIndicator {
                    running: dialogRoot.visible && dialogRoot.loading
                    visible: running
                }
                Label {
                    Layout.fillWidth: true
                    text: dialogRoot.loading ? (dialogRoot.stale ? I18n.home.updatingCache : I18n.common.loading) : ""
                    color: Ui.secondaryText
                    wrapMode: Text.WordWrap
                    Accessible.role: Accessible.AlertMessage
                }
                Button {
                    objectName: "refreshAnimeDetail"
                    text: I18n.common.refresh
                    enabled: !dialogRoot.loading
                    onClicked: dialogRoot.retryRequested()
                }
            }

            LoadingPlaceholder {
                objectName: "animeDetailSkeleton"
                appearance: "detail"
                loading: dialogRoot.loadingSubject
                hasContent: dialogRoot.hasValidDetail
                onContentReady: headerReveal.restart()
            }

            // 错误提示与重试卡片
            Rectangle {
                Layout.fillWidth: true
                visible: dialogRoot.error.length > 0
                implicitHeight: errorLayout.implicitHeight + 24
                radius: Ui.cardRadius
                color: Ui.dark ? Qt.rgba(0.9, 0.2, 0.2, 0.12) : Qt.rgba(0.9, 0.1, 0.1, 0.08)
                border.width: 1
                border.color: Ui.dark ? Qt.rgba(0.9, 0.3, 0.3, 0.35) : Qt.rgba(0.9, 0.2, 0.2, 0.25)

                RowLayout {
                    id: errorLayout
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 12

                    Label {
                        Layout.fillWidth: true
                        text: dialogRoot.error
                        wrapMode: Text.WordWrap
                        color: Ui.dark ? "#FF9999" : "#C42B1C"
                        Accessible.role: Accessible.AlertMessage
                    }

                    Button {
                        visible: !dialogRoot.loading
                        text: I18n.common.refresh
                        onClicked: dialogRoot.retryRequested()
                    }
                }
            }

            // 顶部核心媒体卡片（海报 + 标题 + 评分/元数据 + 主行动 CTA 按钮）
            Rectangle {
                id: detailHeader
                Layout.fillWidth: true
                visible: dialogRoot.hasValidDetail
                implicitHeight: headerRow.implicitHeight + 24
                radius: Ui.cardRadius
                color: Ui.cardBackground
                border.width: 1
                border.color: Ui.cardBorder

                RowLayout {
                    id: headerRow
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 16

                    // 左侧海报封面容器（固定经典比例、圆角微边、加载骨架占位）
                    Rectangle {
                        Layout.preferredWidth: Math.round(130 * Ui.textScale)
                        Layout.preferredHeight: Math.round(180 * Ui.textScale)
                        Layout.alignment: Qt.AlignTop
                        radius: Ui.cardRadius
                        color: Ui.dark ? Qt.rgba(1, 1, 1, 0.06) : Qt.rgba(0, 0, 0, 0.05)
                        border.width: 1
                        border.color: Ui.cardBorder
                        clip: true

                        CachedCoverImage {
                            id: posterImg
                            anchors.fill: parent
                            cacheService: dialogRoot.coverCache
                            remoteSource: (dialogRoot.detail && dialogRoot.detail.cover) || ""
                            sourceSize.width: 320
                            fillMode: Image.PreserveAspectCrop
                            asynchronous: true
                            cache: true
                        }
                    }

                    // 右侧核心信息流与主行动区
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.alignment: Qt.AlignTop
                        spacing: 8

                        // 标题区：中文主标题 + 原名/外文副标题
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2

                            Label {
                                Layout.fillWidth: true
                                text: (dialogRoot.detail && dialogRoot.detail.title) || ""
                                font.pixelSize: Ui.subtitleSize
                                font.weight: Font.Bold
                                color: Ui.textColor
                                wrapMode: Text.Wrap
                                maximumLineCount: 2
                                elide: Text.ElideRight
                            }

                            Label {
                                Layout.fillWidth: true
                                visible: text.length > 0 && text !== (dialogRoot.detail && dialogRoot.detail.title)
                                text: (dialogRoot.detail && dialogRoot.detail.originalTitle) || ""
                                font.pixelSize: Ui.captionSize
                                color: Ui.secondaryText
                                elide: Text.ElideRight
                            }
                        }

                        // 元信息徽章行：评分胶囊、评分人数、首播、平台类型
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6

                            // 评分高光胶囊
                            Rectangle {
                                readonly property string scoreText: (dialogRoot.detail && dialogRoot.detail.score) || ""
                                visible: scoreText.length > 0
                                implicitWidth: scoreInner.implicitWidth + 10
                                implicitHeight: scoreInner.implicitHeight + 4
                                radius: 4
                                color: Ui.dark ? Qt.rgba(1, 0.75, 0, 0.16) : Qt.rgba(1, 0.6, 0, 0.12)
                                border.width: 1
                                border.color: Ui.dark ? Qt.rgba(1, 0.8, 0, 0.35) : Qt.rgba(0.9, 0.5, 0, 0.3)

                                Label {
                                    id: scoreInner
                                    anchors.centerIn: parent
                                    text: I18n.format(I18n.home.heroRatingFormat, parent.scoreText)
                                    font.pixelSize: Ui.captionSize
                                    font.weight: Font.Bold
                                    color: Ui.dark ? "#FFD043" : "#BC4B09"
                                }
                            }

                            // 评分人数
                            Label {
                                visible: Boolean(dialogRoot.detail && Number(dialogRoot.detail.ratingCount) > 0)
                                text: I18n.format(I18n.home.ratingCountFormat, (dialogRoot.detail && dialogRoot.detail.ratingCount) || 0)
                                font.pixelSize: Ui.captionSize
                                color: Ui.secondaryText
                            }

                            // 分隔符
                            Label {
                                visible: Boolean(dialogRoot.detail && dialogRoot.detail.airDate)
                                text: "·"
                                font.pixelSize: Ui.captionSize
                                color: Ui.secondaryText
                            }

                            // 首播日期
                            Label {
                                visible: Boolean(dialogRoot.detail && dialogRoot.detail.airDate)
                                text: (dialogRoot.detail && dialogRoot.detail.airDate) || ""
                                font.pixelSize: Ui.captionSize
                                color: Ui.secondaryText
                            }

                            // 平台/载体
                            Label {
                                visible: Boolean(dialogRoot.detail && dialogRoot.detail.platform)
                                text: "·  " + ((dialogRoot.detail && dialogRoot.detail.platform) || "")
                                font.pixelSize: Ui.captionSize
                                color: Ui.secondaryText
                            }
                        }

                        // 题材标签行 (Tags 胶囊)
                        Flow {
                            Layout.fillWidth: true
                            spacing: 6
                            visible: Boolean(dialogRoot.detail && dialogRoot.detail.tags && dialogRoot.detail.tags.length > 0)

                            Repeater {
                                model: (dialogRoot.detail && dialogRoot.detail.tags) || []
                                delegate: Rectangle {
                                    required property string modelData
                                    implicitWidth: tagLabel.implicitWidth + 10
                                    implicitHeight: tagLabel.implicitHeight + 4
                                    radius: 4
                                    color: Ui.dark ? Qt.rgba(1, 1, 1, 0.07) : Qt.rgba(0, 0, 0, 0.05)
                                    border.width: 1
                                    border.color: Ui.cardBorder

                                    Label {
                                        id: tagLabel
                                        anchors.centerIn: parent
                                        text: modelData
                                        font.pixelSize: Math.round(11 * Ui.textScale)
                                        color: Ui.secondaryText
                                    }
                                }
                            }
                        }

                        Item { Layout.fillHeight: true }

                        // 主行动点（CTA）操作栏
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 12

                            Button {
                                id: watchNowBtn
                                highlighted: true
                                icon.source: Ui.icon("play")
                                icon.width: 16
                                icon.height: 16
                                text: I18n.home.searchDanmakuTitle
                                font.weight: Font.DemiBold
                                Accessible.name: text
                                onClicked: dialogRoot.watchNowRequested((dialogRoot.detail && dialogRoot.detail.title) || "")
                            }

                            Label {
                                Layout.fillWidth: true
                                text: I18n.home.singleEpisodeTip
                                font.pixelSize: Ui.captionSize
                                color: Ui.secondaryText
                                elide: Text.ElideRight
                            }
                        }
                    }
                }
            }

            // 分集选集区
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 8
                visible: dialogRoot.hasValidDetail

                // 选集标题与话数提示
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Label {
                        text: I18n.home.episodesSectionTitle
                        font.pixelSize: Ui.sectionHeaderSize
                        font.weight: Font.DemiBold
                        color: Ui.textColor
                    }

                    Label {
                        objectName: "animeEpisodeCount"
                        text: !dialogRoot.episodesAvailable ? (dialogRoot.loadingEpisodes ? I18n.common.loading : "")
                            : I18n.format(I18n.home.episodesCountFormat, ((dialogRoot.detail && dialogRoot.detail.episodes) || []).length)
                        font.pixelSize: Ui.captionSize
                        color: Ui.secondaryText
                    }
                }

                // 选集按钮网格
                LoadingPlaceholder {
                    objectName: "animeEpisodesSkeleton"
                    appearance: "episodes"
                    loading: dialogRoot.loadingEpisodes
                    hasContent: dialogRoot.episodesAvailable
                    onContentReady: episodesReveal.restart()
                }
                Flow {
                    id: episodesFlow
                    objectName: "animeEpisodesContent"
                    visible: dialogRoot.episodesAvailable && ((dialogRoot.detail && dialogRoot.detail.episodes) || []).length > 0
                    Layout.fillWidth: true
                    spacing: 8

                    Repeater {
                        model: (dialogRoot.detail && dialogRoot.detail.episodes) || []
                        delegate: Button {
                            id: epBtn
                            required property var modelData
                            objectName: "animeEpisode" + modelData.sort

                            readonly property real sortNum: Number(modelData.sort) || 0
                            readonly property string displayNum: {
                                if (sortNum <= 0) return String(modelData.sort || "")
                                if (Math.floor(sortNum) === sortNum) {
                                    return sortNum < 10 ? ("0" + sortNum) : String(sortNum)
                                }
                                return String(sortNum)
                            }

                            text: displayNum
                            font.pixelSize: Math.round(13 * Ui.textScale)
                            font.weight: Font.DemiBold

                            // 统一规整方块尺寸，内部居中对齐，彻底根除省略号截断
                            implicitWidth: Math.round(52 * Ui.textScale)
                            implicitHeight: Math.round(36 * Ui.textScale)
                            leftPadding: 4
                            rightPadding: 4

                            Accessible.name: I18n.format(I18n.home.episodeLoadDanmaku, modelData.sort) +
                                (modelData.title && modelData.title.length > 0 ? (" · " + modelData.title) : "")

                            // 悬停提示完整信息（集数 + 详细标题 + 播出时间）
                            ToolTip.visible: hovered
                            ToolTip.delay: 200
                            ToolTip.text: {
                                var numStr = I18n.format(I18n.home.episodeNumberFormat, modelData.sort)
                                var titleStr = String(modelData.title || "").trim()
                                var res = numStr
                                if (titleStr.length > 0 && titleStr !== numStr) {
                                    res += "  " + titleStr
                                }
                                if (modelData.airDate && modelData.airDate.length > 0) {
                                    res += " (" + modelData.airDate + ")"
                                }
                                return res
                            }

                            onClicked: dialogRoot.episodeSelected(
                                (dialogRoot.detail && dialogRoot.detail.title) || "",
                                modelData.title || "",
                                Number(modelData.sort)
                            )
                        }
                    }
                }

                Label {
                    visible: dialogRoot.episodesAvailable && !dialogRoot.loadingEpisodes && ((dialogRoot.detail && dialogRoot.detail.episodes) || []).length === 0
                    text: I18n.home.noEpisodes
                    color: Ui.secondaryText
                    font.pixelSize: Ui.captionSize
                }
            }

            // 剧情简介区（独立轻量微卡片，自然适应长文本滚动）
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 6
                visible: dialogRoot.hasValidDetail

                Label {
                    text: I18n.home.summarySectionTitle
                    font.pixelSize: Ui.sectionHeaderSize
                    font.weight: Font.DemiBold
                    color: Ui.textColor
                }

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: summaryText.implicitHeight + 20
                    radius: Ui.cardRadius
                    color: Ui.dark ? Qt.rgba(1, 1, 1, 0.03) : Qt.rgba(0, 0, 0, 0.02)
                    border.width: 1
                    border.color: Ui.cardBorder

                    Label {
                        id: summaryText
                        anchors.fill: parent
                        anchors.margins: 10
                        text: (dialogRoot.detail && dialogRoot.detail.summary) || I18n.home.noSummary
                        wrapMode: Text.Wrap
                        color: Ui.secondaryText
                        lineHeight: 1.3
                        font.pixelSize: Ui.captionSize
                    }
                }
            }
        }
    }
}
