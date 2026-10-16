import QtQuick
import QtQuick.Controls.FluentWinUI3
import QtQuick.Layouts
import QtQuick.Dialogs
import "../theme"

Dialog {
    id: dialog
    objectName: "matchingRecoveryDialog"
    required property QtObject backend
    property QtObject workflow: backend
    readonly property QtObject automatic: backend.automatic
    readonly property QtObject recovery: automatic.recovery
    property bool manualSearch: false
    property bool allowLocalXml: false
    property bool manualBoundToPlayer: false
    readonly property QtObject activeLibrary: manualSearch ? backend.library : automatic.library
    signal openSettings()
    signal manualSourceLoaded()
    function cancelMatching() {
        if (manualSearch) { backend.library.cancel(); close() }
        else automatic.cancelRecovery()
    }
    parent: Overlay.overlay
    x: parent ? Math.max(8, (parent.width - width) / 2) : 0
    y: parent ? Math.max(8, (parent.height - height) / 2) : 0
    width: parent ? Math.min(parent.width - 24, Math.round(720 * Ui.textScale)) : 720
    height: parent ? Math.min(parent.height - 24, Math.round(640 * Ui.textScale)) : 640
    modal: true
    focus: true
    title: manualSearch ? "搜索弹幕" : "手动匹配弹幕"
    closePolicy: Popup.CloseOnEscape
    leftPadding: Ui.scrollInset(verticalBar.implicitWidth, padding)
    rightPadding: leftPadding
    topPadding: leftPadding
    bottomPadding: leftPadding
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
        function onShowRequested() { dialog.manualSearch = false; dialog.open() }
        function onChanged() { if (!dialog.manualSearch && !dialog.recovery.active) dialog.close() }
    }
    Connections {
        target: dialog.backend
        function onRequestOnlineDanmakuSearch() {
            dialog.manualSearch = true
            dialog.manualBoundToPlayer = !dialog.workflow.manualMode && dialog.workflow.targetSessionConnected
            dialog.open()
        }
        function onStateChanged() {
            if (dialog.visible && dialog.manualSearch && dialog.manualBoundToPlayer && !dialog.workflow.targetSessionConnected)
                dialog.cancelMatching()
        }
    }
    Connections {
        target: dialog.backend.library
        function onSourceReady() {
            if (dialog.manualSearch && dialog.visible) {
                dialog.close()
                dialog.manualSourceLoaded()
            }
        }
    }
    Component.onCompleted: if (recovery.active) open()
    Binding { target: dialog.enter; property: "enabled"; value: Ui.motionEnabled }
    Binding { target: dialog.exit; property: "enabled"; value: Ui.motionEnabled }
    contentItem: ScrollView {
        id: scroll
        objectName: "recoveryScroll"
        clip: true
        contentWidth: availableWidth
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        ScrollBar.vertical: AppScrollBar {
            id: verticalBar
            objectName: "recoveryVerticalScrollBar"
            viewport: scroll
            scrollTarget: scroll.contentItem
            wheelRegion: scroll.parent
            wheelBottom: dialog.footer.y
            Accessible.name: "手动匹配内容滚动条"
        }
        ColumnLayout {
            width: scroll.availableWidth
            spacing: 12
            Label {
                Layout.fillWidth: true
                text: dialog.recovery.reason
                visible: !dialog.manualSearch
                wrapMode: Text.WordWrap
                Accessible.role: Accessible.AlertMessage
            }
            Label {
                Layout.fillWidth: true
                text: dialog.recovery.pauseStatus
                visible: !dialog.manualSearch
                wrapMode: Text.WordWrap
                color: Ui.secondaryText
            }
            Label { text: "当前播放文件名／媒体标题"; font.weight: Font.DemiBold; visible: !dialog.manualSearch }
            TextArea {
                objectName: "recoveryFileName"
                visible: !dialog.manualSearch
                Layout.fillWidth: true
                text: dialog.recovery.title
                readOnly: true
                selectByMouse: true
                wrapMode: TextEdit.WrapAnywhere
                Accessible.name: "当前播放文件名，可选择并复制"
            }
            Button {
                visible: dialog.allowLocalXml
                objectName: "chooseRecoveryXml"
                text: "选择本地 XML"
                onClicked: xmlDialog.open()
            }
            Button {
                objectName: "copyRecoveryFileName"
                visible: !dialog.manualSearch
                text: "复制完整文件名"
                onClicked: dialog.backend.copyRecoveryTitle()
            }
            Label {
                Layout.fillWidth: true
                text: dialog.manualSearch ? "搜索并确认动画与剧集后载入弹幕，可在播放页选择播放方式。"
                    : "输入番名搜索，确认动画与剧集后载入弹幕。仅当本次操作自动暂停了播放器，完成或取消时才恢复播放。取消后下一集仍会自动识别。"
                color: Ui.secondaryText
                wrapMode: Text.WordWrap
            }
            OnlineDanmakuPane {
                Layout.fillWidth: true
                library: dialog.activeLibrary
                loadActionText: "确认并载入弹幕"
                onCancelled: dialog.cancelMatching()
                onOpenSettings: {
                    dialog.cancelMatching()
                    dialog.openSettings()
                }
            }
        }
    }
    FileDialog {
        id: xmlDialog
        title: "为当前视频选择弹幕 XML"
        nameFilters: ["弹幕 XML (*.xml)"]
        onAccepted: { dialog.manualSearch = false; dialog.backend.loadFile(selectedFile) }
    }
}
