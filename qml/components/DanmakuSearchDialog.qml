import QtQuick
import QtQuick.Controls.FluentWinUI3
import QtQuick.Layouts
import "../theme"

Dialog {
    id: dialog
    objectName: "standaloneDanmakuSearchDialog"
    required property QtObject backend
    property double task: -1
    signal openSettings()
    title: "为独立弹幕选择来源"
    parent: Overlay.overlay
    x: parent ? Math.max(8, (parent.width - width) / 2) : 0
    y: parent ? Math.max(8, (parent.height - height) / 2) : 0
    width: parent ? Math.min(parent.width - 24, 720 * Ui.textScale) : 720
    height: parent ? Math.min(parent.height - 24, 640 * Ui.textScale) : 640
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape
    leftPadding: Ui.scrollInset(verticalBar.implicitWidth, padding)
    rightPadding: leftPadding
    topPadding: leftPadding
    bottomPadding: leftPadding
    function cancel() { if (backend.taskId === task) backend.library.cancel(); close() }
    onRejected: cancel()
    onOpened: if (!searchPane.focusSearch()) cancelButton.forceActiveFocus()
    footer: DialogButtonBox {
        alignment: Qt.AlignRight
        Button { id: cancelButton; objectName: "cancelStandaloneSearch"; text: "取消"; DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
    }
    Connections {
        target: dialog.backend
        function onRequestOnlineDanmakuSearch() {
            if (!dialog.backend.active) return
            dialog.task = dialog.backend.taskId
            dialog.open()
        }
        function onStateChanged() { if (dialog.visible && (!dialog.backend.active || dialog.backend.taskId !== dialog.task)) dialog.close() }
        function onSourceCommitted() { if (dialog.visible) dialog.close() }
    }
    contentItem: ScrollView {
        id: scroll
        objectName: "standaloneSearchScroll"
        clip: true
        contentWidth: availableWidth
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        ScrollBar.vertical: AppScrollBar {
            id: verticalBar
            objectName: "standaloneSearchVerticalScrollBar"
            viewport: scroll
            scrollTarget: scroll.contentItem
            wheelRegion: scroll.parent
            wheelBottom: dialog.footer.y
            Accessible.name: "独立弹幕搜索内容滚动条"
        }
        OnlineDanmakuPane {
            id: searchPane
            width: scroll.availableWidth
            library: dialog.backend.library
            loadActionText: "载入独立弹幕"
            onCancelled: dialog.cancel()
            onOpenSettings: { dialog.cancel(); dialog.openSettings() }
        }
    }
}
