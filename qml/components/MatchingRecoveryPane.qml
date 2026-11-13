import QtQuick
import QtQuick.Controls.FluentWinUI3
import QtQuick.Layouts
import QtQuick.Dialogs
import "../theme"

Item {
    id: pane
    required property QtObject backend
    readonly property QtObject recovery: backend.automatic.recovery
    property bool manualSearch: false
    property bool allowLocalXml: false
    property bool presented: false
    property real minimumInset: 16
    readonly property QtObject activeLibrary: manualSearch ? backend.library : backend.automatic.library
    readonly property real inset: Ui.scrollInset(verticalBar.implicitWidth, minimumInset)
    readonly property bool choosingXml: xmlDialog.visible
    signal cancelMatching()
    signal openSettings()

    onPresentedChanged: {
        if (!presented) {
            closeChoicePopup()
            xmlDialog.task = -1
            xmlDialog.close()
        } else {
            scroll.contentItem.contentY = scroll.contentItem.originY
        }
    }
    function focusSearch() { return online.focusSearch() }
    function closeChoicePopup() { return online.closeChoicePopup() }

    ScrollView {
        id: scroll
        objectName: "recoveryScroll"
        anchors.fill: parent
        anchors.margins: pane.inset
        clip: true
        contentWidth: availableWidth
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        ScrollBar.vertical: AppScrollBar {
            id: verticalBar
            objectName: "recoveryVerticalScrollBar"
            viewport: scroll
            scrollTarget: scroll.contentItem
            wheelRegion: pane
            Accessible.name: "手动匹配内容滚动条"
        }
        ColumnLayout {
            width: scroll.availableWidth
            spacing: 12
            Label {
                Layout.fillWidth: true
                text: pane.recovery.reason
                visible: !pane.manualSearch
                wrapMode: Text.WordWrap
                Accessible.role: Accessible.AlertMessage
            }
            Label {
                Layout.fillWidth: true
                text: pane.recovery.pauseStatus
                visible: !pane.manualSearch
                wrapMode: Text.WordWrap
                color: Ui.secondaryText
            }
            Label { text: "当前播放文件名／媒体标题"; font.weight: Font.DemiBold; visible: !pane.manualSearch }
            TextArea {
                objectName: "recoveryFileName"
                visible: !pane.manualSearch
                Layout.fillWidth: true
                text: pane.recovery.title
                readOnly: true
                selectByMouse: true
                wrapMode: TextEdit.WrapAnywhere
                Accessible.name: "当前播放文件名，可选择并复制"
            }
            Button {
                visible: pane.allowLocalXml
                objectName: "chooseRecoveryXml"
                text: "选择本地 XML"
                onClicked: { xmlDialog.task = pane.backend.taskId; xmlDialog.open() }
            }
            Button {
                objectName: "copyRecoveryFileName"
                visible: !pane.manualSearch
                text: "复制完整文件名"
                onClicked: pane.backend.copyRecoveryTitle()
            }
            Label {
                Layout.fillWidth: true
                text: pane.manualSearch ? "搜索并确认动画与剧集后，为当前视频载入弹幕。"
                    : "输入番名搜索，确认动画与剧集后载入弹幕。仅当本次操作自动暂停了播放器，完成或取消时才恢复播放。取消后下一集仍会自动识别。"
                color: Ui.secondaryText
                wrapMode: Text.WordWrap
            }
            OnlineDanmakuPane {
                id: online
                enabled: !pane.backend.loading
                Layout.fillWidth: true
                library: pane.activeLibrary
                loadActionText: "确认并载入弹幕"
                onCancelled: pane.cancelMatching()
                onOpenSettings: {
                    pane.cancelMatching()
                    pane.openSettings()
                }
            }
        }
    }
    FileDialog {
        id: xmlDialog
        objectName: "recoveryXmlDialog"
        property double task: -1
        parentWindow: pane.Window.window
        modality: Qt.WindowModal
        // The Windows native picker runs on a helper thread. The task may
        // change at any time, so use Qt's picker that can close synchronously.
        options: FileDialog.DontUseNativeDialog
        title: "为当前视频选择弹幕 XML"
        nameFilters: ["弹幕 XML (*.xml)"]
        onAccepted: {
            if (pane.presented && task === pane.backend.taskId && pane.backend.canSelectSource)
                pane.backend.loadFile(selectedFile)
        }
    }
}
