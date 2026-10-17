import QtQuick
import QtQuick.Controls.FluentWinUI3
import QtQuick.Layouts
import "../theme"

Dialog {
    id: dialog
    objectName: "cachedDanmakuDialog"
    required property QtObject player
    property QtObject media: null
    property double task: -1
    title: "选择缓存弹幕"
    parent: Overlay.overlay
    x: parent ? Math.max(8, (parent.width - width) / 2) : 0
    y: parent ? Math.max(8, (parent.height - height) / 2) : 0
    width: parent ? Math.min(parent.width - 24, 720 * Ui.textScale) : 720
    height: parent ? Math.min(parent.height - 24, 560 * Ui.textScale) : 560
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape
    function choose() {
        const target = player.sourceSession
        if (!target || !target.canSelectSource) return
        media = target
        task = media.taskId
        media.library.refreshCache()
        open()
    }
    leftPadding: Ui.scrollInset(verticalBar.implicitWidth, padding)
    rightPadding: leftPadding
    topPadding: leftPadding
    bottomPadding: leftPadding
    footer: DialogButtonBox {
        alignment: Qt.AlignRight
        Button {
            objectName: "closeCachedDanmaku"
            text: "关闭"
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
        }
    }
    onClosed: if (media && media.taskId === task) media.library.cancel()
    Connections {
        target: dialog.media
        function onStateChanged() { if (dialog.visible && (!dialog.media || !dialog.media.active || dialog.media.taskId !== dialog.task)) dialog.close() }
        function onSourceCommitted() { if (dialog.visible && dialog.media.taskId === dialog.task) dialog.close() }
    }
    contentItem: ScrollView {
        id: scroll
        objectName: "cachedDialogScroll"
        clip: true
        contentWidth: availableWidth
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        ScrollBar.vertical: AppScrollBar {
            id: verticalBar
            objectName: "cachedDialogVerticalScrollBar"
            viewport: scroll
            scrollTarget: scroll.contentItem
            wheelRegion: scroll.parent
            wheelBottom: dialog.footer.y
            Accessible.name: "缓存弹幕选择内容滚动条"
        }
        CachedDanmakuPane {
            width: scroll.availableWidth
            library: dialog.media ? dialog.media.library : dialog.player.sourceSession.library
            selectionAllowed: dialog.media && dialog.media.canSelectSource && dialog.media.taskId === dialog.task
            onLoadRequested: key => dialog.media.loadCached(key)
        }
    }
}
