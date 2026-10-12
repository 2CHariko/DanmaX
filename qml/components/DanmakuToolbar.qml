import QtQuick
import QtQuick.Controls.FluentWinUI3
import QtQuick.Layouts
import "../theme"
import "../i18n"

Item {
    id: root
    required property QtObject backend
    property bool pinned: false
    property bool autoExpanded: false
    readonly property bool popoverOpen: opacityPopup.visible
    readonly property bool isHovered: (backend && backend.overlayHovered) || handleArea.containsMouse || barArea.containsMouse
    readonly property bool expanded: pinned || autoExpanded || isHovered || popoverOpen

    // 居中放置在屏幕顶部
    width: expanded ? barCard.width : handleContainer.width
    height: expanded ? (barCard.y + barCard.height + (popoverOpen ? opacityPopup.height + 8 : 0)) : 8
    x: Math.round((parent.width - width) / 2)
    y: 0

    // 监听悬停状态变化，驱动自动展开与延迟收起
    onIsHoveredChanged: {
        if (isHovered) {
            collapseTimer.stop()
            autoExpanded = true
        } else {
            if (!pinned && !popoverOpen) {
                collapseTimer.restart()
            }
        }
    }

    Timer {
        id: collapseTimer
        interval: 1000 // 鼠标离开后 1 秒平滑自动收起
        repeat: false
        onTriggered: {
            if (!root.pinned && !root.popoverOpen && !root.isHovered) {
                root.autoExpanded = false
            }
        }
    }

    // 同步给 C++ 命中测试层
    function updateHitRects() {
        if (!backend) return
        var rects = []
        if (!expanded) {
            rects.push({
                x: root.x,
                y: 0,
                width: root.width,
                height: 8
            })
        } else {
            // 工具栏卡片区域（WinUI 3 自然微圆角矩形）
            rects.push({
                x: root.x + barCard.x,
                y: root.y + barCard.y,
                width: barCard.width,
                height: barCard.height
            })
            // Popover 浮层区域
            if (opacityPopup.visible) {
                rects.push({
                    x: root.x + opacityPopup.x,
                    y: root.y + opacityPopup.y,
                    width: opacityPopup.width,
                    height: opacityPopup.height
                })
            }
        }
        backend.setOverlayInteractiveRects(rects)
    }

    onExpandedChanged: Qt.callLater(updateHitRects)
    onXChanged: Qt.callLater(updateHitRects)
    onWidthChanged: Qt.callLater(updateHitRects)
    onHeightChanged: Qt.callLater(updateHitRects)
    Component.onCompleted: Qt.callLater(updateHitRects)

    // 顶部收起时的微型把手（感应区）
    Item {
        id: handleContainer
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        width: 80
        height: 8
        visible: !root.expanded

        Rectangle {
            id: handlePill
            anchors.centerIn: parent
            width: 72
            height: 4
            radius: 2
            color: Ui.dark ? Qt.rgba(1, 1, 1, 0.45) : Qt.rgba(0, 0, 0, 0.35)
        }

        MouseArea {
            id: handleArea
            anchors.fill: parent
            hoverEnabled: true
            Accessible.role: Accessible.Button
            Accessible.name: I18n.toolbar.handleTooltip
            ToolTip.visible: containsMouse
            ToolTip.text: I18n.toolbar.handleTooltip
            ToolTip.delay: 300
        }
    }

    // 主工具栏底板（WinUI 3 标准自然微圆角 8px，100% 实体面板色，高 42px）
    Rectangle {
        id: barCard
        anchors.top: parent.top
        anchors.topMargin: root.expanded ? 6 : -56
        anchors.horizontalCenter: parent.horizontalCenter
        width: contentLayout.implicitWidth + 20
        height: 42
        radius: 8
        opacity: root.expanded ? 1.0 : 0.0
        visible: opacity > 0.01

        color: Ui.dark ? "#202020" : "#F6F6F6"
        border.width: 1
        border.color: Ui.cardBorder

        Behavior on anchors.topMargin {
            NumberAnimation { duration: 180; easing.type: Easing.OutCubic }
        }
        Behavior on opacity {
            NumberAnimation { duration: 160 }
        }

        MouseArea {
            id: barArea
            anchors.fill: parent
            hoverEnabled: true
            acceptedButtons: Qt.NoButton
        }

        RowLayout {
            id: contentLayout
            anchors.centerIn: parent
            spacing: 2

            // 1. 弹幕显隐切换（微软官方 Fluent eye / eye-off 图标）
            Button {
                id: toggleDanmakuBtn
                flat: true
                leftPadding: 4; rightPadding: 4; topPadding: 4; bottomPadding: 4
                Layout.preferredWidth: 34
                Layout.preferredHeight: 34
                checkable: true
                checked: root.backend ? root.backend.danmakuVisible : true
                icon.source: Ui.icon(checked ? "eye" : "eye-off")
                icon.width: 18
                icon.height: 18
                display: AbstractButton.IconOnly
                Accessible.name: I18n.toolbar.toggleDanmaku
                ToolTip.visible: hovered
                ToolTip.text: checked ? I18n.toolbar.danmakuShown : I18n.toolbar.danmakuHidden
                ToolTip.delay: 400
                onClicked: if (root.backend) root.backend.danmakuVisible = !root.backend.danmakuVisible
            }

            Rectangle {
                Layout.preferredWidth: 1
                Layout.preferredHeight: 18
                color: Ui.cardBorder
            }

            // 2. 时间轴微调组（官方 Fluent chevron-left / timer / chevron-right）
            RowLayout {
                spacing: 1

                Button {
                    id: stepDecBtn
                    flat: true
                    leftPadding: 2; rightPadding: 2; topPadding: 2; bottomPadding: 2
                    Layout.preferredWidth: 28
                    Layout.preferredHeight: 34
                    icon.source: Ui.icon("chevron-left")
                    icon.width: 16
                    icon.height: 16
                    display: AbstractButton.IconOnly
                    Accessible.name: I18n.toolbar.offsetDec
                    ToolTip.visible: hovered
                    ToolTip.text: I18n.toolbar.offsetDec
                    ToolTip.delay: 400
                    onClicked: if (root.backend) root.backend.stepTimeOffset(-0.5)
                }

                Button {
                    id: offsetResetBtn
                    flat: true
                    leftPadding: 6; rightPadding: 6; topPadding: 2; bottomPadding: 2
                    Layout.preferredHeight: 34
                    readonly property real offsetVal: root.backend && root.backend.settings.values.timeOffset ? root.backend.settings.values.timeOffset : 0.0
                    icon.source: Ui.icon("timer")
                    icon.width: 16
                    icon.height: 16
                    text: (offsetVal >= 0 ? "+" : "") + offsetVal.toFixed(1) + "s"
                    display: AbstractButton.TextBesideIcon
                    Accessible.name: I18n.toolbar.offsetReset
                    ToolTip.visible: hovered
                    ToolTip.text: I18n.toolbar.offsetReset
                    ToolTip.delay: 400
                    onClicked: if (root.backend) root.backend.settings.setValue("timeOffset", 0.0)
                }

                Button {
                    id: stepIncBtn
                    flat: true
                    leftPadding: 2; rightPadding: 2; topPadding: 2; bottomPadding: 2
                    Layout.preferredWidth: 28
                    Layout.preferredHeight: 34
                    icon.source: Ui.icon("chevron-right")
                    icon.width: 16
                    icon.height: 16
                    display: AbstractButton.IconOnly
                    Accessible.name: I18n.toolbar.offsetInc
                    ToolTip.visible: hovered
                    ToolTip.text: I18n.toolbar.offsetInc
                    ToolTip.delay: 400
                    onClicked: if (root.backend) root.backend.stepTimeOffset(0.5)
                }
            }

            Rectangle {
                Layout.preferredWidth: 1
                Layout.preferredHeight: 18
                color: Ui.cardBorder
            }

            // 3. 不透明度调节（官方 Fluent appearance 图标 + 百分比数值）
            Button {
                id: opacityBtn
                flat: true
                leftPadding: 6; rightPadding: 6; topPadding: 2; bottomPadding: 2
                Layout.preferredHeight: 34
                icon.source: Ui.icon("appearance")
                icon.width: 16
                icon.height: 16
                text: Math.round((root.backend && root.backend.settings.values.opacity ? root.backend.settings.values.opacity : 0.85) * 100) + "%"
                display: AbstractButton.TextBesideIcon
                Accessible.name: I18n.toolbar.opacityTitle
                ToolTip.visible: hovered && !opacityPopup.visible
                ToolTip.text: I18n.toolbar.opacityTitle
                ToolTip.delay: 400
                onClicked: {
                    opacityPopup.visible = !opacityPopup.visible
                    root.updateHitRects()
                }
            }

            Rectangle {
                Layout.preferredWidth: 1
                Layout.preferredHeight: 18
                color: Ui.cardBorder
            }

            // 4. 字号微调组（A- / 数值 / A+）
            RowLayout {
                spacing: 1

                Button {
                    flat: true
                    leftPadding: 2; rightPadding: 2; topPadding: 2; bottomPadding: 2
                    Layout.preferredWidth: 26
                    Layout.preferredHeight: 34
                    text: "A-"
                    font.pixelSize: 12
                    font.bold: true
                    Accessible.name: I18n.toolbar.fontSizeDec
                    ToolTip.visible: hovered
                    ToolTip.text: I18n.toolbar.fontSizeDec
                    ToolTip.delay: 400
                    onClicked: if (root.backend) root.backend.stepFontSize(-2)
                }

                Label {
                    text: (root.backend && root.backend.settings.values.fontSize ? root.backend.settings.values.fontSize : 24)
                    font.pixelSize: Ui.captionSize
                    font.bold: true
                    color: Ui.textColor
                    Layout.alignment: Qt.AlignVCenter
                    Layout.preferredWidth: 20
                    horizontalAlignment: Text.AlignHCenter
                    ToolTip.visible: fontMouse.containsMouse
                    ToolTip.text: I18n.toolbar.fontSizeTitle
                    ToolTip.delay: 400
                    MouseArea {
                        id: fontMouse
                        anchors.fill: parent
                        hoverEnabled: true
                    }
                }

                Button {
                    flat: true
                    leftPadding: 2; rightPadding: 2; topPadding: 2; bottomPadding: 2
                    Layout.preferredWidth: 26
                    Layout.preferredHeight: 34
                    text: "A+"
                    font.pixelSize: 12
                    font.bold: true
                    Accessible.name: I18n.toolbar.fontSizeInc
                    ToolTip.visible: hovered
                    ToolTip.text: I18n.toolbar.fontSizeInc
                    ToolTip.delay: 400
                    onClicked: if (root.backend) root.backend.stepFontSize(2)
                }
            }

            Rectangle {
                Layout.preferredWidth: 1
                Layout.preferredHeight: 18
                color: Ui.cardBorder
            }

            // 5. 一键清屏（官方 Fluent delete 图标）
            Button {
                id: clearBtn
                flat: true
                leftPadding: 4; rightPadding: 4; topPadding: 4; bottomPadding: 4
                Layout.preferredWidth: 34
                Layout.preferredHeight: 34
                icon.source: Ui.icon("delete")
                icon.width: 18
                icon.height: 18
                display: AbstractButton.IconOnly
                Accessible.name: I18n.toolbar.clearActive
                ToolTip.visible: hovered
                ToolTip.text: I18n.toolbar.clearActive
                ToolTip.delay: 400
                onClicked: if (root.backend) root.backend.clearActiveDanmaku()
            }

            // 6. 性能/HUD 调试指标开关（官方 Fluent info 图标）
            Button {
                id: debugBtn
                flat: true
                leftPadding: 4; rightPadding: 4; topPadding: 4; bottomPadding: 4
                Layout.preferredWidth: 34
                Layout.preferredHeight: 34
                checkable: true
                checked: root.backend && root.backend.settings.values.debug ? true : false
                icon.source: Ui.icon("info")
                icon.width: 18
                icon.height: 18
                display: AbstractButton.IconOnly
                Accessible.name: I18n.toolbar.toggleDebug
                ToolTip.visible: hovered
                ToolTip.text: I18n.toolbar.toggleDebug
                ToolTip.delay: 400
                onClicked: {
                    if (root.backend) {
                        var next = !root.backend.settings.values.debug
                        root.backend.settings.setValue("debug", next)
                    }
                }
            }

            // 7. 打开主控制面板（官方 Fluent settings 图标）
            Button {
                id: settingsBtn
                flat: true
                leftPadding: 4; rightPadding: 4; topPadding: 4; bottomPadding: 4
                Layout.preferredWidth: 34
                Layout.preferredHeight: 34
                icon.source: Ui.icon("settings")
                icon.width: 18
                icon.height: 18
                display: AbstractButton.IconOnly
                Accessible.name: I18n.toolbar.openMainWindow
                ToolTip.visible: hovered
                ToolTip.text: I18n.toolbar.openMainWindow
                ToolTip.delay: 400
                onClicked: if (root.backend) root.backend.showMainWindow()
            }

            Rectangle {
                Layout.preferredWidth: 1
                Layout.preferredHeight: 18
                color: Ui.cardBorder
            }

            // 8. 图钉固定常驻（官方 Fluent pin / pin-off 图标）
            Button {
                id: pinBtn
                flat: true
                leftPadding: 4; rightPadding: 4; topPadding: 4; bottomPadding: 4
                Layout.preferredWidth: 34
                Layout.preferredHeight: 34
                checkable: true
                checked: root.pinned
                icon.source: Ui.icon(root.pinned ? "pin" : "pin-off")
                icon.width: 18
                icon.height: 18
                display: AbstractButton.IconOnly
                Accessible.name: root.pinned ? I18n.toolbar.unpinToolbar : I18n.toolbar.pinToolbar
                ToolTip.visible: hovered
                ToolTip.text: root.pinned ? I18n.toolbar.unpinToolbar : I18n.toolbar.pinToolbar
                ToolTip.delay: 400
                onClicked: {
                    root.pinned = !root.pinned
                    root.updateHitRects()
                }
            }
        }
    }

    // 不透明度微调浮层 Popover（实体不透明面板色）
    Rectangle {
        id: opacityPopup
        visible: false
        anchors.top: barCard.bottom
        anchors.topMargin: 4
        anchors.horizontalCenter: parent.horizontalCenter
        width: 220
        height: 48
        radius: 8
        color: Ui.dark ? "#262626" : "#FFFFFF"
        border.width: 1
        border.color: Ui.cardBorder

        RowLayout {
            anchors.fill: parent
            anchors.margins: 10
            spacing: 8

            Image {
                source: Ui.icon("appearance")
                Layout.preferredWidth: 18
                Layout.preferredHeight: 18
                fillMode: Image.PreserveAspectFit
                Layout.alignment: Qt.AlignVCenter
            }

            Slider {
                id: opacitySlider
                Layout.fillWidth: true
                from: 0.05
                to: 1.0
                stepSize: 0.05
                value: root.backend && root.backend.settings.values.opacity ? root.backend.settings.values.opacity : 0.85
                onMoved: {
                    if (root.backend) {
                        root.backend.settings.setValue("opacity", Math.round(value * 100) / 100)
                    }
                }
            }

            Label {
                text: Math.round(opacitySlider.value * 100) + "%"
                font.pixelSize: Ui.captionSize
                color: Ui.textColor
                Layout.preferredWidth: 32
            }
        }

        onVisibleChanged: root.updateHitRects()
    }
}
