import QtQuick
import QtQuick.Controls.FluentWinUI3
import "../theme"

Dialog {
    popupType: Ui.embeddedVideoActive ? Popup.Window : Popup.Item
    id: dialog
    objectName: "matchingRecoveryDialog"
    required property QtObject backend
    readonly property QtObject automatic: backend.automatic
    readonly property QtObject recovery: automatic.recovery
    property bool manualSearch: false
    property bool allowLocalXml: false
    property bool manualBoundToPlayer: false
    property double task: -1
    signal openSettings()
    signal manualSourceLoaded()
    function cancelMatching() {
        if (backend.taskId !== task) { close(); return }
        if (manualSearch) { backend.cancelOnlineSearch(); close() }
        else backend.cancelMatching()
    }
    parent: Overlay.overlay
    x: parent ? Math.max(8, (parent.width - width) / 2) : 0
    y: parent ? Math.max(8, (parent.height - height) / 2) : 0
    width: parent ? Math.min(parent.width - 24, Math.round(700 * Ui.textScale)) : 700
    height: parent ? Math.min(parent.height - 24, Math.round((manualSearch ? 420 : 600) * Ui.textScale)) : (manualSearch ? 420 : 600)
    modal: true
    focus: true
    title: manualSearch ? "搜索弹幕" : "手动匹配弹幕"
    closePolicy: recoveryPane.choosingXml ? Popup.NoAutoClose : Popup.CloseOnEscape
    leftPadding: 0
    rightPadding: 0
    topPadding: 0
    bottomPadding: 0
    onRejected: cancelMatching()
    footer: DialogButtonBox {
        alignment: Qt.AlignRight
        Button {
            objectName: "cancelMatchingRecovery"
            text: "取消本次匹配"
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
        }
    }
    Connections {
        target: dialog.recovery
        function onShowRequested() {
            if (!Ui.embeddedVideoActive) {
                if (dialog.manualSearch) dialog.close()
                return
            }
            dialog.task = dialog.backend.taskId
            dialog.manualSearch = false
            dialog.open()
        }
        function onChanged() { if (!dialog.manualSearch && !dialog.recovery.active) dialog.close() }
    }
    Connections {
        target: dialog.backend
        function onRequestOnlineDanmakuSearch() {
            dialog.manualSearch = true
            dialog.task = dialog.backend.taskId
            dialog.manualBoundToPlayer = dialog.backend.targetSessionConnected
            dialog.open()
        }
        function onStateChanged() {
            if (dialog.visible && (!dialog.backend.active || dialog.backend.taskId !== dialog.task)) { dialog.close(); return }
            if (dialog.visible && dialog.manualSearch && dialog.manualBoundToPlayer && !dialog.backend.targetSessionConnected)
                dialog.cancelMatching()
        }
    }
    Connections {
        target: dialog.backend
        function onSourceCommitted() {
            if (dialog.manualSearch && dialog.visible) {
                dialog.close()
                dialog.manualSourceLoaded()
            }
        }
    }
    Component.onCompleted: if (Ui.embeddedVideoActive && recovery.active) { task = backend.taskId; open() }
    Binding { target: dialog.enter; property: "enabled"; value: Ui.motionEnabled }
    Binding { target: dialog.exit; property: "enabled"; value: Ui.motionEnabled }
    contentItem: MatchingRecoveryPane {
        id: recoveryPane
        backend: dialog.backend
        manualSearch: dialog.manualSearch
        allowLocalXml: dialog.allowLocalXml
        presented: dialog.visible
        minimumInset: dialog.padding
        onCancelMatching: dialog.cancelMatching()
        onOpenSettings: dialog.openSettings()
    }
}
