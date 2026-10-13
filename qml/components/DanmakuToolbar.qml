import QtQuick
import QtQuick.Controls.FluentWinUI3
import QtQuick.Layouts
import "../theme"
import "../i18n"

Item {
    id: root
    objectName: "danmakuToolbar"
    required property QtObject backend
    property bool presentationAllowed: false
    // Disable input together with presentation, including held controls.
    enabled: presentationAllowed
    property bool pinned: false
    property bool autoExpanded: false
    readonly property bool popoverOpen: opacityPopup.visible
    readonly property bool isHovered: backend.toolbarPointer.hovered
    readonly property bool interactionHeld: opacitySlider.pressed || toggleDanmakuBtn.pressed
        || stepDecBtn.pressed || offsetResetBtn.pressed || stepIncBtn.pressed || opacityBtn.pressed
        || fontDecBtn.pressed || fontIncBtn.pressed || clearBtn.pressed || debugBtn.pressed
        || settingsBtn.pressed || pinBtn.pressed
    readonly property bool expanded: presentationAllowed && (pinned || autoExpanded || isHovered || interactionHeld)
    readonly property bool transitionRunning: slideAnimation.running || fadeAnimation.running

    // 暴露所需的实际内容尺寸给宿主微型窗口
    readonly property int targetWidth: barCard.width + 12
    readonly property int targetHeight: 4 + barCard.height + (popoverOpen ? opacityPopup.height + 8 : 4)

    anchors.fill: parent

    // An item popup is clamped into this short HWND and can cover its button.
    // Keep the official ToolTip style in a non-activating native tooltip window.
    component ToolbarTip: ToolTip {
        required property bool trigger
        popupType: Popup.Window
        visible: root.expanded && trigger && !root.popoverOpen
        delay: 400
        y: parent.height + 4 - topInset
    }

    // All interaction changes use one exit policy, including pinned popovers.
    function updateCollapse() {
        collapseTimer.stop()
        if (!presentationAllowed) {
            autoExpanded = false
            opacityPopup.visible = false
            return
        }
        if (isHovered) autoExpanded = true
        if (isHovered || interactionHeld) return
        if (popoverOpen || (!pinned && autoExpanded)) collapseTimer.start()
    }
    onPresentationAllowedChanged: updateCollapse()
    onIsHoveredChanged: updateCollapse()
    onInteractionHeldChanged: updateCollapse()
    onPinnedChanged: updateCollapse()
    onPopoverOpenChanged: updateCollapse()
    onAutoExpandedChanged: updateCollapse()

    Timer {
        id: collapseTimer
        interval: 1000 // 鼠标离开后 1 秒平滑自动收起
        repeat: false
        onTriggered: {
            if (!root.presentationAllowed || root.isHovered || root.interactionHeld) return
            opacityPopup.visible = false
            if (!root.pinned) root.autoExpanded = false
            root.updateCollapse()
        }
    }

    // 主工具栏底板（WinUI 3 标准自然微圆角 8px，100% 实体面板色，高 42px）
    Rectangle {
        id: barCard
        anchors.top: parent.top
        anchors.topMargin: root.expanded ? 4 : -56
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
            NumberAnimation { id: slideAnimation; duration: 180; easing.type: Easing.OutCubic }
        }
        Behavior on opacity {
            NumberAnimation { id: fadeAnimation; duration: 160 }
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
                ToolbarTip {
                    trigger: parent.hovered
                    text: parent.checked ? I18n.toolbar.danmakuShown : I18n.toolbar.danmakuHidden
                }
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
                    ToolbarTip {
                        trigger: parent.hovered
                        text: I18n.toolbar.offsetDec
                    }
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
                    ToolbarTip {
                        trigger: parent.hovered
                        text: I18n.toolbar.offsetReset
                    }
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
                    ToolbarTip {
                        trigger: parent.hovered
                        text: I18n.toolbar.offsetInc
                    }
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
                objectName: "toolbarOpacityButton"
                flat: true
                leftPadding: 6; rightPadding: 6; topPadding: 2; bottomPadding: 2
                Layout.preferredHeight: 34
                icon.source: Ui.icon("appearance")
                icon.width: 16
                icon.height: 16
                text: Math.round((root.backend && root.backend.settings.values.opacity ? root.backend.settings.values.opacity : 0.85) * 100) + "%"
                display: AbstractButton.TextBesideIcon
                Accessible.name: I18n.toolbar.opacityTitle
                ToolbarTip {
                    trigger: parent.hovered
                    text: I18n.toolbar.opacityTitle
                }
                onClicked: opacityPopup.visible = !opacityPopup.visible
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
                    id: fontDecBtn
                    flat: true
                    leftPadding: 2; rightPadding: 2; topPadding: 2; bottomPadding: 2
                    Layout.preferredWidth: 26
                    Layout.preferredHeight: 34
                    text: "A-"
                    font.pixelSize: 12
                    font.bold: true
                    Accessible.name: I18n.toolbar.fontSizeDec
                    ToolbarTip {
                        trigger: parent.hovered
                        text: I18n.toolbar.fontSizeDec
                    }
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
                    ToolbarTip {
                        trigger: fontMouse.containsMouse
                        text: I18n.toolbar.fontSizeTitle
                    }
                    MouseArea {
                        id: fontMouse
                        anchors.fill: parent
                        hoverEnabled: true
                    }
                }

                Button {
                    id: fontIncBtn
                    flat: true
                    leftPadding: 2; rightPadding: 2; topPadding: 2; bottomPadding: 2
                    Layout.preferredWidth: 26
                    Layout.preferredHeight: 34
                    text: "A+"
                    font.pixelSize: 12
                    font.bold: true
                    Accessible.name: I18n.toolbar.fontSizeInc
                    ToolbarTip {
                        trigger: parent.hovered
                        text: I18n.toolbar.fontSizeInc
                    }
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
                ToolbarTip {
                    trigger: parent.hovered
                    text: I18n.toolbar.clearActive
                }
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
                ToolbarTip {
                    trigger: parent.hovered
                    text: I18n.toolbar.toggleDebug
                }
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
                ToolbarTip {
                    trigger: parent.hovered
                    text: I18n.toolbar.openMainWindow
                }
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
                objectName: "toolbarPinButton"
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
                ToolbarTip {
                    objectName: "toolbarPinTooltip"
                    trigger: parent.hovered
                    text: root.pinned ? I18n.toolbar.unpinToolbar : I18n.toolbar.pinToolbar
                }
                onClicked: root.pinned = !root.pinned
            }
        }
    }

    // 不透明度微调浮层 Popover（实体不透明面板色）
    Rectangle {
        id: opacityPopup
        objectName: "toolbarOpacityPopup"
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
                objectName: "toolbarOpacitySlider"
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
    }
}
