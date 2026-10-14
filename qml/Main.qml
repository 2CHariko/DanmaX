import QtQuick
import QtQuick.Controls.FluentWinUI3
import QtQuick.Layouts
import QWindowKit 1.0
import "components"
import "pages"
import "theme"
import "i18n"

ApplicationWindow {
    id: root
    required property QtObject backend
    property QtObject backdrop: null
    Binding { target: Ui; property: "micaActive"; value: root.backdrop ? root.backdrop.effectiveMica : false }
    property int selectedPage: 0
    property int sourceTab: 0
    readonly property string currentPageName: ["home", "player", "logs", "settings", "about"][selectedPage] || ""
    readonly property bool compact: width < Ui.expandedNavigationWidth
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
        function onRequestShowMainWindow() {
            root.visible = true
            if (root.visibility === Window.Minimized) root.showNormal()
            root.raise()
            root.requestActivate()
        }
        function onRequestNavigateToPage(page, tab) {
            root.sourceTab = tab
            if (page === 1) playerPage.selectSourceTab(tab)
            root.navigate(page)
        }
    }
    // Keep the swapchain alpha-capable across live material preference changes.
    color: "transparent"
    background: Rectangle {
        color: Ui.windowSurface

        Rectangle {
            anchors.left: parent.left
            anchors.leftMargin: (root.compact ? 48 : 200) + 1
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            color: Ui.contentAreaSurface
        }
    }
    function navigate(page) { selectedPage = page; pages.children[page].focusHeading() }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // 1. 常驻侧边栏：宽屏展开，窄屏自动缩小为图标模式（最小宽度下始终显示图标）
        Item {
            Layout.preferredWidth: root.compact ? 48 : 200
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
                Layout.fillWidth: true
                // Keep every page heading aligned with the homepage search layout.
                Layout.preferredHeight: Math.max(44, homeSearch.height + 2 * Ui.titleBarVerticalPadding)

                Item {
                    id: searchHost
                    anchors.left: parent.left
                    anchors.leftMargin: root.compact ? 16 : Ui.pagePadding
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
                    anchors.right: parent.right
                    anchors.top: parent.top
                }
            }

            // 内容与错误横幅容器
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.leftMargin: root.compact ? 16 : Ui.pagePadding
                Layout.rightMargin: 0
                Layout.bottomMargin: 0
                Layout.topMargin: Ui.pageHeaderGap
                spacing: 12

            RowLayout {
                visible: root.backend.error.length > 0 || root.backend.settings.error.length > 0
                Layout.fillWidth: true
                Layout.rightMargin: root.compact ? 16 : Ui.pagePadding
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: root.backend.error.length > 0 ? root.backend.error : root.backend.settings.error
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
                        sourceTab: root.sourceTab
                        onOpenSettings: {
                            root.navigate(3)
                            Qt.callLater(settingsPage.focusOnlineSettings)
                        }
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
                }
            }
            }
        }
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
        root.syncTheme()
        root.backend.attach(overlay.renderer, overlay, toolbarWindow, root)
    }
    onClosing: root.backend.stop()
}
