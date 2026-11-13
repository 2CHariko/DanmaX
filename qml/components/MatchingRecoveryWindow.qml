import QtQuick
import QtQuick.Controls.FluentWinUI3
import "../theme"

ApplicationWindow {
    id: dialog
    objectName: "mediaMatchingRecoveryWindow"
    required property QtObject backend
    readonly property QtObject media: backend.media
    readonly property QtObject recovery: media.automatic.recovery
    property double task: -1
    property bool initialFocus: false
    signal openSettings()

    transientParent: null
    // A Windows tool window can take focus on demand without activating on show.
    flags: Qt.Tool | Qt.WindowTitleHint | Qt.WindowSystemMenuHint | Qt.WindowCloseButtonHint
    title: "手动匹配弹幕"
    color: Ui.colors.window
    minimumWidth: Math.min(300, Math.max(1, screen ? screen.width - 32 : 300))
    minimumHeight: Math.min(260, Math.max(1, screen ? screen.height - 48 : 260))
    visible: false

    function present() {
        if (backend.embeddedVideo || !media.active || !recovery.active) return
        if (visible && task === media.taskId) return
        task = media.taskId
        initialFocus = true
        backend.showMatchingWindow(dialog, Math.round(720 * Ui.textScale), Math.round(640 * Ui.textScale))
    }
    function cancelMatching() {
        if (task === media.taskId && recovery.active) media.cancelMatching()
        hide()
    }
    onClosing: function(event) { event.accepted = false; cancelMatching() }
    onActiveChanged: {
        if (active && visible && initialFocus) {
            initialFocus = false
            if (!pane.focusSearch()) cancelButton.forceActiveFocus()
        }
    }
    Connections {
        target: dialog.recovery
        function onChanged() { if (!dialog.recovery.active) dialog.hide() }
    }
    Connections {
        target: dialog.media
        function onStateChanged() {
            if (dialog.visible && (!dialog.media.active || dialog.media.taskId !== dialog.task)) dialog.hide()
        }
    }

    Shortcut {
        sequence: "Escape"
        enabled: dialog.visible && !pane.choosingXml
        onActivated: if (!pane.closeChoicePopup()) dialog.cancelMatching()
    }
    MatchingRecoveryPane {
        id: pane
        anchors.fill: parent
        backend: dialog.media
        allowLocalXml: true
        presented: dialog.visible && dialog.task === dialog.media.taskId
        onCancelMatching: dialog.cancelMatching()
        onOpenSettings: dialog.openSettings()
    }
    footer: DialogButtonBox {
        alignment: Qt.AlignRight
        Button {
            id: cancelButton
            objectName: "cancelMatchingRecovery"
            text: "取消本次匹配"
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
        }
        onRejected: dialog.cancelMatching()
    }
}
