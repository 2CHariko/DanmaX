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
    property int episodePage: 0
    property int episodeTotal: 0
    property int episodePageCount: 1
    required property QtObject coverCache
    property bool stale: false
    property string error: ""
    property bool ownerVisible: true
    property bool requested: false
    function present() {
        const ready = opened
        requested = true
        if (ready) loadRequested()
    }
    onOwnerVisibleChanged: if (!ownerVisible) {
        requested = false
        resetEpisodes()
    }
    onOpened: {
        syncEpisodes()
        if (requested) loadRequested()
    }
    onClosed: {
        requested = false
        resetEpisodes()
    }

    readonly property var sourceEpisodes: (detail && detail.episodes) || []
    property var episodeSnapshot: []
    readonly property int preparedEpisodeCount: episodeModel.count
    readonly property bool preparingEpisodes: episodesAvailable && preparedEpisodeCount < sourceEpisodes.length
    onSourceEpisodesChanged: syncEpisodes()
    onEpisodesAvailableChanged: syncEpisodes()
    function resetEpisodes() {
        episodeSnapshot = []
        episodeModel.clear()
    }
    function syncEpisodes() {
        if (!opened || !ownerVisible || !episodesAvailable) {
            resetEpisodes()
            return
        }
        // detailChanged also updates status/subject fields: keep existing delegates
        // when the episode page itself has not changed.
        let same = episodeSnapshot.length === sourceEpisodes.length
        for (let i = 0; same && i < sourceEpisodes.length; ++i) {
            const a = episodeSnapshot[i], b = sourceEpisodes[i]
            same = a.id === b.id && a.sort === b.sort && a.title === b.title && a.airDate === b.airDate
        }
        if (!same) {
            episodeModel.clear()
            episodeSnapshot = sourceEpisodes
        }
    }
    ListModel { id: episodeModel }
    FrameAnimation {
        objectName: "animeEpisodePreparation"
        running: dialogRoot.opened && dialogRoot.ownerVisible
            && episodeModel.count < dialogRoot.episodeSnapshot.length
        onTriggered: {
            // Bound both the work count and its time. Appending preserves already
            // created delegates instead of resetting the Repeater's whole model.
            const start = Date.now()
            for (let n = 0; n < 8 && episodeModel.count < dialogRoot.episodeSnapshot.length; ++n) {
                episodeModel.append({ entry: dialogRoot.episodeSnapshot[episodeModel.count] })
                if (Date.now() - start >= 2) break
            }
        }
    }
    property bool tagsExpanded: false
    property real tagsSubjectId: 0
    onDetailChanged: {
        const id = Number(detail && detail.id) || 0
        if (id !== tagsSubjectId) {
            tagsSubjectId = id
            tagsExpanded = false
        }
    }
    readonly property bool hasValidDetail: Boolean(detail && Number(detail.id) > 0)
    property string currentDate: Qt.formatDateTime(new Date(), "yyyy-MM-dd")

    function isFutureEpisode(value) {
        var text = String(value || "")
        if (!/^\d{4}-\d{2}-\d{2}$/.test(text)) return false
        var date = new Date(Number(text.slice(0, 4)), Number(text.slice(5, 7)) - 1, Number(text.slice(8, 10)))
        if (Qt.formatDateTime(date, "yyyy-MM-dd") !== text) return false
        return text > currentDate
    }

    onAboutToShow: currentDate = Qt.formatDateTime(new Date(), "yyyy-MM-dd")
    Timer {
        interval: 60000
        repeat: true
        running: dialogRoot.visible
        onTriggered: dialogRoot.currentDate = Qt.formatDateTime(new Date(), "yyyy-MM-dd")
    }

    signal closeRequested()
    signal loadRequested()
    signal retryRequested()
    signal episodePageRequested(int page)
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
    visible: ownerVisible && (requested || hasValidDetail || loading || error.length > 0)
    title: (detail && detail.title) || I18n.home.heroDetails
    header: null
    // Keep the native scrollbar's full hit area inside the shared outer inset.
    leftPadding: Ui.scrollInset(detailVerticalBar.implicitWidth, padding)
    topPadding: leftPadding
    rightPadding: leftPadding
    bottomPadding: leftPadding
    standardButtons: Dialog.NoButton
    onRejected: closeRequested()
    footer: DialogButtonBox {
        alignment: Qt.AlignRight
        Button {
            objectName: "closeAnimeDetail"
            text: I18n.common.close
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            onClicked: dialogRoot.closeRequested()
        }
    }
    Binding { target: dialogRoot.enter; property: "enabled"; value: Ui.motionEnabled }
    Binding { target: dialogRoot.exit; property: "enabled"; value: Ui.motionEnabled }
    ContentReveal { id: headerReveal; target: detailHeader }
    ContentReveal { id: episodesReveal; target: episodesFlow }

    contentItem: ScrollView {
        id: detailScroll
        Accessible.name: dialogRoot.title
        clip: true
        padding: 0
        contentWidth: availableWidth
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        rightPadding: 0
        ScrollBar.vertical: AppScrollBar {
            id: detailVerticalBar
            objectName: "animeDetailVerticalScrollBar"
            viewport: detailScroll
            scrollTarget: detailScroll.contentItem
            wheelBottom: dialogRoot.footer.y
            Accessible.name: I18n.common.pageScrollBarAccessible
        }

        ColumnLayout {
            width: detailScroll.availableWidth
            spacing: 16

            LoadingPlaceholder {
                objectName: "animeDetailSkeleton"
                appearance: "detail"
                loading: dialogRoot.opened && dialogRoot.loadingSubject
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
                        Layout.alignment: Qt.AlignVCenter
                    }

                    Label {
                        objectName: "animeEpisodeCount"
                        text: dialogRoot.episodesAvailable || dialogRoot.episodeTotal > 0
                            ? I18n.format(I18n.home.episodesCountFormat, dialogRoot.episodeTotal)
                            : (dialogRoot.loadingEpisodes ? I18n.common.loading : "")
                        font.pixelSize: Ui.captionSize
                        color: Ui.secondaryText
                        Layout.alignment: Qt.AlignVCenter
                    }

                    Item {
                        Layout.fillWidth: true
                    }

                    // 紧凑分页控制器（靠右对齐，与标题同行，消除纵向冗余与横向过度拉伸）
                    RowLayout {
                        visible: dialogRoot.episodePageCount > 1
                        spacing: 4
                        Layout.alignment: Qt.AlignVCenter

                        Button {
                            id: prevRangeBtn
                            objectName: "animeEpisodesPrevious"
                            icon.source: Ui.icon("chevron-left")
                            icon.width: 16
                            icon.height: 16
                            display: AbstractButton.IconOnly
                            Accessible.name: I18n.home.previousEpisodeRange
                            enabled: dialogRoot.episodePage > 0
                            implicitWidth: implicitHeight
                            implicitHeight: episodeRangeCombo.implicitHeight
                            onClicked: dialogRoot.episodePageRequested(dialogRoot.episodePage - 1)
                        }

                        AppComboBox {
                            id: episodeRangeCombo
                            objectName: "animeEpisodeRange"
                            implicitWidth: Math.round(136 * Ui.textScale)
                            Accessible.name: I18n.home.episodeRange
                            model: {
                                const ranges = []
                                for (let i = 0; i < dialogRoot.episodePageCount; ++i)
                                    ranges.push(I18n.format(I18n.home.episodeRangeFormat, i * 100 + 1,
                                        Math.min((i + 1) * 100, dialogRoot.episodeTotal)))
                                return ranges
                            }
                            currentIndex: dialogRoot.episodePage
                            onActivated: index => dialogRoot.episodePageRequested(index)
                        }

                        Button {
                            id: nextRangeBtn
                            objectName: "animeEpisodesNext"
                            icon.source: Ui.icon("chevron-right")
                            icon.width: 16
                            icon.height: 16
                            display: AbstractButton.IconOnly
                            Accessible.name: I18n.home.nextEpisodeRange
                            enabled: dialogRoot.episodePage + 1 < dialogRoot.episodePageCount
                            implicitWidth: implicitHeight
                            implicitHeight: episodeRangeCombo.implicitHeight
                            onClicked: dialogRoot.episodePageRequested(dialogRoot.episodePage + 1)
                        }
                    }
                }

                // Reserve the complete grid height while its controls are prepared.
                Item {
                    Layout.fillWidth: true
                    visible: (dialogRoot.loadingEpisodes && !dialogRoot.episodesAvailable)
                        || dialogRoot.sourceEpisodes.length > 0
                    readonly property real cellWidth: Math.round(52 * Ui.textScale)
                    readonly property real cellHeight: Math.round(36 * Ui.textScale)
                    readonly property int columns: Math.max(1, Math.floor((width + 8) / (cellWidth + 8)))
                    implicitHeight: dialogRoot.episodesAvailable && dialogRoot.sourceEpisodes.length > 0
                        ? Math.ceil(dialogRoot.sourceEpisodes.length / columns) * (cellHeight + 8) - 8
                        : episodesPlaceholder.implicitHeight
                    LoadingPlaceholder {
                        id: episodesPlaceholder
                        anchors.fill: parent
                        objectName: "animeEpisodesSkeleton"
                        appearance: "episodes"
                        loading: dialogRoot.opened && (dialogRoot.loadingEpisodes || dialogRoot.preparingEpisodes)
                        hasContent: dialogRoot.preparedEpisodeCount > 0
                            || (dialogRoot.episodesAvailable && !dialogRoot.preparingEpisodes)
                        onContentReady: episodesReveal.restart()
                    }
                    Flow {
                        id: episodesFlow
                        objectName: "animeEpisodesContent"
                        visible: dialogRoot.episodesAvailable && dialogRoot.preparedEpisodeCount > 0
                        width: parent.width
                        spacing: 8

                        Repeater {
                            model: episodeModel
                            delegate: Item {
                                id: episodeDelegate
                                required property var entry
                                readonly property var modelData: entry
                                implicitWidth: epBtn.implicitWidth
                                implicitHeight: epBtn.implicitHeight
                                HoverHandler {
                                    id: episodeHover
                                    objectName: "animeEpisodeHover" + episodeDelegate.modelData.sort
                                }
                                // 悬停提示完整信息（集数 + 详细标题 + 播出时间）
                                ToolTip.visible: episodeHover.hovered
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

                                Button {
                                    id: epBtn
                                    anchors.fill: parent
                                    readonly property var modelData: episodeDelegate.modelData
                                    objectName: "animeEpisode" + modelData.sort
                                    enabled: !dialogRoot.isFutureEpisode(modelData.airDate)

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

                                    onClicked: dialogRoot.episodeSelected(
                                        (dialogRoot.detail && dialogRoot.detail.title) || "",
                                        modelData.title || "",
                                        Number(modelData.sort)
                                    )
                                }
                            }
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

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 8
                visible: Boolean(dialogRoot.detail && dialogRoot.detail.tags && dialogRoot.detail.tags.length > 0)
                Label {
                    text: I18n.home.allTags
                    font.pixelSize: Ui.sectionHeaderSize
                    font.weight: Font.DemiBold
                }
                Flow {
                    id: tagsFlow
                    objectName: "animeDetailTags"
                    Layout.fillWidth: true
                    spacing: 6
                    readonly property var tags: (dialogRoot.detail && dialogRoot.detail.tags) || []
                    readonly property real chipHeight: Math.ceil(tagMetrics.height) + 8
                    // Reserve the trailing action before selecting a complete prefix.
                    readonly property int collapsedCount: {
                        let used = 0
                        const widths = tags.map(tag => Math.min(Math.ceil(tagMetrics.advanceWidth(String(tag))) + 18, width))
                        for (let i = 0; i < widths.length; ++i)
                            used += widths[i] + (i > 0 ? spacing : 0)
                        if (used <= width) return tags.length
                        const available = Math.max(0, width - expandTagsButton.implicitWidth - spacing)
                        used = 0
                        let count = 0
                        for (const chipWidth of widths) {
                            const next = used + (count > 0 ? spacing : 0) + chipWidth
                            if (next > available) break
                            used = next
                            ++count
                        }
                        return count
                    }
                    FontMetrics {
                        id: tagMetrics
                        font.pixelSize: Ui.captionSize
                    }
                    Repeater {
                        model: tagsFlow.tags
                        delegate: Rectangle {
                            required property string modelData
                            required property int index
                            visible: dialogRoot.tagsExpanded || index < tagsFlow.collapsedCount
                            implicitWidth: Math.min(tagLabel.implicitWidth + 16, tagsFlow.width)
                            implicitHeight: dialogRoot.tagsExpanded ? tagLabel.implicitHeight + 8 : tagsFlow.chipHeight
                            radius: Ui.cardRadius
                            color: Ui.cardBackground
                            border.width: 1
                            border.color: Ui.cardBorder
                            Label {
                                id: tagLabel
                                anchors.centerIn: parent
                                readonly property bool isLongTag: (implicitWidth + 16) > tagsFlow.width
                                width: isLongTag ? Math.max(0, parent.width - 16) : implicitWidth
                                text: modelData
                                wrapMode: (isLongTag && dialogRoot.tagsExpanded) ? Text.WrapAnywhere : Text.NoWrap
                                elide: (isLongTag && !dialogRoot.tagsExpanded) ? Text.ElideRight : Text.ElideNone
                                font.pixelSize: Ui.captionSize
                                color: Ui.secondaryText
                            }
                        }
                    }
                    Button {
                        id: expandTagsButton
                        objectName: "animeDetailExpandTags"
                        visible: tagsFlow.collapsedCount < tagsFlow.tags.length
                        text: dialogRoot.tagsExpanded ? I18n.home.collapseTags : I18n.home.expandTags
                        font.pixelSize: Ui.captionSize
                        leftPadding: 8
                        rightPadding: 8
                        topPadding: 4
                        bottomPadding: 4
                        topInset: 0
                        bottomInset: 0
                        implicitWidth: Math.ceil(contentItem.implicitWidth) + leftPadding + rightPadding
                        implicitHeight: tagsFlow.chipHeight
                        onClicked: dialogRoot.tagsExpanded = !dialogRoot.tagsExpanded
                    }
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
