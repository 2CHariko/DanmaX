import QtQuick
import QtQuick.Controls.FluentWinUI3
import QtQuick.Layouts
import QtQuick.Dialogs
import QWindowKit 1.0
import "components"
import "pages"
import "theme"
import "i18n"

ApplicationWindow {
    id: root
    required property QtObject backend
    property QtObject backdrop: null
    Binding { target: Ui; property: "embeddedVideoActive"; value: root.backend.embeddedVideo }
    Binding { target: root.backdrop; property: "compatibilityOpaque"; value: root.backend.videoViewport.active; when: root.backdrop !== null }
    Binding { target: Ui; property: "micaActive"; value: root.backdrop ? root.backdrop.effectiveMica : false }
    Binding { target: Ui; property: "systemAnimationsEnabled"; value: root.backend.uiPreferences.animationsEnabled }
    property int selectedPage: 0
    property int lastBrowsePage: 0
    readonly property QtObject presentation: backend.presentation
    readonly property bool watching: presentation.watching
    onSelectedPageChanged: if (selectedPage !== 1) lastBrowsePage = selectedPage
    Binding { target: root.presentation; property: "watching"; value: root.backend.embeddedVideo && root.selectedPage === 1 }
    Binding { target: root.presentation; property: "playing"; value: root.backend.nowPlaying.playing }
    Binding { target: root.presentation; property: "blocked"; value: root.backend.nowPlaying.busy || root.backend.nowPlaying.completed || root.backend.nowPlaying.seekPending || root.backend.nowPlaying.error.length > 0 || root.backend.videoViewport.releaseStatus.length > 0 || nowPlayingBar.interacting }
    Connections {
        target: root.presentation
        function onAboutToChangeHost() { nowPlayingBar.prepareHostChange() }
        function onKeyboardControlsRequested() { fullscreenControls.requestActivate(); nowPlayingBar.focusTransport() }
        function onPointerInteraction() { nowPlayingBar.keyboardFocus = false }
        function onKeyboardInteraction() { nowPlayingBar.keyboardFocus = true }
    }
    readonly property string currentPageName: ["home", "player", "logs", "settings", "about", "media"][selectedPage] || ""
    readonly property bool compact: width < Ui.expandedNavigationWidth
    readonly property real pageContentInset: pages.children[selectedPage].contentInset
    visible: false
    width: 1080; height: 760
    readonly property QtObject sizingScreen: backend.screenForName(screen ? screen.name : "")
    readonly property rect sizingWorkArea: sizingScreen ? sizingScreen.availableGeometry : Qt.rect(0, 0, 832, 632)
    minimumWidth: Math.min(800, Math.max(1, sizingWorkArea.width - 32))
    minimumHeight: Math.min(600, Math.max(1, sizingWorkArea.height - 32))
    title: I18n.nav.appTitle
    WindowAgent { id: windowAgent }
    function syncTheme() {
        windowAgent.setWindowAttribute("dark-mode", Ui.dark)
        windowAgent.setWindowAttribute("mica", Ui.micaActive)
    }
    Connections {
        target: Ui
        function onDarkChanged() { root.syncTheme() }
        function onMicaActiveChanged() { root.syncTheme() }
    }
    Connections {
        target: root.backend
        function onCloseReady() { root.close() }
        function onRequestShowMainWindow() {
            root.visible = true
            if (root.visibility === Window.Minimized) root.showNormal()
            root.raise()
            root.requestActivate()
        }
        function onRequestNavigateToPage(page, tab) {
            root.navigate(page)
        }
    }
    // Keep the swapchain alpha-capable across live material preference changes.
    color: root.backdrop && root.backdrop.compatibilityOpaque ? Ui.windowSurface : "transparent"
    background: Rectangle {
        color: root.watching ? Ui.playbackSurface : Ui.windowSurface
    }
    function navigate(page) { selectedPage = page; pages.children[page].focusHeading() }

    Item {
        id: mainScene
        objectName: "mainScene"
        anchors.fill: parent
        Rectangle {
            anchors.fill: workspace
            visible: !root.watching
            anchors.leftMargin: (root.compact ? Ui.navigationCompactWidth : Ui.navigationExpandedWidth) + 1
            color: Ui.contentAreaSurface
        }
    RowLayout {
        id: workspace
        objectName: "mainWorkspace"
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.bottomMargin: root.presentation.fullscreen ? 0 : nowPlayingBar.height
        spacing: 0

        // 1. 常驻侧边栏：宽屏展开，窄屏自动缩小为图标模式（最小宽度下始终显示图标）
        Item {
            objectName: "mainNavigation"
            visible: !root.watching
            Layout.preferredWidth: root.compact ? Ui.navigationCompactWidth : Ui.navigationExpandedWidth
            Layout.minimumWidth: Layout.preferredWidth
            Layout.maximumWidth: Layout.preferredWidth
            Layout.fillHeight: true

            NavigationPane {
                id: navPane
                anchors.fill: parent
                anchors.topMargin: 0
                anchors.bottomMargin: 12
                anchors.leftMargin: root.compact ? 4 : 8
                anchors.rightMargin: root.compact ? 4 : 8
                compact: root.compact
                selected: root.selectedPage
                showBrandHeader: true
                onStartSystemMove: root.startSystemMove()
                onToggleMaximized: {
                    if (root.visibility === Window.Maximized) root.showNormal()
                    else root.showMaximized()
                }
                onNavigate: page => root.navigate(page)
            }
        }

        // 2. 左侧导航与右侧内容的分隔线
        Rectangle {
            visible: !root.watching
            Layout.fillHeight: true
            Layout.preferredWidth: 1
            color: Ui.cardBorder
        }

        // 3. 右侧主内容区域
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // 顶部沉浸式标题栏（拖拽区域与控制按钮）
            Item {
                id: titleBar
                objectName: "mainTitleBar"
                visible: !root.presentation.fullscreen
                Rectangle { anchors.fill: parent; visible: root.watching; color: Ui.playbackSurface }
                ToolButton {
                    id: watchingBack
                    objectName: "watchingBack"
                    visible: root.watching
                    anchors.left: parent.left; anchors.leftMargin: 8; anchors.verticalCenter: parent.verticalCenter
                    text: "返回"; icon.source: Ui.icon("chevron-left", true)
                    palette.buttonText: Ui.playbackText; palette.button: Ui.playbackSurface
                    Accessible.name: "返回浏览页面"
                    onClicked: root.navigate(root.lastBrowsePage)
                }
                Layout.fillWidth: true
                // Keep every page heading aligned with the homepage search layout.
                Layout.preferredHeight: Math.max(44, homeSearch.height + 2 * Ui.titleBarVerticalPadding)
                Layout.minimumHeight: Layout.preferredHeight

                Item {
                    id: searchHost
                    anchors.left: parent.left
                    anchors.leftMargin: root.pageContentInset
                    anchors.right: captionButtons.left
                    anchors.rightMargin: 48
                    height: parent.height
                    AnimeSearchField {
                        id: homeSearch
                        metadata: root.backend.metadata
                        visible: root.selectedPage === 0
                        anchors.centerIn: parent
                        width: Math.max(0, Math.min(440, parent.width))
                        onFirstResultRequested: homePage.focusFirstSearchResult()
                        onSubmitted: homePage.contentItem.contentY = 0
                    }
                }

                WindowCaptionButtons {
                    id: captionButtons
                    objectName: "mainCaptionButtons"
                    watchingTheme: root.watching
                    anchors.right: parent.right
                    anchors.top: parent.top
                }
            }

            // 内容与错误横幅容器
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.leftMargin: 0
                Layout.rightMargin: 0
                Layout.bottomMargin: 0
                Layout.topMargin: root.watching ? 0 : Ui.pageHeaderGap
                spacing: 12

            RowLayout {
                visible: (root.backend.error.length > 0 && root.backend.error !== root.backend.media.error)
                    || root.backend.settings.error.length > 0
                Layout.fillWidth: true
                Layout.leftMargin: root.pageContentInset
                Layout.rightMargin: root.pageContentInset
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: root.watching ? Ui.playbackText : Ui.textColor
                    textFormat: Text.PlainText
                    text: root.backend.error.length > 0 && root.backend.error !== root.backend.media.error
                        ? root.backend.error : root.backend.settings.error
                    Accessible.name: I18n.common.errorPrefix + text
                }
                Button {
                    text: root.backend.settings.error.length > 0 ? I18n.common.retrySave : I18n.common.close
                    onClicked: root.backend.settings.error.length > 0 ? root.backend.settings.retrySave() : root.backend.clearError()
                }
            }

            // 主页面堆栈
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                StackLayout {
                    id: pages
                    anchors.fill: parent
                    currentIndex: root.selectedPage

                    HomePage {
                        id: homePage
                        objectName: "homePage"
                        backend: root.backend
                        searchField: homeSearch
                        onOpenDanmakuForAnime: (title, episodeTitle, episodeNumber) => {
                            root.backend.showOnlineDanmakuForAnime(title, episodeTitle, episodeNumber)
                        }
                    }
                    PlayerPage {
                        id: playerPage
                        objectName: "playerPage"
                        backend: root.backend
                        onOpenVideo: playbackVideoPicker.open()
                    }
                    LogsPage {
                        backend: root.backend
                    }
                    SettingsPage {
                        id: settingsPage
                        backdrop: root.backdrop
                        objectName: "settingsPage"
                        backend: root.backend
                    }
                    AboutPage {
                        id: aboutPage
                        objectName: "aboutPage"
                    }
                    MediaLibraryPage {
                        objectName: "mediaLibraryPage"
                        backend: root.backend
                    }
                }
            }
            }
        }
    }
        NowPlayingBar {
            id: nowPlayingBar
            parent: root.presentation.fullscreen ? fullscreenControls.contentItem : mainScene
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            backend: root.backend
            onOpenVideo: playbackVideoPicker.open()
            onOpenStandalone: { standaloneXmlPicker.context = root.backend.nowPlaying.contextId; standaloneXmlPicker.open() }
            onReplaceXml: {
                const target = root.backend.nowPlaying.sourceSession
                if (target.canSelectSource) { playbackXmlPicker.workflow = target; playbackXmlPicker.task = target.taskId; playbackXmlPicker.open() }
            }
            onShowSources: cachedDanmakuDialog.choose()
            onShowSettings: root.navigate(3)
            onShowPlaying: root.navigate(1)
        }
    }

    Window {
        id: fullscreenControls
        objectName: "fullscreenControlsWindow"
        transientParent: root
        flags: Qt.Tool | Qt.FramelessWindowHint
        color: Ui.playbackSurface
        width: root.width
        height: nowPlayingBar.implicitHeight
        visible: false
    }

    VideoDropOverlay {
        id: dropFeedback
        anchors.fill: parent
        z: 10
        backend: root.backend
        backgroundSource: mainScene
        paintFeedback: !root.backend.embeddedVideo
        // This is a sibling of the captured UI, outside the popup overlay.
        enabled: root.visible && root.visibility !== Window.Minimized
            && !root.Overlay.overlay.children.some(item => item.visible)
            && !missingPlayerDialog.visible
    }

    Window {
        id: embeddedDropWindow
        transientParent: root
        flags: Qt.FramelessWindowHint | Qt.Tool | Qt.WindowTransparentForInput | Qt.WindowDoesNotAcceptFocus
        color: "transparent"
        x: root.x; y: root.y; width: root.width; height: root.height
        visible: root.backend.embeddedVideo && root.visible && root.visibility !== Window.Minimized
            && (dropFeedback.dragActive || root.backend.videoViewport.dragging)
        VideoDropOverlay {
            anchors.fill: parent
            backend: root.backend
            backgroundSource: mainScene
            opaqueFeedback: true
            presentationOnly: true
            engaged: dropFeedback.dragActive || root.backend.videoViewport.dragging
            fileName: root.backend.videoViewport.dragging ? root.backend.videoViewport.dragName : dropFeedback.fileName
        }
    }

    MatchingRecoveryDialog {
        objectName: "mediaMatchingRecoveryDialog"
        allowLocalXml: true
        backend: root.backend.media
        onOpenSettings: root.navigate(3)
        onManualSourceLoaded: root.navigate(1)
    }
    DanmakuSearchDialog { backend: root.backend.standalone; onOpenSettings: root.navigate(3) }
    CachedDanmakuDialog { id: cachedDanmakuDialog; player: root.backend.nowPlaying }
    FileDialog {
        id: standaloneXmlPicker
        objectName: "standaloneXmlPicker"
        property double context: -1
        title: "独立播放弹幕 XML"
        fileMode: FileDialog.OpenFile
        nameFilters: ["弹幕 XML (*.xml)"]
        onAccepted: if (context === root.backend.nowPlaying.contextId) root.backend.nowPlaying.openStandalone(selectedFile)
    }
    FileDialog {
        id: playbackVideoPicker
        title: "打开视频"
        nameFilters: ["视频 (*.mp4 *.mkv *.avi *.mov *.wmv *.flv *.webm *.m4v *.ts *.m2ts)"]
        onAccepted: root.backend.media.openVideo(selectedFile)
    }
    FileDialog {
        id: playbackXmlPicker
        property double task: -1
        property QtObject workflow: null
        title: root.backend.nowPlaying.count > 0 ? "更换弹幕 XML" : "加载弹幕 XML"
        nameFilters: ["弹幕 XML (*.xml)"]
        onAccepted: {
            if (workflow && workflow === root.backend.nowPlaying.sourceSession && task === workflow.taskId && workflow.canSelectSource)
                root.backend.nowPlaying.replaceDanmaku(selectedFile)
            root.navigate(1)
        }
    }
    FileDialog {
        id: missingPlayerDialog
        objectName: "missingPlayerDialog"
        title: "选择 " + root.backend.videoOpen.playerName + "，随后继续打开视频"
        fileMode: FileDialog.OpenFile
        nameFilters: [root.backend.videoOpen.executableFilter]
        onAccepted: root.backend.videoOpen.choosePlayer(selectedFile)
        onRejected: root.backend.videoOpen.cancel()
    }
    Connections {
        target: root.backend.videoOpen
        function onPlayerRequired() { missingPlayerDialog.open() }
    }
    OverlayWindow { id: overlay; backend: root.backend; visible: root.backend.overlayVisible }
    ToolbarWindow { id: toolbarWindow; backend: root.backend; overlayWindow: overlay }
    Component.onCompleted: {
        windowAgent.setup(root)
        windowAgent.setTitleBar(titleBar)
        windowAgent.setSystemButton(WindowAgent.Minimize, captionButtons.minBtn)
        windowAgent.setSystemButton(WindowAgent.Maximize, captionButtons.maxBtn)
        windowAgent.setSystemButton(WindowAgent.Close, captionButtons.closeBtn)
        windowAgent.setHitTestVisible(navPane, true)
        windowAgent.setHitTestVisible(homeSearch, true)
        windowAgent.setHitTestVisible(watchingBack, true)
        root.syncTheme()
        root.backend.attach(overlay.renderer, overlay, toolbarWindow, root)
        root.presentation.attach(root, fullscreenControls, overlay)
    }
    onClosing: function(event) { if (!root.backend.requestClose()) event.accepted = false }
}
