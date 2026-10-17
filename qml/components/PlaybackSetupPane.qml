import QtQuick
import "../theme"
import "../components"
import QtQuick.Controls.FluentWinUI3
import QtQuick.Layouts
import QtQuick.Dialogs
import "../i18n"

ColumnLayout {
    id: page
    required property QtObject backend
    readonly property QtObject session: backend.danmaku
    property int sourceTab: 0
    readonly property bool effectiveManual: session.manualMode
    readonly property var sessionChoices: {
        const choices = page.backend.sessions.slice()
        const saved = page.backend.settings.values.targetSession
        if (saved.length > 0 && !choices.some(entry => entry.id === saved))
            choices.unshift({ id: saved, label: I18n.format(page.session.targetSessionConnected
                ? I18n.player.playerBadgeConnected : I18n.player.savedSessionWaiting, saved) })
        return choices
    }
    readonly property color playerConnectionColor: effectiveManual ? Ui.secondaryText
        : session.targetSessionConnected ? Ui.successText : Ui.cautionText
    readonly property string blockedReason: session.loading ? I18n.player.blockedLoading
        : !effectiveManual && backend.settings.values.targetSession.length === 0 ? I18n.player.blockedNoSession
        : session.total <= 0 && (effectiveManual || !backend.settings.values.automaticDanmaku) ? I18n.player.blockedNoDanmaku : ""
    Layout.fillWidth: true
    spacing: Ui.sectionGap

    Label {
        Layout.fillWidth: true
        text: page.session.error
        visible: text.length > 0
        wrapMode: Text.WordWrap
        color: Ui.cautionText
        Accessible.role: Accessible.AlertMessage
    }


    function formatTime(value) {
        const seconds = Math.max(0, Math.floor(value || 0))
        const minutes = Math.floor(seconds / 60)
        return (seconds >= 3600 ? Math.floor(minutes / 60) + ":" + String(minutes % 60).padStart(2, "0") : String(minutes).padStart(2, "0")) + ":" + String(seconds % 60).padStart(2, "0")
    }

    function selectSourceTab(tab) { sourceTabs.currentIndex = tab }

    SettingsSection {
        title: I18n.player.sourceTabsAccessible

        TabBar {
            id: sourceTabs
            objectName: "danmakuSourceTabs"
            onCurrentIndexChanged: if (currentIndex === 1) page.backend.library.refreshCache()
            currentIndex: page.sourceTab
            Layout.fillWidth: true
            Accessible.name: I18n.player.sourceTabsAccessible
            TabButton { objectName: "sourceLocalTab"; text: I18n.player.sourceLocalTab; icon.color: palette.buttonText }
            TabButton { text: I18n.player.sourceCachedTab; icon.color: palette.buttonText }
        }

        // --- 本地 XML 面板 ---
        ColumnLayout {
            visible: sourceTabs.currentIndex === 0
            Layout.fillWidth: true
            spacing: 10

            // 状态 A：已载入弹幕时，展示精致的媒体就绪卡片（完全收起输入框）
            Control {
                id: loadedDanmakuCard
                visible: page.session.sourceTitle.length > 0 || page.session.total > 0
                Layout.fillWidth: true
                leftPadding: Ui.cardPaddingX
                rightPadding: Ui.cardPaddingX
                topPadding: 14
                bottomPadding: 14

                background: Rectangle {
                    radius: Ui.cardRadius
                    color: Ui.cardBackground
                    border.width: 1
                    border.color: Ui.cardBorder
                }

                contentItem: RowLayout {
                    spacing: 14

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4

                        Label {
                            visible: text.length > 0
                            text: page.session.sourceTitle
                            font.weight: Font.DemiBold
                            font.pixelSize: Ui.bodySize
                            color: Ui.textColor
                            Layout.fillWidth: true
                            wrapMode: Text.WrapAnywhere
                            Accessible.name: I18n.player.currentDanmakuPrefix + text
                        }

                        RowLayout {
                            spacing: 8

                            Rectangle {
                                radius: 9
                                implicitHeight: 18
                                implicitWidth: loadedCountBadge.implicitWidth + 12
                                color: Ui.dark ? Qt.rgba(0.42, 0.80, 0.37, 0.15) : Qt.rgba(0.06, 0.49, 0.06, 0.10)
                                border.width: 1
                                border.color: Ui.dark ? "#6CCB5F" : "#107C10"

                                Label {
                                    id: loadedCountBadge
                                    anchors.centerIn: parent
                                    text: I18n.format(I18n.player.loadedCountFormat, page.session.total)
                                    font.pixelSize: Ui.captionSize - 1
                                    color: Ui.dark ? "#6CCB5F" : "#107C10"
                                }
                            }

                            Label {
                                text: page.session.filePath.length > 0 && page.session.sourceTitle === page.session.filePath
                                    ? page.session.filePath : I18n.player.loadedDanmakuCardTitle
                                font.pixelSize: Ui.captionSize
                                color: Ui.secondaryText
                                elide: Text.ElideMiddle
                                Layout.fillWidth: true
                            }
                        }
                    }

                    RowLayout {
                        spacing: 8

                        Button {
                            text: I18n.player.changeDanmakuBtn
                            onClicked: filePicker.open()
                        }

                        Button {
                            text: I18n.player.clearDanmakuBtn
                            onClicked: {
                                page.session.unload()
                                if (pathInput) pathInput.text = ""
                            }
                        }
                    }
                }
            }

            // 状态 B：未载入弹幕时，展示文件路径选择与载入操作
            ColumnLayout {
                visible: !(page.session.sourceTitle.length > 0 || page.session.total > 0)
                Layout.fillWidth: true
                spacing: 8

                GridLayout {
                    Layout.fillWidth: true
                    columns: width >= 600 ? 2 : 1
                    columnSpacing: 8
                    rowSpacing: 8

                    TextField {
                        id: pathInput
                        objectName: "danmakuPath"
                        Layout.fillWidth: true
                        text: page.session.filePath
                        placeholderText: I18n.player.pathPlaceholder
                        Accessible.name: I18n.player.pathAccessible
                        onAccepted: if (!page.session.loading) page.backend.loadFile(text)
                    }

                    Flow {
                        Layout.preferredWidth: 216
                        Layout.fillWidth: parent.columns === 1
                        spacing: 8

                        Button {
                            text: I18n.player.browseFile
                            onClicked: filePicker.open()
                        }

                        Button {
                            text: page.session.loading ? I18n.player.cancelLoad : I18n.player.loadDanmaku
                            highlighted: pathInput.text.trim().length > 0
                            enabled: page.session.loading || pathInput.text.trim().length > 0
                            onClicked: page.session.loading ? page.backend.cancelLoad() : page.backend.loadFile(pathInput.text)
                        }
                    }
                }

                ProgressBar {
                    visible: page.session.loading
                    Layout.fillWidth: true
                    indeterminate: page.session.loadProgress < 0
                    from: 0
                    to: 100
                    value: Math.max(0, page.session.loadProgress)
                }
            }
        }

        CachedDanmakuPane {
            visible: sourceTabs.currentIndex === 1
            Layout.fillWidth: true
            library: page.backend.library
        }
    }

    // ==========================================
    // 3. 播放方式与外部联动配置区
    // ==========================================
    SettingsSection {
        title: I18n.player.modeSectionTitle

        // 播放模式切换单选按钮
        RowLayout {
            Layout.fillWidth: true
            spacing: 24
            RadioButton {
                objectName: "syncMode"
                text: I18n.player.syncMode
                checked: !page.effectiveManual
                enabled: !page.session.running
                onClicked: page.backend.setManualMode(false)
            }
            RadioButton {
                objectName: "manualMode"
                text: I18n.player.manualMode
                checked: page.effectiveManual
                enabled: !page.session.running
                onClicked: page.backend.setManualMode(true)
            }
        }

        // 跟随播放器联动卡片
        ColumnLayout {
            visible: !page.effectiveManual
            Layout.fillWidth: true
            spacing: 10

            AppComboBox {
                id: sessionBox
                objectName: "targetSessionChoice"
                Layout.fillWidth: true
                model: page.sessionChoices
                textRole: "label"
                valueRole: "id"
                enabledRole: "connectable"
                displayText: currentIndex >= 0 ? currentText : I18n.player.sessionWaitPlaceholder
                Accessible.name: I18n.player.sessionAccessible
                onActivated: {
                    const entry = page.sessionChoices[currentIndex]
                    if (entry && entry.connectable === false) syncSelection()
                    else page.backend.selectSession(currentValue)
                }
                function syncSelection() { currentIndex = indexOfValue(page.backend.settings.values.targetSession) }
                onModelChanged: syncSelection()
                Component.onCompleted: syncSelection()
                Connections { target: page.backend.settings; function onChanged() { sessionBox.syncSelection() } }
            }

            Label {
                text: I18n.player.sessionHint
                color: Ui.secondaryText
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            Label {
                objectName: "vlcSessionDiscoveryHint"
                visible: page.sessionChoices.some(entry => entry.connectable === false)
                text: I18n.player.vlcDiscoveryHint
                color: Ui.secondaryText
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            Label {
                objectName: "savedPlayerStatus"
                visible: page.backend.settings.values.targetSession.length > 0
                text: I18n.format(page.session.targetSessionConnected ? I18n.player.playerBadgeConnected : I18n.player.savedSessionWaiting,
                    page.backend.settings.values.targetSession)
                color: page.playerConnectionColor
                wrapMode: Text.WrapAnywhere
                Layout.fillWidth: true
                Accessible.role: Accessible.StaticText
            }
            Button {
                objectName: "forgetTargetSession"
                visible: page.backend.settings.values.targetSession.length > 0
                text: I18n.player.forgetSession
                onClicked: page.backend.selectSession("")
            }

            SettingsRow {
                title: I18n.player.automaticTitle
                description: I18n.player.automaticHint
                Switch {
                    objectName: "automaticDanmakuSwitch"
                    checked: page.backend.automatic.enabled
                    Accessible.name: I18n.player.automaticTitle
                    onToggled: page.backend.setAutomaticDanmaku(checked)
                }
            }
            Label {
                objectName: "automaticDanmakuStatus"
                visible: page.backend.automatic.enabled
                text: (page.backend.automatic.recognizedTitle.length > 0 ? page.backend.automatic.recognizedTitle + "\n" : "")
                    + page.backend.automatic.status
                textFormat: Text.PlainText
                color: Ui.secondaryText
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
                Accessible.role: Accessible.StaticText
            }
            Flow {
                visible: page.backend.automatic.enabled
                Layout.fillWidth: true
                spacing: 8
                Button {
                    objectName: "automaticDanmakuChoice"
                    visible: page.backend.automatic.needsChoice
                    text: I18n.player.automaticChoice
                    onClicked: page.backend.showAutomaticDanmakuChoice()
                }
                Button {
                    objectName: "retryAutomaticDanmaku"
                    text: page.backend.automatic.suspended ? I18n.player.automaticResume : I18n.player.automaticRetry
                    onClicked: page.backend.automatic.retry()
                }
                Button {
                    objectName: "forgetAutomaticAssociation"
                    visible: page.backend.automatic.recognizedTitle.length > 0
                    text: I18n.player.automaticForget
                    onClicked: page.backend.automatic.forgetAssociation()
                }
            }
            RowLayout {
                visible: page.backend.automatic.enabled && page.backend.automatic.library.busy
                Layout.fillWidth: true
                spacing: 8
                ProgressBar {
                    Layout.fillWidth: true
                    indeterminate: page.backend.automatic.library.progress < 0
                    from: 0; to: 100
                    value: Math.max(0, page.backend.automatic.library.progress)
                    Accessible.name: I18n.online.progressAccessible
                }
                Button {
                    text: I18n.online.cancelBtn
                    onClicked: page.backend.automatic.suspend()
                }
            }

            Disclosure {
                objectName: "sessionDisclosure"
                title: I18n.player.sessionIdTitle
                TextField {
                    objectName: "sessionIdInput"
                    Layout.fillWidth: true
                    placeholderText: I18n.player.sessionIdPlaceholder
                    text: page.backend.settings.values.targetSession
                    Accessible.name: I18n.player.sessionIdAccessible
                    onEditingFinished: page.backend.selectSession(text)
                }
                Label {
                    text: I18n.player.sessionIdHint
                    color: Ui.secondaryText
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
            }
        }

        Label {
            visible: page.effectiveManual
            text: I18n.player.manualHint
            color: Ui.secondaryText
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
    }

    Button {
        objectName: "startPlayback"
        text: "播放所选弹幕"
        enabled: page.blockedReason.length === 0 && !page.session.running
        onClicked: page.session.start(page.effectiveManual)
    }
    FileDialog {
        id: filePicker
        title: I18n.player.browseDialogTitle
        nameFilters: [I18n.player.browseFileFilter]
        onAccepted: page.backend.loadFile(selectedFile.toString())
    }
}
