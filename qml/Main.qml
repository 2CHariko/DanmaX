import QtQuick
import QtQuick.Controls.FluentWinUI3
import QtQuick.Layouts
import "components"
import "pages"
import "theme"
ApplicationWindow {
    id: root
    required property QtObject backend
    property int selectedPage: 0
    property bool navigationOpen: false
    readonly property bool minimal: width <= Ui.minimalNavigationWidth
    readonly property bool compact: width < Ui.expandedNavigationWidth
    visible: false
    width: 1080; height: 760
    minimumWidth: 520; minimumHeight: 480
    title: "Local Danmaku"
    function navigate(page) { selectedPage=page; navigationOpen=false; pages.children[page].forceActiveFocus() }
    ColumnLayout {
        anchors.fill:parent
        anchors.margins:root.minimal?12:24
        spacing:12
        ToolButton { id:menuButton;visible:root.minimal;text:"导航";Accessible.name:"展开导航";checkable:true;checked:root.navigationOpen;Accessible.description:root.navigationOpen?"导航已展开":"导航已收起";onToggled:root.navigationOpen=checked }
        RowLayout {
            visible: root.backend.error.length>0 || root.backend.settings.error.length>0
            Layout.fillWidth:true
            Label { Layout.fillWidth:true;wrapMode:Text.WordWrap;text:root.backend.error.length>0?root.backend.error:root.backend.settings.error;Accessible.name:"错误："+text }
            Button { text:root.backend.settings.error.length>0?"重试保存":"关闭";onClicked:root.backend.settings.error.length>0?root.backend.settings.retrySave():root.backend.clearError() }
        }
        RowLayout {
            Layout.fillWidth:true;Layout.fillHeight:true;spacing:24
            NavigationPane {
                visible:!root.minimal||root.navigationOpen
                Layout.preferredWidth:root.compact?72:176
                Layout.minimumWidth:Layout.preferredWidth
                Layout.maximumWidth:Layout.preferredWidth
                Layout.fillHeight:true
                compact:root.compact;selected:root.selectedPage;fluentIcons:root.backend.hasFluentIcons
                onNavigate:page=>root.navigate(page)
            }
            StackLayout {
                id:pages
                Layout.fillWidth:true;Layout.fillHeight:true
                currentIndex:root.selectedPage
                PlayerPage { backend:root.backend }
                LogsPage { backend:root.backend }
                SettingsPage { backend:root.backend }
            }
        }
    }
    Shortcut { sequence:"Escape";enabled:root.navigationOpen;onActivated:{root.navigationOpen=false;menuButton.forceActiveFocus()} }
    OverlayWindow { id:overlay;backend:root.backend;visible:root.backend.overlayVisible }
    Component.onCompleted: root.backend.attach(overlay.renderer,overlay)
    onClosing:root.backend.stop()
}
