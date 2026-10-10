import QtQuick
import QtQuick.Controls.FluentWinUI3
import QtQuick.Layouts
import "../theme"
import "../i18n"

ColumnLayout {
    id: pane
    required property QtObject library
    readonly property string effectiveServer: pane.library.activeServer.length > 0
        ? pane.library.activeServer
        : (pane.library.servers.length > 0 ? pane.library.servers[0] : "")
    signal openSettings()
    spacing: 10

    // 1. 无服务器配置时的引导横幅
    Control {
        visible: pane.library.servers.length === 0
        Layout.fillWidth: true
        leftPadding: Ui.cardPaddingX
        rightPadding: Ui.cardPaddingX
        topPadding: 12
        bottomPadding: 12

        background: Rectangle {
            radius: Ui.cardRadius
            color: Ui.dark ? Qt.rgba(1, 1, 1, 0.05) : Qt.rgba(0, 0, 0, 0.04)
            border.width: 1
            border.color: Ui.dark ? "#EAA300" : "#BC4B09"
        }

        contentItem: RowLayout {
            spacing: 12

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Label {
                    text: I18n.online.noServerConfigured
                    font.pixelSize: Ui.bodySize
                    font.weight: Font.DemiBold
                    color: Ui.textColor
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
                Label {
                    text: I18n.settings.onlineDesc
                    font.pixelSize: Ui.captionSize
                    color: Ui.secondaryText
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
            }

            Button {
                text: I18n.online.openSettingsBtn
                highlighted: true
                onClicked: pane.openSettings()
            }
        }
    }

    // 2. 有服务器时的当前服务指示微条
    RowLayout {
        visible: pane.library.servers.length > 0
        Layout.fillWidth: true
        spacing: 8

        Rectangle {
            visible: pane.effectiveServer.length > 0
            radius: 11
            implicitHeight: 22
            implicitWidth: serverLabel.implicitWidth + 16
            color: Ui.dark ? Qt.rgba(1, 1, 1, 0.06) : Qt.rgba(0, 0, 0, 0.05)
            border.width: 1
            border.color: Ui.cardBorder

            Label {
                id: serverLabel
                anchors.centerIn: parent
                text: pane.effectiveServer.length > 0 ? I18n.online.activeServerPrefix + pane.effectiveServer : ""
                font.pixelSize: Ui.captionSize
                color: Ui.secondaryText
                elide: Text.ElideMiddle
            }
        }

        Item { Layout.fillWidth: true }

        Button {
            text: I18n.settings.manageServersBtn
            flat: true
            onClicked: pane.openSettings()
        }
    }

    // 3. 搜索与级联选择区域
    ColumnLayout {
        visible: pane.library.servers.length > 0
        Layout.fillWidth: true
        spacing: 10

        // 搜索输入行
        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            TextField {
                id: keyword
                objectName: "animeKeyword"
                Layout.fillWidth: true
                placeholderText: I18n.online.searchPlaceholder
                Accessible.name: I18n.online.searchAccessible
                maximumLength: 512
                onAccepted: if (text.trim().length > 0) pane.library.searchAnime(text)
            }

            Button {
                text: I18n.online.searchBtn
                highlighted: keyword.text.trim().length > 0
                enabled: pane.library.servers.length > 0 && keyword.text.trim().length > 0 && !pane.library.busy
                onClicked: pane.library.searchAnime(keyword.text)
            }
        }

        // 检索结果与剧集选择卡片
        Control {
            visible: pane.library.animes.length > 0
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

            contentItem: ColumnLayout {
                spacing: 10

                // 动画下拉行
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4
                    Label {
                        text: I18n.online.animeLabel
                        font.pixelSize: Ui.captionSize
                        font.weight: Font.DemiBold
                        color: Ui.textColor
                    }
                    AppComboBox {
                        id: animeBox
                        objectName: "animeChoice"
                        Layout.fillWidth: true
                        model: pane.library.animes
                        textRole: "title"
                        valueRole: "id"
                        displayText: currentIndex >= 0 ? currentText : I18n.online.animePlaceholder
                        Accessible.name: I18n.online.animeAccessible
                        onActivated: pane.library.selectAnime(currentValue)
                        function sync() { currentIndex = indexOfValue(pane.library.selectedAnime) }
                        onModelChanged: sync()
                        Component.onCompleted: sync()
                    }
                }

                // 剧集下拉行
                ColumnLayout {
                    visible: pane.library.selectedAnime.length > 0
                    Layout.fillWidth: true
                    spacing: 4
                    Label {
                        text: I18n.online.episodeLabel
                        font.pixelSize: Ui.captionSize
                        font.weight: Font.DemiBold
                        color: Ui.textColor
                    }
                    AppComboBox {
                        id: episodeBox
                        objectName: "episodeChoice"
                        Layout.fillWidth: true
                        model: pane.library.episodes
                        textRole: "title"
                        valueRole: "id"
                        displayText: currentIndex >= 0 ? currentText : I18n.online.episodePlaceholder
                        Accessible.name: I18n.online.episodeAccessible
                        onActivated: pane.library.selectEpisode(currentValue)
                        function sync() { currentIndex = indexOfValue(pane.library.selectedEpisode) }
                        onModelChanged: sync()
                        Component.onCompleted: sync()
                    }
                }

                // 下载与载入动作行
                Flow {
                    visible: pane.library.selectedEpisode.length > 0
                    Layout.fillWidth: true
                    spacing: 8

                    Button {
                        text: pane.library.selectedCached ? I18n.online.loadCachedBtn : I18n.online.downloadAndLoadBtn
                        highlighted: true
                        enabled: !pane.library.busy && pane.library.selectedEpisode.length > 0
                        onClicked: pane.library.downloadEpisode(false)
                    }

                    Button {
                        text: I18n.online.redownloadBtn
                        visible: pane.library.selectedCached
                        enabled: !pane.library.busy && pane.library.selectedEpisode.length > 0
                        onClicked: pane.library.downloadEpisode(true)
                    }
                }
            }
        }

        Connections {
            target: pane.library
            function onChanged() { animeBox.sync(); episodeBox.sync() }
        }
    }

    // 4. 进度条与忙碌状态
    RowLayout {
        visible: pane.library.busy
        Layout.fillWidth: true
        spacing: 8

        ProgressBar {
            Layout.fillWidth: true
            indeterminate: pane.library.progress < 0
            from: 0
            to: 100
            value: Math.max(0, pane.library.progress)
            Accessible.name: I18n.online.progressAccessible
        }

        Button {
            text: I18n.online.cancelBtn
            onClicked: pane.library.cancel()
        }
    }

    // 5. 提示与状态信息
    Control {
        visible: pane.library.status.length > 0 && !pane.library.busy && pane.library.status !== "请输入动画名称" && pane.library.status !== "请选择动画" && pane.library.status !== "请选择剧集"
        Layout.fillWidth: true
        leftPadding: 12
        rightPadding: 12
        topPadding: 8
        bottomPadding: 8

        background: Rectangle {
            radius: Ui.cardRadius
            color: Ui.dark ? Qt.rgba(1, 1, 1, 0.04) : Qt.rgba(0, 0, 0, 0.03)
            border.width: 1
            border.color: Ui.cardBorder
        }

        contentItem: Label {
            text: pane.library.status
            font.pixelSize: Ui.captionSize
            color: Ui.secondaryText
            wrapMode: Text.WordWrap
            Accessible.name: I18n.online.statusPrefix + text
        }
    }

    Label {
        visible: pane.library.error.length > 0
        text: pane.library.error
        font.pixelSize: Ui.captionSize
        color: Ui.dark ? "#FF99A4" : "#D13438"
        wrapMode: Text.WordWrap
        Layout.fillWidth: true
        Accessible.name: I18n.online.errorPrefix + text
    }
}
