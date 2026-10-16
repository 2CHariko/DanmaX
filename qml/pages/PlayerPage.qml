import QtQuick
import "../theme"
import "../components"
import QtQuick.Controls.FluentWinUI3
import QtQuick.Layouts
import QtQuick.Dialogs
import "../i18n"

PageFrame {
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
    title: I18n.player.title
    description: I18n.player.description

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

    // ==========================================
    // 1. 顶部 HERO 主控台与就绪状态看板
    // ==========================================
    Control {
        id: heroCard
        Layout.fillWidth: true
        leftPadding: Ui.cardPaddingX
        rightPadding: Ui.cardPaddingX
        topPadding: Ui.cardPaddingY
        bottomPadding: Ui.cardPaddingY

        background: Rectangle {
            radius: Ui.cardRadius
            color: Ui.cardBackground
            border.width: 1
            border.color: page.session.running ? Ui.accentColor : Ui.cardBorder
            Behavior on border.color { ColorAnimation { duration: 150 } }
        }

        contentItem: ColumnLayout {
            spacing: 12

            // --- 状态 A：未运行（空闲/配置与就绪检查）---
            GridLayout {
                visible: !page.session.running
                Layout.fillWidth: true
                columns: width >= 600 ? 2 : 1
                columnSpacing: 16
                rowSpacing: 12

                // 左侧：主状态与就绪检查胶囊
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    RowLayout {
                        spacing: 8
                        Rectangle {
                            width: 8
                            height: 8
                            radius: 4
                            color: page.blockedReason.length === 0 ? (Ui.dark ? "#6CCB5F" : "#107C10") : (Ui.dark ? "#EAA300" : "#BC4B09")
                        }
                        Label {
                            text: page.blockedReason.length === 0 ? I18n.player.heroStatusReady : I18n.player.controlSectionTitle
                            font.pixelSize: Ui.subtitleSize
                            font.weight: Font.DemiBold
                            color: Ui.textColor
                        }
                    }

                    // 就绪指示徽章流
                    Flow {
                        Layout.fillWidth: true
                        spacing: 8

                        // 弹幕源徽章
                        Rectangle {
                            radius: 11
                            implicitHeight: 22
                            implicitWidth: danmakuBadgeRow.implicitWidth + 16
                            color: page.session.total > 0 ? (Ui.dark ? Qt.rgba(0.42, 0.80, 0.37, 0.15) : Qt.rgba(0.06, 0.49, 0.06, 0.10)) : (Ui.dark ? Qt.rgba(1, 1, 1, 0.06) : Qt.rgba(0, 0, 0, 0.05))
                            border.width: 1
                            border.color: page.session.total > 0 ? (Ui.dark ? "#6CCB5F" : "#107C10") : Ui.cardBorder

                            RowLayout {
                                id: danmakuBadgeRow
                                anchors.centerIn: parent
                                spacing: 5
                                Rectangle {
                                    width: 6; height: 6; radius: 3
                                    color: page.session.total > 0 ? (Ui.dark ? "#6CCB5F" : "#107C10") : Ui.secondaryText
                                }
                                Label {
                                    id: danmakuBadgeText
                                    text: page.session.total > 0 ? I18n.format(I18n.player.danmakuBadgeReady, page.session.total) : I18n.player.danmakuBadgeNotLoaded
                                    font.pixelSize: Ui.captionSize
                                    color: page.session.total > 0 ? (Ui.dark ? "#6CCB5F" : "#107C10") : Ui.secondaryText
                                }
                            }
                            Accessible.role: Accessible.StaticText
                            Accessible.name: I18n.player.danmakuBadgeAccessible + " " + danmakuBadgeText.text
                        }

                        // 播放器连接徽章
                        Rectangle {
                            radius: 11
                            implicitHeight: 22
                            implicitWidth: playerBadgeRow.implicitWidth + 16
                            width: Math.min(implicitWidth, parent.width)
                            color: Ui.cardBackground
                            border.width: 1
                            border.color: page.playerConnectionColor

                            RowLayout {
                                id: playerBadgeRow
                                anchors.centerIn: parent
                                width: Math.min(implicitWidth, parent.width - 16)
                                spacing: 5
                                Rectangle {
                                    width: 6; height: 6; radius: 3
                                    color: page.playerConnectionColor
                                }
                                Label {
                                    id: playerBadgeText
                                    objectName: "playerConnectionBadge"
                                    Layout.fillWidth: true
                                    text: page.effectiveManual ? I18n.player.playerBadgeManual
                                         : (page.backend.settings.values.targetSession.length > 0
                                            ? I18n.format(page.session.targetSessionConnected ? I18n.player.playerBadgeConnected : I18n.player.savedSessionWaiting, page.backend.settings.values.targetSession)
                                            : I18n.player.playerBadgeWaiting)
                                    font.pixelSize: Ui.captionSize
                                    color: page.playerConnectionColor
                                    elide: Text.ElideRight
                                }
                            }
                            Accessible.role: Accessible.StaticText
                            Accessible.name: I18n.player.playerBadgeAccessible + " " + playerBadgeText.text
                        }
                    }

                    // 辅助状态/行动说明
                    Label {
                        visible: text.length > 0
                        text: page.blockedReason.length > 0 ? page.blockedReason : page.session.status
                        font.pixelSize: Ui.captionSize
                        color: page.blockedReason.length > 0 ? (Ui.dark ? "#EAA300" : "#BC4B09") : Ui.secondaryText
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                        Accessible.name: I18n.player.statusAccessiblePrefix + text
                    }
                }

                // 右侧：主要行动按钮区
                Flow {
                    Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                    spacing: 8

                    Button {
                        objectName: "unloadDanmaku"
                        visible: page.session.running || page.session.total > 0 || page.session.loading || page.backend.library.busy
                        text: I18n.player.unloadDanmaku
                        Accessible.description: I18n.player.unloadDanmakuDesc
                        onClicked: page.session.unload()
                    }

                    Button {
                        id: startPlaybackBtn
                        objectName: "startPlayback"
                        visible: !page.session.running
                        text: I18n.player.startPlayback
                        highlighted: true
                        enabled: page.blockedReason.length === 0
                        font.weight: Font.DemiBold
                        Layout.preferredHeight: 36
                        Layout.preferredWidth: Math.max(104, implicitWidth)
                        onClicked: page.session.start(page.effectiveManual)
                    }
                }
            }

            // --- 状态 B：正在运行（中控台与实时状态）---
            ColumnLayout {
                visible: page.session.running
                Layout.fillWidth: true
                spacing: 12

                // 顶行：媒体标题与中控动作
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Rectangle {
                            width: 10
                            height: 10
                            radius: 5
                            color: Ui.accentColor
                            SequentialAnimation on opacity {
                                loops: Animation.Infinite
                                NumberAnimation { to: 0.35; duration: 900; easing.type: Easing.InOutQuad }
                                NumberAnimation { to: 1.0; duration: 900; easing.type: Easing.InOutQuad }
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Label {
                                text: page.session.mediaTitle.length > 0 ? page.session.mediaTitle : I18n.player.heroStatusRunning
                                font.pixelSize: Ui.subtitleSize
                                font.weight: Font.DemiBold
                                color: Ui.textColor
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                            Label {
                                text: page.session.status
                                font.pixelSize: Ui.captionSize
                                color: Ui.secondaryText
                                Layout.fillWidth: true
                            }
                        }
                    }

                    RowLayout {
                        spacing: 8
                        Button {
                            objectName: "pausePlayback"
                            visible: page.session.running && page.session.manualMode
                            text: page.session.completed ? I18n.player.replayPlayback : page.session.playing ? I18n.player.pausePlayback : I18n.player.resumePlayback
                            highlighted: !page.session.playing
                            onClicked: page.session.togglePause()
                        }
                        Button {
                            objectName: "stopPlayback"
                            text: I18n.player.stopPlayback
                            Accessible.description: I18n.player.stopPlaybackDesc
                            onClicked: page.session.stop()
                        }
                        Button {
                            text: I18n.player.unloadDanmaku
                            Accessible.description: I18n.player.unloadDanmakuDesc
                            onClicked: page.session.unload()
                        }
                    }
                }

                // 中行：时间轴与进度条
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4

                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            text: page.formatTime(manualSeek.pendingSeek ? manualSeek.value : page.session.position)
                                + " / " + page.formatTime(page.session.duration)
                            font.pixelSize: Ui.bodySize
                            font.weight: Font.DemiBold
                            color: Ui.textColor
                        }
                        Label {
                            text: page.session.manualMode ? I18n.player.timeManualSuffix : I18n.player.timeSyncSuffix
                            font.pixelSize: Ui.captionSize
                            color: Ui.secondaryText
                        }
                        Item { Layout.fillWidth: true }
                    }

                    Slider {
                        id: manualSeek
                        visible: page.session.manualMode
                        property bool pendingSeek: false
                        property bool cancelling: false
                        Layout.fillWidth: true
                        from: 0
                        to: Math.max(1, page.session.duration)
                        Binding on value { value: page.session.position; when: !manualSeek.pressed && !manualSeek.pendingSeek }
                        Accessible.name: I18n.player.manualSeekAccessible
                        onMoved: {
                            pendingSeek = true
                            if (!pressed && page.session.running) {
                                const target = value
                                pendingSeek = false
                                page.session.seek(target)
                            }
                        }
                        onPressedChanged: {
                            if (!pressed && pendingSeek && !cancelling && page.session.running) {
                                const target = value
                                pendingSeek = false
                                page.session.seek(target)
                            }
                        }
                        Connections {
                            target: page.backend
                            function onStateChanged() {
                                if (!page.session.running) {
                                    manualSeek.cancelling = true
                                    manualSeek.pendingSeek = false
                                    manualSeek.cancelling = false
                                }
                            }
                        }
                    }

                    ProgressBar {
                        visible: !page.session.manualMode
                        indeterminate: false
                        Layout.fillWidth: true
                        from: 0
                        to: Math.max(1, page.session.duration)
                        value: page.session.position
                        Accessible.name: I18n.player.playerProgressAccessible
                    }
                }

                // 底行：性能指标监控胶囊
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12

                    Rectangle {
                        radius: 11
                        implicitHeight: 22
                        implicitWidth: metricActiveLabel.implicitWidth + 16
                        color: Ui.dark ? Qt.rgba(1, 1, 1, 0.06) : Qt.rgba(0, 0, 0, 0.05)
                        border.width: 1
                        border.color: Ui.cardBorder

                        Label {
                            id: metricActiveLabel
                            anchors.centerIn: parent
                            text: I18n.format(I18n.player.runtimeDetailsFormat, page.backend.metrics.active || 0, page.backend.metrics.dropped || 0, Number(page.backend.metrics.memoryMiB || 0).toFixed(1))
                            font.pixelSize: Ui.captionSize
                            color: Ui.secondaryText
                        }
                    }

                    Item { Layout.fillWidth: true }
                }
            }
        }
    }

    // ==========================================
    // 2. 弹幕来源配置区
    // ==========================================
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

    FileDialog {
        id: filePicker
        title: I18n.player.browseDialogTitle
        nameFilters: [I18n.player.browseFileFilter]
        onAccepted: page.backend.loadFile(selectedFile.toString())
    }
}
