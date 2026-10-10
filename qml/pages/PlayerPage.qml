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
    property int sourceTab: 0
    property bool independent: false
    readonly property bool effectiveManual: backend.running ? backend.manualMode : independent
    readonly property string blockedReason: backend.loading ? I18n.player.blockedLoading : backend.total <= 0 ? I18n.player.blockedNoDanmaku : !effectiveManual && backend.settings.values.targetSession.length === 0 ? I18n.player.blockedNoSession : ""
    signal openSettings()
    title: I18n.player.title
    description: I18n.player.description

    function formatTime(value) {
        const seconds = Math.max(0, Math.floor(value || 0))
        const minutes = Math.floor(seconds / 60)
        return (seconds >= 3600 ? Math.floor(minutes / 60) + ":" + String(minutes % 60).padStart(2, "0") : String(minutes).padStart(2, "0")) + ":" + String(seconds % 60).padStart(2, "0")
    }

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
            border.color: page.backend.running ? Ui.accentColor : Ui.cardBorder
            Behavior on border.color { ColorAnimation { duration: 150 } }
        }

        contentItem: ColumnLayout {
            spacing: 12

            // --- 状态 A：未运行（空闲/配置与就绪检查）---
            GridLayout {
                visible: !page.backend.running
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
                            color: page.backend.total > 0 ? (Ui.dark ? Qt.rgba(0.42, 0.80, 0.37, 0.15) : Qt.rgba(0.06, 0.49, 0.06, 0.10)) : (Ui.dark ? Qt.rgba(1, 1, 1, 0.06) : Qt.rgba(0, 0, 0, 0.05))
                            border.width: 1
                            border.color: page.backend.total > 0 ? (Ui.dark ? "#6CCB5F" : "#107C10") : Ui.cardBorder

                            RowLayout {
                                id: danmakuBadgeRow
                                anchors.centerIn: parent
                                spacing: 5
                                Rectangle {
                                    width: 6; height: 6; radius: 3
                                    color: page.backend.total > 0 ? (Ui.dark ? "#6CCB5F" : "#107C10") : Ui.secondaryText
                                }
                                Label {
                                    id: danmakuBadgeText
                                    text: page.backend.total > 0 ? I18n.format(I18n.player.danmakuBadgeReady, page.backend.total) : I18n.player.danmakuBadgeNotLoaded
                                    font.pixelSize: Ui.captionSize
                                    color: page.backend.total > 0 ? (Ui.dark ? "#6CCB5F" : "#107C10") : Ui.secondaryText
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
                            color: page.effectiveManual ? (Ui.dark ? Qt.rgba(1, 1, 1, 0.06) : Qt.rgba(0, 0, 0, 0.05))
                                 : (page.backend.settings.values.targetSession.length > 0
                                    ? (Ui.dark ? Qt.rgba(0.42, 0.80, 0.37, 0.15) : Qt.rgba(0.06, 0.49, 0.06, 0.10))
                                    : (Ui.dark ? Qt.rgba(0.92, 0.64, 0, 0.15) : Qt.rgba(0.74, 0.29, 0.04, 0.10)))
                            border.width: 1
                            border.color: page.effectiveManual ? Ui.cardBorder
                                        : (page.backend.settings.values.targetSession.length > 0 ? (Ui.dark ? "#6CCB5F" : "#107C10") : (Ui.dark ? "#EAA300" : "#BC4B09"))

                            RowLayout {
                                id: playerBadgeRow
                                anchors.centerIn: parent
                                spacing: 5
                                Rectangle {
                                    width: 6; height: 6; radius: 3
                                    color: page.effectiveManual ? Ui.secondaryText
                                         : (page.backend.settings.values.targetSession.length > 0 ? (Ui.dark ? "#6CCB5F" : "#107C10") : (Ui.dark ? "#EAA300" : "#BC4B09"))
                                }
                                Label {
                                    id: playerBadgeText
                                    text: page.effectiveManual ? I18n.player.playerBadgeManual
                                         : (page.backend.settings.values.targetSession.length > 0
                                            ? I18n.format(I18n.player.playerBadgeConnected, page.backend.settings.values.targetSession)
                                            : I18n.player.playerBadgeWaiting)
                                    font.pixelSize: Ui.captionSize
                                    color: page.effectiveManual ? Ui.secondaryText
                                         : (page.backend.settings.values.targetSession.length > 0 ? (Ui.dark ? "#6CCB5F" : "#107C10") : (Ui.dark ? "#EAA300" : "#BC4B09"))
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
                        text: page.blockedReason.length > 0 ? page.blockedReason : page.backend.status
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
                        objectName: "stopPlayback"
                        visible: page.backend.running || page.backend.total > 0 || page.backend.loading || page.backend.library.busy
                        text: I18n.player.stopPlayback
                        Accessible.description: I18n.player.stopPlaybackDesc
                        onClicked: page.backend.stop()
                    }

                    Button {
                        id: startPlaybackBtn
                        objectName: "startPlayback"
                        visible: !page.backend.running
                        text: I18n.player.startPlayback
                        highlighted: true
                        enabled: page.blockedReason.length === 0
                        font.weight: Font.DemiBold
                        Layout.preferredHeight: 36
                        Layout.preferredWidth: Math.max(104, implicitWidth)
                        onClicked: page.backend.start(page.independent)
                    }
                }
            }

            // --- 状态 B：正在运行（中控台与实时状态）---
            ColumnLayout {
                visible: page.backend.running
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
                                text: page.backend.mediaTitle.length > 0 ? page.backend.mediaTitle : I18n.player.heroStatusRunning
                                font.pixelSize: Ui.subtitleSize
                                font.weight: Font.DemiBold
                                color: Ui.textColor
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                            Label {
                                text: page.backend.status
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
                            visible: page.backend.running && page.backend.manualMode
                            text: page.backend.playing ? I18n.player.pausePlayback : I18n.player.resumePlayback
                            highlighted: !page.backend.playing
                            onClicked: page.backend.togglePause()
                        }
                        Button {
                            text: I18n.player.stopPlayback
                            Accessible.description: I18n.player.stopPlaybackDesc
                            onClicked: page.backend.stop()
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
                            text: page.formatTime(page.backend.position) + " / " + page.formatTime(page.backend.duration)
                            font.pixelSize: Ui.bodySize
                            font.weight: Font.DemiBold
                            color: Ui.textColor
                        }
                        Label {
                            text: page.backend.manualMode ? I18n.player.timeManualSuffix : I18n.player.timeSyncSuffix
                            font.pixelSize: Ui.captionSize
                            color: Ui.secondaryText
                        }
                        Item { Layout.fillWidth: true }
                    }

                    Slider {
                        visible: page.backend.manualMode
                        Layout.fillWidth: true
                        from: 0
                        to: Math.max(1, page.backend.duration)
                        value: page.backend.position
                        Accessible.name: I18n.player.manualSeekAccessible
                        onMoved: page.backend.seek(value)
                    }

                    ProgressBar {
                        visible: !page.backend.manualMode
                        indeterminate: false
                        Layout.fillWidth: true
                        from: 0
                        to: Math.max(1, page.backend.duration)
                        value: page.backend.position
                        Accessible.name: I18n.player.playerProgressAccessible
                    }
                }

                // 底行：性能指标监控胶囊与折叠
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12

                    Flow {
                        Layout.fillWidth: true
                        spacing: 8

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
                    }

                    Disclosure {
                        title: I18n.player.runtimeDetailsTitle
                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            text: I18n.format(I18n.player.runtimeDetailsFormat, page.backend.metrics.active || 0, page.backend.metrics.dropped || 0, Number(page.backend.metrics.memoryMiB || 0).toFixed(1))
                            color: Ui.secondaryText
                        }
                    }
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
            onCurrentIndexChanged: if (currentIndex === 2) page.backend.library.refreshCache()
            currentIndex: page.sourceTab
            Layout.fillWidth: true
            Accessible.name: I18n.player.sourceTabsAccessible
            TabButton { objectName: "sourceLocalTab"; text: I18n.player.sourceLocalTab; icon.color: palette.buttonText }
            TabButton { text: I18n.player.sourceOnlineTab; icon.color: palette.buttonText }
            TabButton { text: I18n.player.sourceCachedTab; icon.color: palette.buttonText }
        }

        // --- 本地 XML 面板 ---
        ColumnLayout {
            visible: sourceTabs.currentIndex === 0
            Layout.fillWidth: true
            spacing: 10

            // 已载入弹幕信息微型卡片
            Control {
                visible: page.backend.sourceTitle.length > 0 || page.backend.total > 0
                Layout.fillWidth: true
                leftPadding: Ui.cardPaddingX
                rightPadding: Ui.cardPaddingX
                topPadding: 12
                bottomPadding: 12

                background: Rectangle {
                    radius: Ui.cardRadius
                    color: Ui.cardBackground
                    border.width: 1
                    border.color: Ui.cardBorder
                }

                contentItem: RowLayout {
                    spacing: 12

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Label {
                            visible: text.length > 0
                            text: page.backend.sourceTitle
                            font.weight: Font.DemiBold
                            font.pixelSize: Ui.bodySize
                            Layout.fillWidth: true
                            wrapMode: Text.WrapAnywhere
                            Accessible.name: I18n.player.currentDanmakuPrefix + text
                        }
                        Label {
                            text: I18n.format(I18n.player.loadedCountFormat, page.backend.total)
                            font.pixelSize: Ui.captionSize
                            color: Ui.secondaryText
                        }
                    }

                    Button {
                        text: I18n.player.changeDanmakuBtn
                        onClicked: filePicker.open()
                    }
                }
            }

            // 文件路径选择输入行
            GridLayout {
                Layout.fillWidth: true
                columns: width >= 600 ? 2 : 1
                columnSpacing: 8
                rowSpacing: 8

                TextField {
                    id: pathInput
                    objectName: "danmakuPath"
                    Layout.fillWidth: true
                    text: page.backend.filePath
                    placeholderText: I18n.player.pathPlaceholder
                    Accessible.name: I18n.player.pathAccessible
                    onAccepted: if (!page.backend.loading) page.backend.loadFile(text)
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
                        text: page.backend.loading ? I18n.player.cancelLoad : I18n.player.loadDanmaku
                        enabled: page.backend.loading || pathInput.text.trim().length > 0
                        onClicked: page.backend.loading ? page.backend.cancelLoad() : page.backend.loadFile(pathInput.text)
                    }
                }
            }

            ProgressBar {
                visible: page.backend.loading
                Layout.fillWidth: true
                indeterminate: page.backend.loadProgress < 0
                from: 0
                to: 100
                value: Math.max(0, page.backend.loadProgress)
            }
        }

        // --- 在线搜索与缓存面板 ---
        OnlineDanmakuPane {
            visible: sourceTabs.currentIndex === 1
            Layout.fillWidth: true
            library: page.backend.library
            onOpenSettings: page.openSettings()
        }

        CachedDanmakuPane {
            visible: sourceTabs.currentIndex === 2
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
                enabled: !page.backend.running
                onClicked: page.independent = false
            }
            RadioButton {
                objectName: "manualMode"
                text: I18n.player.manualMode
                checked: page.effectiveManual
                enabled: !page.backend.running
                onClicked: page.independent = true
            }
        }

        // 跟随播放器联动卡片
        ColumnLayout {
            visible: !page.effectiveManual
            Layout.fillWidth: true
            spacing: 10

            AppComboBox {
                id: sessionBox
                Layout.fillWidth: true
                model: page.backend.sessions
                textRole: "label"
                valueRole: "id"
                displayText: currentIndex >= 0 ? currentText : I18n.player.sessionWaitPlaceholder
                Accessible.name: I18n.player.sessionAccessible
                onActivated: page.backend.selectSession(currentValue)
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
