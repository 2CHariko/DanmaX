import QtQuick
import QtQuick.Controls.FluentWinUI3
import QtQuick.Layouts
import QtQuick.Dialogs
import "../theme"

SettingsSection {
    id: pane
    required property QtObject backend
    readonly property QtObject store: backend.players
    property QtObject editor: null
    property var pendingPathInput: null
    title: "外部播放器"
    Component.onCompleted: editor = store.createEditor(pane)
    onVisibleChanged: if (!visible && manager.visible) manager.close()
    function manage() { editor.begin(); manager.open() }
    function ensureVisible(item) {
        const flick = playerScroll.contentItem
        if (!flick || !item) return
        const y = item.mapToItem(playerCards, 0, 0).y
        const target = y < flick.contentY ? y : Math.max(flick.contentY, y + item.height - flick.height)
        flick.contentY = Math.max(0, Math.min(Math.max(0, flick.contentHeight - flick.height), target))
    }
    function focusIssue() {
        Qt.callLater(() => {
            for (let i = 0; i < playerRows.count; ++i) {
                const row = playerRows.itemAt(i)
                if (row && row.profile.id === editor.issueProfileId) { row.focusIssue(); return }
            }
            generalIssue.forceActiveFocus()
        })
    }
    Connections {
        target: pane.Window.window
        function onActiveFocusItemChanged() {
            const focused = pane.Window.window.activeFocusItem
            let ancestor = focused
            while (ancestor) {
                if (ancestor === playerCards) {
                    Qt.callLater(() => { if (manager.visible && focused) pane.ensureVisible(focused) })
                    return
                }
                ancestor = ancestor.parent
            }
        }
    }
    SettingsRow {
        title: "播放器配置"
        description: pane.store.error.length > 0 ? pane.store.error
            : "预设 " + pane.store.profiles.length + " 个 · 默认 " + pane.store.selected.name
              + (pane.store.selected.synchronize ? " · 打开后同步弹幕" : " · 仅打开视频")
        Button {
            id: manageButton
            objectName: "manageExternalPlayers"
            text: "管理播放器"
            Accessible.name: text
            onClicked: pane.manage()
        }
    }
    Dialog {
        id: manager
        objectName: "externalPlayerDialog"
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: Math.min(680, Math.max(0, parent.width - 32))
        height: Math.min(implicitHeight, Math.max(0, parent.height - 32))
        title: "管理外部播放器"
        leftPadding: Ui.scrollInset(playerBar.implicitWidth, padding)
        rightPadding: leftPadding
        closePolicy: Popup.CloseOnEscape
        standardButtons: Dialog.NoButton
        onClosed: {
            pane.pendingPathInput = null
            programDialog.close()
            if (pane.editor) pane.editor.begin()
            if (pane.visible) manageButton.forceActiveFocus(Qt.TabFocusReason)
        }
        footer: DialogButtonBox {
            alignment: Qt.AlignRight
            Button {
                objectName: "saveExternalPlayers"
                text: "保存"
                enabled: pane.editor !== null
                highlighted: enabled
                onClicked: {
                    if (pane.pendingPathInput) pane.pendingPathInput.submit()
                    if (pane.editor.save()) manager.close()
                    else pane.focusIssue()
                }
            }
            Button {
                objectName: "cancelExternalPlayers"
                text: "取消"
                onClicked: manager.close()
            }
        }
        contentItem: ColumnLayout {
            width: manager.availableWidth
            implicitWidth: 600
            spacing: 12
            Label {
                text: "选择预设播放器的程序路径。修改在保存后生效。"
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Ui.secondaryText
                font.pixelSize: Ui.captionSize
            }
            Label {
                id: generalIssue
                objectName: "externalPlayerGeneralIssue"
                visible: pane.editor && pane.editor.issue.length > 0 && pane.editor.issueProfileId.length === 0
                text: pane.editor ? pane.editor.issue : ""
                Layout.fillWidth: true
                wrapMode: Text.WrapAnywhere
                textFormat: Text.PlainText
                color: Ui.cautionText
                activeFocusOnTab: true
                Accessible.name: text
            }
            ScrollView {
                id: playerScroll
                objectName: "externalPlayerScrollView"
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: 0
                Layout.preferredHeight: Math.min(playerCards.implicitHeight, 480 * Ui.textScale)
                contentWidth: availableWidth
                padding: 0
                clip: true
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                WheelHandler {
                    parent: playerScroll.contentItem
                    target: null
                    enabled: manager.visible
                    acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                    orientation: Qt.Vertical
                    blocking: true
                    onWheel: event => { event.accepted = playerBar.handleWheel(event) }
                }
                ScrollBar.vertical: AppScrollBar {
                    id: playerBar
                    objectName: "externalPlayerScrollBar"
                    viewport: playerScroll
                    scrollTarget: playerScroll.contentItem
                    wheelRegion: manager.contentItem.parent
                    wheelTop: manager.contentItem.y + playerScroll.y
                    wheelBottom: wheelTop + playerScroll.height
                    parent: manager.contentItem.parent
                    y: manager.contentItem.y + playerScroll.y
                    Accessible.name: "播放器列表滚动条"
                }
                ColumnLayout {
                    id: playerCards
                    width: playerScroll.availableWidth
                    spacing: 4
                    Repeater {
                        id: playerRows
                        model: pane.editor
                        delegate: Control {
                            id: card
                            required property var profile
                            required property int index
                            objectName: "playerProfile-" + profile.id
                            Layout.fillWidth: true
                            padding: 16
                            implicitHeight: Math.max(70, contentItem.implicitHeight + topPadding + bottomPadding)
                            function focusIssue() {
                                Qt.callLater(() => { pathStatus.forceActiveFocus(); pane.ensureVisible(pathStatus) })
                            }
                            background: Rectangle {
                                color: Ui.cardBackground
                                border.color: Ui.cardBorder
                                radius: Ui.cardRadius
                            }
                            contentItem: ColumnLayout {
                                spacing: 12
                                RowLayout {
                                    Layout.fillWidth: true
                                    Label {
                                        text: card.profile.name + " · x64"
                                        Layout.fillWidth: true
                                        textFormat: Text.PlainText
                                        font.weight: Font.DemiBold
                                    }
                                    RadioButton {
                                        objectName: "defaultPlayer-" + card.profile.id
                                        text: "默认"
                                        ButtonGroup.group: defaultGroup
                                        checked: pane.editor.defaultId === card.profile.id
                                        Accessible.name: "将 " + card.profile.name + " 设为默认播放器"
                                        onClicked: pane.editor.setDefault(card.profile.id)
                                    }
                                }
                                Label {
                                    text: "使用预设启动规则 · " + card.profile.synchronizationLabel
                                    Layout.fillWidth: true
                                    wrapMode: Text.WordWrap
                                    color: Ui.secondaryText
                                    font.pixelSize: Ui.captionSize
                                }
                                TextField {
                                    id: executableInput
                                    objectName: "externalPlayerExecutable-" + card.profile.id
                                    Layout.fillWidth: true
                                    text: card.profile.executable
                                    placeholderText: "输入路径、自动发现或选择程序"
                                    maximumLength: 32768
                                    function submit() {
                                        if (pane.pendingPathInput !== executableInput) return
                                        pane.pendingPathInput = null
                                        if (!manager.visible) return
                                        pane.editor.edit(card.profile.id)
                                        pane.editor.enterExecutable(text)
                                        text = Qt.binding(() => card.profile.executable)
                                    }
                                    onTextEdited: pane.pendingPathInput = executableInput
                                    onEditingFinished: submit()
                                    Accessible.name: card.profile.name + " 程序路径"
                                }
                                Label {
                                    id: pathStatus
                                    objectName: "externalPlayerValidation-" + card.profile.id
                                    readonly property bool hasIssue: pane.editor.issue.length > 0 && pane.editor.issueProfileId === card.profile.id
                                    text: hasIssue ? pane.editor.issue : card.profile.validation
                                    Layout.fillWidth: true
                                    wrapMode: Text.WrapAnywhere
                                    textFormat: Text.PlainText
                                    color: hasIssue || (card.profile.executable.length > 0 && !card.profile.pathValid) ? Ui.cautionText : Ui.secondaryText
                                    font.pixelSize: Ui.captionSize
                                    activeFocusOnTab: hasIssue
                                    Accessible.name: text
                                }
                                Flow {
                                    Layout.fillWidth: true
                                    spacing: 8
                                    Button {
                                        objectName: "choosePlayerButton-" + card.profile.id
                                        text: "选择程序"
                                        onClicked: {
                                            pane.editor.edit(card.profile.id)
                                            programDialog.profileId = card.profile.id
                                            programDialog.open()
                                        }
                                    }
                                    Button {
                                        objectName: "discoverPlayerButton-" + card.profile.id
                                        text: "自动发现"
                                        visible: card.profile.canDiscover
                                        onClicked: { pane.editor.edit(card.profile.id); pane.editor.discover() }
                                    }
                                }
                                ColumnLayout {
                                    visible: pane.editor.editingId === card.profile.id && pane.editor.candidates.length > 1
                                    Layout.fillWidth: true
                                    AppComboBox {
                                        id: candidatesBox
                                        objectName: "externalPlayerCandidates-" + card.profile.id
                                        Layout.fillWidth: true
                                        model: pane.editor.editingId === card.profile.id ? pane.editor.candidates : []
                                        Accessible.name: "发现的播放器安装路径"
                                    }
                                    Button {
                                        objectName: "useDiscoveredPlayer-" + card.profile.id
                                        text: "使用此路径"
                                        onClicked: pane.editor.chooseCandidate(candidatesBox.currentIndex)
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    ButtonGroup { id: defaultGroup }
    FileDialog {
        id: programDialog
        property string profileId: ""
        title: "选择播放器程序"
        fileMode: FileDialog.OpenFile
        nameFilters: pane.editor ? [pane.editor.executableFilter] : ["播放器 (*.exe)"]
        onAccepted: if (manager.visible && pane.editor.editingId === profileId) pane.editor.chooseExecutable(selectedFile)
    }
}
