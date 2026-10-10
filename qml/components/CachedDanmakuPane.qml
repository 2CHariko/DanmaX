import QtQuick
import QtQuick.Controls.FluentWinUI3
import QtQuick.Layouts
import "../theme"
import "../i18n"

ColumnLayout {
    id: pane
    required property QtObject library
    property string deleteKey: ""
    property string deleteTitle: ""
    readonly property var filteredEntries: {
        const query = filter.text.trim().toLowerCase()
        return library.cachedEntries.filter(entry => query.length === 0 ||
            (entry.animeTitle + " " + entry.episodeTitle + " " + (entry.server || "")).toLowerCase().includes(query))
    }
    spacing: 10

    // 1. 顶部过滤与刷新工具栏
    RowLayout {
        Layout.fillWidth: true
        spacing: 8

        TextField {
            id: filter
            Layout.fillWidth: true
            placeholderText: I18n.cache.filterPlaceholder
            Accessible.name: I18n.cache.filterAccessible
        }

        Button {
            text: I18n.cache.refreshBtn
            enabled: !pane.library.scanning
            onClicked: pane.library.refreshCache()
        }
    }

    // 2. 统计与扫描状态提示
    RowLayout {
        visible: pane.library.cachedEntries.length > 0 || pane.library.scanning
        Layout.fillWidth: true
        spacing: 8

        Label {
            visible: !pane.library.scanning && pane.library.cachedEntries.length > 0
            text: I18n.format(I18n.cache.itemCountFormat, pane.library.cachedEntries.length)
            font.pixelSize: Ui.captionSize
            color: Ui.secondaryText
        }

        Item { Layout.fillWidth: true }

        Label {
            visible: pane.library.scanning
            text: I18n.cache.scanningHint
            font.pixelSize: Ui.captionSize
            color: Ui.accentColor
            Accessible.name: text
        }
    }

    // 3. 空状态卡片
    Control {
        visible: !pane.library.scanning && pane.filteredEntries.length === 0
        Layout.fillWidth: true
        leftPadding: Ui.cardPaddingX
        rightPadding: Ui.cardPaddingX
        topPadding: 20
        bottomPadding: 20

        background: Rectangle {
            radius: Ui.cardRadius
            color: Ui.cardBackground
            border.width: 1
            border.color: Ui.cardBorder
        }

        contentItem: ColumnLayout {
            spacing: 6
            Label {
                text: filter.text.trim().length > 0 ? I18n.cache.emptyMatched : I18n.cache.emptyHint
                font.pixelSize: Ui.bodySize
                font.weight: Font.DemiBold
                color: Ui.secondaryText
                horizontalAlignment: Text.AlignHCenter
                Layout.fillWidth: true
            }
        }
    }

    // 4. 独立微卡片式缓存列表
    ListView {
        id: cachedList
        objectName: "cachedDanmakuList"
        Layout.fillWidth: true
        Layout.preferredHeight: pane.filteredEntries.length > 0 ? Math.min(320 * Ui.textScale, contentHeight) : 0
        clip: true
        spacing: 6
        model: pane.filteredEntries
        keyNavigationEnabled: true
        boundsBehavior: Flickable.StopAtBounds

        ScrollBar.vertical: ScrollBar {
            id: cacheScrollBar
            Accessible.name: I18n.cache.scrollBarAccessible
        }

        delegate: Control {
            id: itemDelegate
            required property var modelData
            required property int index
            width: Math.max(0, cachedList.width - (cacheScrollBar.visible ? cacheScrollBar.width + Ui.scrollGap : 0))
            leftPadding: Ui.cardPaddingX
            rightPadding: Ui.cardPaddingX
            topPadding: 10
            bottomPadding: 10

            background: Rectangle {
                radius: Ui.cardRadius
                color: itemDelegate.hovered ? Ui.cardBackgroundHover : Ui.cardBackground
                border.width: 1
                border.color: Ui.cardBorder
                Behavior on color { ColorAnimation { duration: 100 } }
            }

            contentItem: RowLayout {
                spacing: 12

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4

                    Label {
                        text: itemDelegate.modelData.animeTitle + " · " + itemDelegate.modelData.episodeTitle
                        font.weight: Font.DemiBold
                        font.pixelSize: Ui.bodySize
                        color: Ui.textColor
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }

                    RowLayout {
                        spacing: 8

                        Rectangle {
                            radius: 9
                            implicitHeight: 18
                            implicitWidth: countBadge.implicitWidth + 12
                            color: Ui.dark ? Qt.rgba(1, 1, 1, 0.06) : Qt.rgba(0, 0, 0, 0.05)
                            border.width: 1
                            border.color: Ui.cardBorder

                            Label {
                                id: countBadge
                                anchors.centerIn: parent
                                text: I18n.format(I18n.cache.itemCountFormat, itemDelegate.modelData.count || 0)
                                font.pixelSize: Ui.captionSize - 1
                                color: Ui.secondaryText
                            }
                        }

                        Label {
                            text: (itemDelegate.modelData.server || I18n.cache.unknownServer) +
                                  (itemDelegate.modelData.downloadedAt ? " · " + new Date(itemDelegate.modelData.downloadedAt).toLocaleString(Qt.locale(), Locale.ShortFormat) : "")
                            font.pixelSize: Ui.captionSize
                            color: Ui.secondaryText
                            elide: Text.ElideMiddle
                            Layout.fillWidth: true
                        }
                    }

                    Label {
                        visible: !itemDelegate.modelData.valid
                        text: itemDelegate.modelData.error || I18n.cache.invalidHint
                        font.pixelSize: Ui.captionSize
                        color: Ui.dark ? "#FF99A4" : "#D13438"
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                        Accessible.name: I18n.cache.errorPrefix + text
                    }
                }

                RowLayout {
                    spacing: 8

                    Button {
                        text: I18n.cache.loadBtn
                        enabled: itemDelegate.modelData.valid && !pane.library.busy
                        highlighted: true
                        Accessible.name: I18n.format(I18n.cache.loadAccessibleFormat, itemDelegate.modelData.animeTitle, itemDelegate.modelData.episodeTitle)
                        onClicked: {
                            cachedList.currentIndex = itemDelegate.index
                            pane.library.loadCached(itemDelegate.modelData.key)
                        }
                    }

                    Button {
                        text: I18n.cache.deleteBtn
                        enabled: !pane.library.busy
                        Accessible.name: I18n.format(I18n.cache.deleteAccessibleFormat, itemDelegate.modelData.animeTitle, itemDelegate.modelData.episodeTitle)
                        onClicked: {
                            cachedList.currentIndex = itemDelegate.index
                            pane.deleteKey = itemDelegate.modelData.key
                            pane.deleteTitle = itemDelegate.modelData.animeTitle + " · " + itemDelegate.modelData.episodeTitle
                            deleteDialog.open()
                        }
                    }
                }
            }
        }
    }

    // 5. 忙碌取消与错误信息
    RowLayout {
        visible: pane.library.busy
        Layout.fillWidth: true
        spacing: 8

        ProgressBar {
            Layout.fillWidth: true
            indeterminate: true
        }

        Button {
            text: I18n.cache.cancelBtn
            onClicked: pane.library.cancel()
        }
    }

    Label {
        visible: pane.library.status.length > 0 && !pane.library.busy
        text: pane.library.status
        font.pixelSize: Ui.captionSize
        color: Ui.secondaryText
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
    }

    Label {
        visible: pane.library.error.length > 0
        text: pane.library.error
        font.pixelSize: Ui.captionSize
        color: Ui.dark ? "#FF99A4" : "#D13438"
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        Accessible.name: I18n.cache.errorPrefix + text
    }

    // 6. 二次确认删除对话框
    Dialog {
        id: deleteDialog
        title: I18n.cache.deleteDialogTitle
        modal: true
        width: Math.min(420, Overlay.overlay ? Overlay.overlay.width - 24 : 400)
        standardButtons: Dialog.Ok | Dialog.Cancel
        contentItem: Label {
            text: pane.deleteTitle + I18n.cache.deleteDialogContentSuffix
            wrapMode: Text.WordWrap
        }
        onAccepted: pane.library.removeCached(pane.deleteKey)
        onClosed: cachedList.forceActiveFocus()
    }
}
