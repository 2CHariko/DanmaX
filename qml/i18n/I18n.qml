pragma Singleton
import QtQuick

QtObject {
    id: root

    // 当前语言标识，预留后续多语言切换能力
    property string language: "zh_CN"

    // 格式化函数：替换模板中的 %1, %2, %3...
    function format(template, ...args) {
        if (!template) return ""
        let result = String(template)
        for (let i = 0; i < args.length; ++i) {
            result = result.split("%" + (i + 1)).join(String(args[i]))
        }
        return result
    }

    // 通用常用词汇与无障碍文本
    readonly property var common: ({
        ok: "确定",
        cancel: "取消",
        close: "关闭",
        retrySave: "重试保存",
        remove: "删除",
        refresh: "刷新",
        loading: "加载中…",
        unknown: "未知",
        percentSuffix: "%",
        errorPrefix: "错误：",
        pageScrollBarAccessible: "页面滚动条",
        optionsScrollBarAccessible: "选项列表滚动条",
        disclosureExpanded: "已展开",
        disclosureCollapsed: "已收起",
        disclosureCollapsePrefix: "收起：",
        disclosureExpandPrefix: "展开："
    })

    // 导航与顶层框架
    readonly property var nav: ({
        appTitle: "DanmaX",
        menu: "导航",
        expandNav: "展开导航",
        navExpanded: "导航已展开",
        navCollapsed: "导航已收起",
        play: "播放",
        logs: "日志",
        settings: "设置",
        about: "关于"
    })

    // 播放页
    readonly property var player: ({
        title: "播放",
        description: "选择弹幕来源，连接播放器，然后开始播放。",
        sourceTabsAccessible: "弹幕来源",
        sourceLocalTab: "本地 XML",
        sourceOnlineTab: "在线搜索",
        sourceCachedTab: "已缓存",

        // 本地 XML 来源
        localSectionTitle: "XML 弹幕文件",
        pathPlaceholder: "输入或选择 Bilibili XML 文件路径",
        pathAccessible: "弹幕文件路径",
        browseFile: "选择文件",
        browseDialogTitle: "选择弹幕 XML",
        browseFileFilter: "弹幕文件 (*.xml)",
        loadDanmaku: "载入弹幕",
        cancelLoad: "取消载入",
        currentDanmakuPrefix: "当前弹幕：",
        loadedCountFormat: "已载入 %1 条弹幕",

        // 播放方式
        modeSectionTitle: "播放方式",
        syncMode: "跟随播放器",
        manualMode: "独立播放",
        sessionWaitPlaceholder: "等待媒体会话，请先在播放器中播放视频",
        sessionAccessible: "目标播放器",
        sessionHint: "自动刷新媒体会话，跟随播放器的暂停和跳转。",
        sessionIdTitle: "手动指定应用 ID",
        sessionIdPlaceholder: "播放器应用 ID",
        sessionIdAccessible: "播放器应用 ID",
        sessionIdHint: "Windows 媒体会话标识（AUMID），例如 PotPlayer64 或应用包名；留空则自动匹配。",
        manualHint: "使用独立时间轴，可在下方暂停或调整进度。",

        // 播放控制
        controlSectionTitle: "播放控制",
        statusAccessiblePrefix: "播放状态：",
        blockedLoading: "正在载入弹幕，请稍候。",
        blockedNoDanmaku: "先选择并载入弹幕，再开始播放。",
        blockedNoSession: "请选择播放器，或切换为独立播放。",
        startPlayback: "开始播放",
        pausePlayback: "暂停",
        resumePlayback: "继续",
        stopPlayback: "停止并卸载",
        stopPlaybackDesc: "停止后需重新载入弹幕",
        timeManualSuffix: "（预计）",
        timeSyncSuffix: " · 由播放器控制",
        manualSeekAccessible: "独立播放进度",
        playerProgressAccessible: "播放器进度",
        runtimeDetailsTitle: "运行详情",
        runtimeDetailsFormat: "在屏 %1 · 丢弃 %2 · 内存 %3 MiB",
        heroStatusReady: "就绪，随时可播放",
        heroStatusWaiting: "等待配置",
        heroStatusRunning: "弹幕正在播放",
        danmakuBadgeNotLoaded: "未载入弹幕",
        danmakuBadgeReady: "%1 条弹幕",
        playerBadgeConnected: "已连接：%1",
        playerBadgeWaiting: "等待播放器中",
        playerBadgeManual: "独立播放模式",
        danmakuBadgeAccessible: "弹幕载入状态",
        playerBadgeAccessible: "播放器连接状态",
        loadedDanmakuCardTitle: "已就绪弹幕",
        clearDanmakuBtn: "清除",
        changeDanmakuBtn: "更换文件"
    })

    // 在线弹幕子面板
    readonly property var online: ({
        noServerConfigured: "请先配置允许匿名访问的弹弹play兼容服务。",
        openSettingsBtn: "打开在线服务设置",
        activeServerPrefix: "当前服务：",
        searchPlaceholder: "输入动画名称",
        searchAccessible: "动画搜索关键词",
        searchBtn: "搜索",
        animeLabel: "动画",
        animePlaceholder: "选择搜索结果中的动画",
        animeAccessible: "动画搜索结果",
        episodeLabel: "剧集",
        episodePlaceholder: "选择剧集",
        episodeAccessible: "动画剧集",
        loadCachedBtn: "载入缓存",
        downloadAndLoadBtn: "下载并载入",
        redownloadBtn: "重新下载",
        cancelBtn: "取消",
        progressAccessible: "在线弹幕操作进度",
        statusPrefix: "在线状态：",
        errorPrefix: "在线错误：",
        readyToPlayHint: "载入后，在播放控制中开始播放。"
    })

    // 缓存弹幕子面板
    readonly property var cache: ({
        filterPlaceholder: "筛选已缓存动画或剧集",
        filterAccessible: "缓存筛选",
        refreshBtn: "刷新",
        scanningHint: "正在读取缓存列表…",
        emptyMatched: "没有匹配的缓存",
        emptyHint: "还没有在线弹幕缓存。下载剧集后会显示在这里。",
        scrollBarAccessible: "缓存列表滚动条",
        unknownServer: "未知来源",
        itemCountFormat: "%1 条",
        unknownDownloadTime: "下载时间未知",
        invalidHint: "缓存不可载入",
        loadBtn: "载入",
        loadAccessibleFormat: "载入 %1 %2",
        deleteBtn: "删除",
        deleteAccessibleFormat: "删除缓存 %1 %2",
        cancelBtn: "取消",
        errorPrefix: "缓存错误：",
        deleteDialogTitle: "删除这条弹幕缓存？",
        deleteDialogContentSuffix: "\n已载入的弹幕仍可继续播放。"
    })

    // 字体选择器组件
    readonly property var fontSelector: ({
        accessibleName: "弹幕字体",
        accessibleDesc: "选择系统字体，或输入名称并按 Enter 确认。",
        previewText: "弹幕预览 Aa 123",
        previewAccessible: "弹幕字体预览",
        notFoundHint: "系统列表中未找到此名称；可能使用字体别名或回退字体。"
    })

    // 日志页
    readonly property var logs: ({
        title: "日志",
        levelLabel: "日志级别",
        filterAccessible: "日志过滤",
        filterAll: "全部",
        exportBtn: "导出全部",
        clearBtn: "清空全部",
        statsFormat: "共 %1 条 · 当前筛选 %2 条 · 最多保留 1000 条",
        emptyAll: "暂无日志",
        emptyFiltered: "当前筛选无结果",
        scrollBarAccessible: "日志滚动条",
        exportDialogTitle: "导出全部日志",
        exportFilter: "文本文件 (*.txt)"
    })

    // 设置页
    readonly property var settings: ({
        title: "设置",
        description: "修改后即时生效并保存。",

        // 外观
        appearanceSection: "应用外观",
        themeTitle: "应用主题",
        themeOptions: ["跟随系统", "浅色", "深色"],
        themeAccessible: "应用主题",
        backdropTitle: "窗口材质",
        backdropDefaultDesc: "使用 Mica 云母背景，系统限制时自动使用纯色。",
        backdropAccessible: "窗口材质 Mica",

        // 样式
        styleSection: "弹幕样式",
        fontFamilyTitle: "弹幕字体",
        fontFamilyDesc: "选择或输入字体名称；缺失字符自动回退。",
        fontSizeTitle: "字号",
        fontSizeDesc: "小字和大字按文件中的比例缩放。",
        fontSizeAccessible: "弹幕字号",
        strokeWidthTitle: "描边宽度",
        strokeWidthAccessible: "描边宽度",
        opacityTitle: "不透明度",
        opacityAccessible: "弹幕不透明度百分比",

        // 播放
        playbackSection: "弹幕播放",
        speedTitle: "滚动速度（逻辑像素/秒）",
        speedAccessible: "滚动速度（逻辑像素/秒）",
        maxActiveTitle: "最大在屏弹幕",
        maxActiveAccessible: "最大在屏弹幕",
        maxTracksTitle: "最大轨道数",
        maxTracksAccessible: "最大轨道数",
        fixedSecondsTitle: "固定弹幕时长（秒）",
        fixedSecondsAccessible: "固定弹幕时长",
        lineSpacingTitle: "轨道额外行距（%）",
        lineSpacingAccessible: "轨道额外行距",
        overlapTitle: "允许弹幕重叠",
        overlapDesc: "优先使用空闲轨道；轨道满时，开启则允许重叠，关闭则丢弃新弹幕。",
        overlapAccessible: "允许弹幕重叠",

        // 同步与窗口
        syncSection: "同步与窗口",
        timeOffsetTitle: "时间偏移（0.1 秒）",
        timeOffsetDesc: "正值提前显示弹幕。",
        timeOffsetAccessible: "时间偏移十分之一秒",
        screenTitle: "显示器",
        screenAccessible: "弹幕显示器",
        onTopTitle: "置顶策略",
        onTopOptions: ["不置顶", "置顶", "周期保持置顶", "兼容保持置顶"],
        onTopAccessible: "置顶策略",
        foregroundOnlyTitle: "仅播放器在前台时显示",
        foregroundOnlyDesc: "无法关联媒体应用与前台进程时请关闭此项。",
        foregroundOnlyAccessible: "仅播放器在前台显示",

        // 在线弹幕
        onlineSection: "在线弹幕",
        onlineServersTitle: "在线弹幕服务器",
        onlineServersDescFormat: "已配置 %1 个服务地址（首选：%2）",
        onlineServersEmptyDesc: "未配置在线弹幕服务地址",
        manageServersBtn: "管理服务...",
        serverDialogTitle: "管理在线弹幕服务器",
        onlineDesc: "按列表顺序尝试；失败或无结果时回退。各服务须共享弹弹play动画和剧集 ID。",
        serverPlaceholder: "https://服务器/路径前缀",
        serverAccessibleFormat: "在线弹幕服务地址 %1",
        moveUpBtn: "上移",
        moveUpAccessibleFormat: "上移服务 %1",
        moveDownBtn: "下移",
        moveDownAccessibleFormat: "下移服务 %1",
        removeBtn: "删除",
        removeAccessibleFormat: "删除服务 %1",
        serverEmptyIssue: "地址不能为空；不需要此项时请删除。",
        serverInvalidProtocolIssue: "协议无效，必须以 http:// 或 https:// 开头。",
        serverInvalidHostIssue: "地址格式无效，不能包含查询参数 (?)、锚点 (#) 或特殊字符。",
        serverDuplicateIssue: "该服务地址与列表中其他项重复。",
        serverSaveIssue: "地址保存失败，请检查配置错误提示。",
        addServerBtn: "添加服务地址",
        saveBtn: "保存",
        cancelBtn: "取消",
        onlineBottomHint: "缓存长期保留，可在播放页离线选择、重新下载或删除；本版本不提供在线账号登录。",
        serverEmptyPlaceholderTitle: "未配置任何在线弹幕服务器",
        serverEmptyPlaceholderDesc: "点击下方按钮添加第三方服务地址",

        // 文字缓存
        cacheSection: "文字缓存",
        budgetModeTitle: "预算模式",
        budgetModeDesc: "自动按当前弹幕需求增长，持续低负载后收缩；手动使用固定预算。",
        budgetModeOptions: ["自动", "手动"],
        budgetModeAccessible: "文字缓存预算模式",
        limitTitleAuto: "容量上限（MiB）",
        limitTitleManual: "固定预算（MiB）",
        limitDesc: "只限制文字图片内容，不提前分配；超限弹幕仍以文字显示。",
        budgetMiBAccessible: "文字缓存容量 MiB",

        // 诊断
        diagnosticsSection: "诊断",
        debugTitle: "显示调试信息",
        debugAccessible: "显示调试信息",
        debugPosTitle: "调试信息位置",
        debugPosOptions: ["左上", "右上", "左下", "右下"],
        debugPosAccessible: "调试位置",
        logLevelTitle: "日志级别",
        logLevelOptions: ["DEBUG", "INFO", "WARNING", "ERROR"],
        logLevelAccessible: "日志级别",
        logToFileTitle: "写入日志文件",
        logToFileDesc: "单文件 2 MiB，保留一份轮转备份。",
        logToFileAccessible: "写入日志文件",

        // 重置与管理
        resetSection: "重置",
        resetBtn: "恢复默认",
        resetDefaultsBtn: "恢复默认设置",
        resetDefaultsDesc: "将所有选项还原为初始预设值，原弹幕文件不会删除。",
        aboutSection: "配置与关于",
        iniHint: "配置使用带中文说明的 settings.ini。手动编辑前请退出程序；旧配置不导入。",
        aboutDescFormat: "DanmaX %1 · C++20 / Qt 6.11\n便携版的配置和日志保存在程序旁；开发运行使用指定数据目录。",
        resetDialogTitle: "恢复默认设置？",
        resetDialogContent: "将覆盖当前设置，原弹幕文件不会删除。"
    })

    // 覆盖层 HUD 监控指标
    readonly property var overlay: ({
        activeDanmakuFormat: "在屏 %1 / 丢弃 %2",
        frequencyFormat: "呈现 %1 Hz / 更新 %2 Hz",
        intervalFormat: "更新间隔 P95 %1 ms",
        memoryFormat: "内存 %1 MiB",
        textureBudgetFormat: "图片 %1 / 预算 %2 MiB",
        fallbackFormat: "回退 %1 / 容量 %2"
    })

    // 关于页
    readonly property var about: ({
        title: "关于",
        appDescription: "高性能桌面弹幕覆盖层与媒体同步器",
        versionFormat: "版本 %1",
        feedbackSection: "反馈",
        reportBugTitle: "报告错误",
        reportBugDesc: "向开发者提交程序缺陷与运行异常",
        suggestFeatureTitle: "建议功能",
        suggestFeatureDesc: "提出新功能设想或交互改进建议",
        discussionsTitle: "讨论区",
        discussionsDesc: "与其他用户交流使用心得与弹幕配置",
        otherLinksSection: "其他链接",
        githubRepo: "GitHub 仓库",
        faq: "FAQ",
        contributionGuide: "贡献指南",
        license: "许可协议",
        openLinkAccessible: "打开链接 %1"
    })
}
