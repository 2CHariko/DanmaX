import QtQuick
import QtQuick.Controls.FluentWinUI3
import QtQuick.Layouts
import "../components"
import "../theme"
import "../i18n"

PageFrame {
    id: page
    required property QtObject backend
    required property Item searchField
    function focusFirstSearchResult() { searchPane.focusFirstResult() }
    signal openDanmakuForAnime(string title, string episodeTitle, real episodeNumber)
    property real detailSubjectId: 0
    function showDetails(anime) {
        if (!anime || !(Number(anime.id) > 0)) return
        detailSubjectId = Number(anime.id)
        backend.metadata.fetchDetail(detailSubjectId)
    }

    title: I18n.home.title
    description: I18n.home.description
    headingVisible: !searchPane.showingResults

    property int activeWeekday: backend.metadata.todayWeekday

    // 计算当前所选星期的番剧列表
    readonly property var currentWeekdayList: {
        const cal = page.backend.metadata.weeklyCalendar
        if (!cal || cal.length === 0) return page.backend.metadata.airingToday
        for (let i = 0; i < cal.length; ++i) {
            if (cal[i].weekdayId === page.activeWeekday) {
                return cal[i].items || []
            }
        }
        return page.backend.metadata.airingToday
    }

    // 格式化本地弹幕缓存为最近在看卡片列表
    readonly property var recentWatchedList: {
        const entries = page.backend.library.cachedEntries
        if (!entries || entries.length === 0) return []
        const list = []
        const covers = {}
        for (const day of page.backend.metadata.weeklyCalendar || []) {
            for (const anime of day.items || []) {
                if (anime.cover) { covers[anime.title] = anime.cover; covers[anime.originalTitle] = anime.cover }
            }
        }
        const seen = {}
        for (let i = 0; i < entries.length && list.length < 12; ++i) {
            const entry = entries[i]
            if (!entry.valid || !entry.key) continue
            const identity = entry.animeTitle || entry.key
            if (seen[identity]) continue
            seen[identity] = true
            list.push({
                id: entry.episodeId || (i + 1),
                title: entry.animeTitle || entry.key,
                originalTitle: entry.episodeTitle,
                airDate: entry.episodeTitle || "",
                cover: entry.imageUrl || covers[entry.animeTitle] || "",
                score: "",
                badge: I18n.home.cachedBadge,
                key: entry.key
            })
        }
        return list
    }

    AnimeSearchPane {
        id: searchPane
        metadata: page.backend.metadata
        onDetailsRequested: anime => page.showDetails(anime)
        onRevealRequested: item => page.reveal(item)
    }
    Shortcut {
        objectName: "homeAnimeSearchShortcut"
        sequences: [StandardKey.Find]
        enabled: page.visible
        onActivated: {
            page.searchField.contentItem.forceActiveFocus(Qt.ShortcutFocusReason)
        }
    }

    RowLayout {
        visible: !searchPane.showingResults
        Layout.fillWidth: true
        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: page.backend.metadata.loading ? (page.backend.metadata.weeklyCalendar.length > 0 ? I18n.home.updatingCache : I18n.home.loadingCalendar) : page.backend.metadata.error
            visible: text.length > 0
            color: Ui.secondaryText
            Accessible.role: Accessible.AlertMessage
        }
        Button {
            objectName: "refreshCalendar"
            text: I18n.home.refreshSchedule
            enabled: !page.backend.metadata.loading
            onClicked: page.backend.metadata.refreshCalendar(true)
        }
    }

    // 1. 顶部 Hero Banner 流媒体焦点推荐
    HeroBanner {
        id: heroBanner
        visible: !searchPane.showingResults
        anime: page.backend.metadata.heroAnime
        coverCache: page.backend.metadata.coverCache
        onWatchNowClicked: title => page.openDanmakuForAnime(title, "", 0)
        onDetailsClicked: anime => page.showDetails(anime)
    }

    // 2. 轨道一：继续追番 / 本地缓存历史（有历史记录时展示）
    AnimeRail {
        coverCache: page.backend.metadata.coverCache
        visible: !searchPane.showingResults && Boolean(page.recentWatchedList && page.recentWatchedList.length > 0)
        title: I18n.home.continueWatching
        model: page.recentWatchedList
        onCardClicked: anime => {
            if (!anime) return
            if (anime.key && anime.key.length > 0) {
                page.backend.loadCachedDanmaku(anime.key)
            } else {
                page.openDanmakuForAnime(anime.title, "", 0)
            }
        }
    }

    // 3. 轨道二：今日新番 / 追番周历（支持周一至周日 7 天 Tab 快速切换）
    AnimeRail {
        id: weeklyRail
        coverCache: page.backend.metadata.coverCache
        visible: !searchPane.showingResults
        title: I18n.home.weeklyCalendar
        weekdayTabs: I18n.home.weekdays
        selectedWeekday: page.activeWeekday
        model: page.currentWeekdayList
        onWeekdaySelected: day => {
            page.activeWeekday = day
        }
        onCardClicked: anime => {
            page.showDetails(anime)
        }
    }

    // 4. 轨道三：季度热门 / 评分榜单
    AnimeRail {
        id: topRatedRail
        coverCache: page.backend.metadata.coverCache
        visible: !searchPane.showingResults
        title: I18n.home.topRated
        model: page.backend.metadata.topRated
        onCardClicked: anime => {
            page.showDetails(anime)
        }
    }

    // 5. 番剧详情弹层
    AnimeDetailDialog {
        id: detailDialog
        ownerVisible: page.visible
        detail: page.backend.metadata.currentDetail
        loading: page.backend.metadata.loadingDetail
        stale: page.backend.metadata.detailStale
        coverCache: page.backend.metadata.coverCache
        error: page.backend.metadata.detailError
        onRetryRequested: page.backend.metadata.fetchDetail(page.detailSubjectId, true)
        onCloseRequested: page.backend.metadata.clearDetail()
        onWatchNowRequested: animeTitle => {
            page.backend.metadata.clearDetail()
            page.openDanmakuForAnime(animeTitle, "", 0)
        }
        onEpisodeSelected: (animeTitle, episodeTitle, episodeNumber) => {
            page.backend.metadata.clearDetail()
            page.openDanmakuForAnime(animeTitle, episodeTitle, episodeNumber)
        }
    }

    Component.onCompleted: {
        page.backend.metadata.refreshCalendar(false)
    }
    onVisibleChanged: {
        if (!backend) return
        if (!visible) backend.metadata.clearDetail()
        else backend.metadata.refreshCalendar(false)
    }
}
