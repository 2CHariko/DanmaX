import QtQuick
import QtQuick.Controls.FluentWinUI3
import QtQuick.Layouts
import DanmaX.Native
import "../theme"

Item {
    id: pane
    required property QtObject backend
    readonly property QtObject recovery: backend.automatic.recovery
    property bool manualSearch: false
    property bool allowLocalXml: false
    property bool presented: false
    property real minimumInset: 16
    property bool titleCopied: false
    readonly property QtObject activeLibrary: manualSearch ? backend.library : backend.automatic.library
    readonly property real inset: Ui.scrollInset(verticalBar.implicitWidth, minimumInset)
    readonly property bool choosingXml: xmlPicker.busy
    property string xmlPickerError: ""
    signal cancelMatching()
    signal openSettings()

    onPresentedChanged: {
        titleCopied = false
        copiedTimer.stop()
        if (!presented) {
            closeChoicePopup()
            xmlPicker.task = -1
            xmlPicker.cancel()
        } else {
            xmlPickerError = ""
            scroll.contentItem.contentY = scroll.contentItem.originY
        }
    }
    Connections {
        target: pane.recovery
        function onChanged() {
            pane.titleCopied = false
            copiedTimer.stop()
        }
    }
    Timer {
        id: copiedTimer
        interval: 1500
        onTriggered: pane.titleCopied = false
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
            id: contentColumn
            width: scroll.availableWidth
            spacing: 10

            // 1. 顶部识别状态横幅
            Control {
                visible: !pane.manualSearch
                Layout.fillWidth: true
                leftPadding: Ui.cardPaddingX
                rightPadding: Ui.cardPaddingX
                topPadding: 10
                bottomPadding: 10

                background: Rectangle {
                    radius: Ui.cardRadius
                    color: Ui.highContrast ? Ui.colors.window
                         : (Ui.dark ? Qt.rgba(234 / 255, 163 / 255, 0, 0.10) : Qt.rgba(188 / 255, 75 / 255, 9 / 255, 0.06))
                    border.width: 1
                    border.color: Ui.highContrast ? Ui.colors.windowText
                                : Qt.rgba(Ui.cautionText.r, Ui.cautionText.g, Ui.cautionText.b, Ui.dark ? 0.45 : 0.35)
                }

                contentItem: GridLayout {
                    id: bannerGrid
                    columns: contentColumn.width < 480 * Ui.textScale ? 1 : 2
                    columnSpacing: 12
                    rowSpacing: 8

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignVCenter
                        spacing: 10

                        FluentIcon {
                            visible: !Ui.highContrast
                            source: Ui.icon("info")
                            Layout.preferredWidth: 20
                            Layout.preferredHeight: 20
                            Layout.alignment: Qt.AlignVCenter
                        }

                        Label {
                            Layout.fillWidth: true
                            text: pane.recovery.reason
                            font.pixelSize: Ui.bodySize
                            font.weight: Font.DemiBold
                            color: Ui.textColor
                            wrapMode: Text.WordWrap
                            Accessible.role: Accessible.AlertMessage
                        }
                    }

                    Rectangle {
                        visible: pane.recovery.pauseStatus.length > 0
                        Layout.alignment: bannerGrid.columns === 1 ? (Qt.AlignLeft | Qt.AlignVCenter) : (Qt.AlignRight | Qt.AlignVCenter)
                        Layout.maximumWidth: Math.max(80, contentColumn.width - Ui.cardPaddingX * 2)
                        radius: 12
                        implicitHeight: Math.round(24 * Ui.textScale)
                        implicitWidth: Math.min(Layout.maximumWidth, pauseLabel.implicitWidth + 16)
                        color: Ui.highContrast ? Ui.colors.window
                             : (Ui.dark ? Qt.rgba(1, 1, 1, 0.08) : Qt.rgba(0, 0, 0, 0.05))
                        border.width: 1
                        border.color: Ui.cardBorder

                        Label {
                            id: pauseLabel
                            anchors.centerIn: parent
                            width: Math.min(implicitWidth, parent.width - 16)
                            text: pane.recovery.pauseStatus
                            font.pixelSize: Ui.captionSize
                            color: Ui.secondaryText
                            elide: Text.ElideRight
                        }
                    }
                }
            }

            // 2. 当前播放媒体信息卡片（标题与复制操作同行）
            Control {
                visible: !pane.manualSearch
                Layout.fillWidth: true
                leftPadding: Ui.cardPaddingX
                rightPadding: Ui.cardPaddingX
                topPadding: 10
                bottomPadding: 10

                background: Rectangle {
                    radius: Ui.cardRadius
                    color: Ui.cardBackground
                    border.width: 1
                    border.color: Ui.cardBorder
                }

                contentItem: ColumnLayout {
                    spacing: 6

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Label {
                            Layout.fillWidth: true
                            text: "当前播放文件名／媒体标题"
                            font.pixelSize: Ui.captionSize
                            font.weight: Font.DemiBold
                            color: Ui.secondaryText
                            elide: Text.ElideRight
                        }

                        Button {
                            objectName: "copyRecoveryFileName"
                            text: pane.titleCopied ? "已复制" : "复制完整文件名"
                            onClicked: {
                                pane.backend.copyRecoveryTitle()
                                pane.titleCopied = true
                                copiedTimer.restart()
                            }
                        }
                    }

                    TextArea {
                        objectName: "recoveryFileName"
                        Layout.fillWidth: true
                        text: pane.recovery.title
                        readOnly: true
                        selectByMouse: true
                        wrapMode: TextEdit.WrapAnywhere
                        Accessible.name: "当前播放文件名，可选择并复制"
                    }
                }
            }

            // 3. 在线匹配弹幕主卡片
            Control {
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
                    spacing: 8

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        FluentIcon {
                            visible: !Ui.highContrast
                            source: Ui.icon("cloud")
                            Layout.preferredWidth: 20
                            Layout.preferredHeight: 20
                            Layout.alignment: Qt.AlignVCenter
                        }

                        Label {
                            text: "在线匹配弹幕"
                            font.pixelSize: Ui.sectionHeaderSize
                            font.weight: Font.DemiBold
                            color: Ui.textColor
                        }

                        Label {
                            Layout.fillWidth: true
                            Layout.leftMargin: 4
                            text: pane.manualSearch
                                ? "搜索并确认动画与剧集后载入"
                                : "输入番名搜索并选集载入"
                            font.pixelSize: Ui.captionSize
                            color: Ui.secondaryText
                            elide: Text.ElideRight
                        }
                    }

                    OnlineDanmakuPane {
                        id: online
                        nested: true
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

            // 4. 本地 XML 兜底卡片
            SettingsRow {
                visible: pane.allowLocalXml
                iconSource: Ui.icon("open")
                title: "使用本地 XML 弹幕"
                description: "已有本地下载好的弹幕文件时，可直接导入并载入"

                Button {
                    objectName: "chooseRecoveryXml"
                    text: "选择本地 XML"
                    enabled: pane.presented && pane.backend.canSelectSource && !xmlPicker.busy
                    onClicked: {
                        pane.xmlPickerError = ""
                        xmlPicker.task = pane.backend.taskId
                        xmlPicker.open(pane.Window.window)
                    }
                }
            }
            Label {
                visible: pane.allowLocalXml && pane.xmlPickerError.length > 0
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: pane.xmlPickerError
            }

            // 5. 底部规则脚注
            Label {
                visible: !pane.manualSearch
                Layout.fillWidth: true
                text: "提示：仅当本次操作自动暂停了播放器，完成或取消时才恢复播放；取消后下一集仍会自动识别。"
                font.pixelSize: Ui.captionSize
                color: Ui.secondaryText
                wrapMode: Text.WordWrap
            }
        }
    }
    RecoveryXmlPicker {
        id: xmlPicker
        objectName: "recoveryXmlPicker"
        property double task: -1
        onAccepted: function(file) {
            if (pane.presented && task === pane.backend.taskId && pane.backend.canSelectSource)
                pane.backend.loadFile(file)
        }
        onFailed: function(message) { pane.xmlPickerError = message }
    }
}
