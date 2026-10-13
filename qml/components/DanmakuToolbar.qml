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
    property real maximumWidth: 1920
    readonly property bool popoverOpen: opacityPopup.visible || seekPopup.visible
    readonly property bool isHovered: backend.toolbarPointer.hovered
    readonly property bool interactionHeld: opacitySlider.pressed || seekSlider.pressed
        || playbackBtn.pressed || exitBtn.pressed || toggleDanmakuBtn.pressed
        || stepDecBtn.pressed || offsetResetBtn.pressed || stepIncBtn.pressed || opacityBtn.pressed
        || fontDecBtn.pressed || fontIncBtn.pressed || clearBtn.pressed || debugBtn.pressed
        || settingsBtn.pressed || pinBtn.pressed
    readonly property bool expanded: presentationAllowed && (pinned || autoExpanded || isHovered || interactionHeld)
    readonly property bool transitionRunning: slideAnimation.running || fadeAnimation.running

    // 暴露所需的实际内容尺寸给宿主微型窗口
    readonly property real activationWidth: barCard.width
    readonly property int targetWidth: Math.max(barCard.width,
        seekPopup.visible ? seekPopup.width : opacityPopup.visible ? opacityPopup.width : 0) + 12
    readonly property int targetHeight: 4 + barCard.height +
        (seekPopup.visible ? seekPopup.height + 8 : opacityPopup.visible ? opacityPopup.height + 8 : 4)

    anchors.fill: parent

    function formatTime(value) {
        const seconds = Math.max(0, Math.floor(value || 0))
        const minutes = Math.floor(seconds / 60)
        return (seconds >= 3600 ? Math.floor(minutes / 60) + ":" + String(minutes % 60).padStart(2, "0")
                               : String(minutes).padStart(2, "0")) + ":" + String(seconds % 60).padStart(2, "0")
    }
    function closePopovers() {
        seekSlider.pendingSeek = false
        seekPopup.visible = false
        opacityPopup.visible = false
    }

    // An item popup is clamped into this short HWND and can cover its button.
    // Keep the official ToolTip style in a non-activating native tooltip window.
    component ToolbarTip: ToolTip {
        id: tipControl
        objectName: "toolbarTip"
        required property bool trigger
        popupType: Popup.Window
        width: implicitWidth
        contentItem: Text {
            text: tipControl.text
            font: tipControl.font
            color: tipControl.palette.toolTipText
            textFormat: Text.PlainText
            wrapMode: Text.NoWrap
        }
        visible: root.expanded && trigger && !root.popoverOpen && !root.interactionHeld
        delay: 400
        y: parent.height + 4 - topInset
    }

    // All interaction changes use one exit policy, including pinned popovers.
    function updateCollapse() {
        collapseTimer.stop()
        if (!presentationAllowed) {
            autoExpanded = false
            closePopovers()
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
            root.closePopovers()
            if (!root.pinned) root.autoExpanded = false
            root.updateCollapse()
        }
    }

    // 主工具栏底板（WinUI 3 标准自然微圆角 8px，100% 实体面板色，高 42px）
    Rectangle {
        id: barCard
        anchors.top: parent.top
        anchors.topMargin: root.expanded ? 4 : -height - 8
        anchors.horizontalCenter: parent.horizontalCenter
        width: Math.min(contentLayout.naturalWidth + 20, Math.max(0, root.maximumWidth - 12))
        height: contentLayout.height + 8
        radius: 8
        opacity: root.expanded ? 1.0 : 0.0

        color: Ui.dark ? "#202020" : "#F6F6F6"
        border.width: 1
        border.color: Ui.cardBorder

        Behavior on anchors.topMargin {
            NumberAnimation { id: slideAnimation; duration: 180; easing.type: Easing.OutCubic }
        }
        Behavior on opacity {
            NumberAnimation { id: fadeAnimation; duration: 160 }
        }

        Flow {
            id: contentLayout
            objectName: "toolbarFlow"
            x: 10
            y: 4
            width: parent.width - 20
            readonly property real naturalWidth: {
                let size = 0
                let count = 0
                for (let child of children) {
                    if (child === playbackBtn && !root.backend.manualMode) continue
                    size += child.implicitWidth || child.width
                    count++
                }
                return size + Math.max(0, count - 1) * spacing
            }
            spacing: 2

            Button {
                id: playbackBtn
                objectName: "toolbarPlaybackButton"
                visible: root.backend.manualMode
                flat: true
                implicitWidth: 34; implicitHeight: 34
                leftPadding: 4; rightPadding: 4; topPadding: 4; bottomPadding: 4
                icon.source: Ui.icon(root.backend.playing ? "pause" : "play")
                icon.width: 18; icon.height: 18
                display: AbstractButton.IconOnly
                Accessible.name: root.backend.completed ? I18n.toolbar.replayPlayback
                    : root.backend.playing ? I18n.toolbar.pausePlayback : I18n.toolbar.resumePlayback
                ToolbarTip { trigger: parent.hovered; text: parent.Accessible.name }
                onClicked: root.backend.togglePause()
            }
            Button {
                id: exitBtn
                objectName: "toolbarExitButton"
                flat: true
                implicitWidth: 34; implicitHeight: 34
                leftPadding: 4; rightPadding: 4; topPadding: 4; bottomPadding: 4
                icon.source: Ui.icon("stop")
                icon.width: 18; icon.height: 18
                display: AbstractButton.IconOnly
                Accessible.name: root.backend.manualMode ? I18n.toolbar.stopPlayback : I18n.toolbar.exitOverlay
                ToolbarTip { trigger: parent.hovered; text: parent.Accessible.name }
                onClicked: root.backend.stop()
            }

            // 1. 弹幕显隐切换（微软官方 Fluent eye / eye-off 图标）
            Button {
                id: toggleDanmakuBtn
                flat: true
                leftPadding: 4; rightPadding: 4; topPadding: 4; bottomPadding: 4
                implicitWidth: 34
                implicitHeight: 34
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

            Item {
                width: 1
                height: 34
                Rectangle {
                    anchors.centerIn: parent
                    width: 1
                    height: 18
                    color: Ui.cardBorder
                }
            }

            // 2. 时间轴微调组（官方 Fluent chevron-left / timer / chevron-right）
            RowLayout {
                spacing: 1

                Button {
                    id: stepDecBtn
                    objectName: "toolbarSeekBackButton"
                    flat: true
                    leftPadding: 2; rightPadding: 2; topPadding: 2; bottomPadding: 2
                    implicitWidth: 28
                    implicitHeight: 34
                    icon.source: Ui.icon("chevron-left")
                    icon.width: 16
                    icon.height: 16
                    display: AbstractButton.IconOnly
                    Accessible.name: root.backend.manualMode ? I18n.toolbar.seekBack : I18n.toolbar.offsetDec
                    ToolbarTip {
                        trigger: parent.hovered
                        text: parent.Accessible.name
                    }
                    onClicked: {
                        if (root.backend.manualMode) root.backend.seek(root.backend.position - 5)
                        else root.backend.stepTimeOffset(-0.5)
                    }
                }

                Button {
                    id: offsetResetBtn
                    objectName: "toolbarTimeButton"
                    flat: true
                    leftPadding: 6; rightPadding: 6; topPadding: 2; bottomPadding: 2
                    implicitHeight: 34
                    readonly property real offsetVal: root.backend && root.backend.settings.values.timeOffset ? root.backend.settings.values.timeOffset : 0.0
                    icon.source: Ui.icon("timer")
                    icon.width: 16
                    icon.height: 16
                    text: root.backend.manualMode ? root.formatTime(root.backend.position)
                        : (offsetVal >= 0 ? "+" : "") + offsetVal.toFixed(1) + "s"
                    display: AbstractButton.TextBesideIcon
                    Accessible.name: root.backend.manualMode ? I18n.toolbar.seekTitle : I18n.toolbar.offsetReset
                    ToolbarTip {
                        trigger: parent.hovered
                        text: parent.Accessible.name
                    }
                    onClicked: {
                        if (root.backend.manualMode) {
                            const open = !seekPopup.visible
                            root.closePopovers()
                            seekPopup.visible = open
                        } else root.backend.settings.setValue("timeOffset", 0.0)
                    }
                }

                Button {
                    id: stepIncBtn
                    objectName: "toolbarSeekForwardButton"
                    flat: true
                    leftPadding: 2; rightPadding: 2; topPadding: 2; bottomPadding: 2
                    implicitWidth: 28
                    implicitHeight: 34
                    icon.source: Ui.icon("chevron-right")
                    icon.width: 16
                    icon.height: 16
                    display: AbstractButton.IconOnly
                    Accessible.name: root.backend.manualMode ? I18n.toolbar.seekForward : I18n.toolbar.offsetInc
                    ToolbarTip {
                        trigger: parent.hovered
                        text: parent.Accessible.name
                    }
                    onClicked: {
                        if (root.backend.manualMode) root.backend.seek(root.backend.position + 5)
                        else root.backend.stepTimeOffset(0.5)
                    }
                }
            }

            Item {
                width: 1
                height: 34
                Rectangle {
                    anchors.centerIn: parent
                    width: 1
                    height: 18
                    color: Ui.cardBorder
                }
            }

            // 3. 不透明度调节（官方 Fluent appearance 图标 + 百分比数值）
            Button {
                id: opacityBtn
                objectName: "toolbarOpacityButton"
                flat: true
                leftPadding: 6; rightPadding: 6; topPadding: 2; bottomPadding: 2
                implicitHeight: 34
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
                onClicked: {
                    const open = !opacityPopup.visible
                    root.closePopovers()
                    opacityPopup.visible = open
                }
            }

            Item {
                width: 1
                height: 34
                Rectangle {
                    anchors.centerIn: parent
                    width: 1
                    height: 18
                    color: Ui.cardBorder
                }
            }

            // 4. 字号微调组（A- / 数值 / A+）
            RowLayout {
                spacing: 1

                Button {
                    id: fontDecBtn
                    flat: true
                    leftPadding: 2; rightPadding: 2; topPadding: 2; bottomPadding: 2
                    implicitWidth: 26
                    implicitHeight: 34
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
                    Layout.preferredWidth: Math.max(20, implicitWidth)
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
                    implicitWidth: 26
                    implicitHeight: 34
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

            Item {
                width: 1
                height: 34
                Rectangle {
                    anchors.centerIn: parent
                    width: 1
                    height: 18
                    color: Ui.cardBorder
                }
            }

            // 5. 一键清屏（官方 Fluent delete 图标）
            Button {
                id: clearBtn
                flat: true
                leftPadding: 4; rightPadding: 4; topPadding: 4; bottomPadding: 4
                implicitWidth: 34
                implicitHeight: 34
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
                implicitWidth: 34
                implicitHeight: 34
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
                implicitWidth: 34
                implicitHeight: 34
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

            Item {
                width: 1
                height: 34
                Rectangle {
                    anchors.centerIn: parent
                    width: 1
                    height: 18
                    color: Ui.cardBorder
                }
            }

            // 8. 图钉固定常驻（官方 Fluent pin / pin-off 图标）
            Button {
                id: pinBtn
                objectName: "toolbarPinButton"
                flat: true
                leftPadding: 4; rightPadding: 4; topPadding: 4; bottomPadding: 4
                implicitWidth: 34
                implicitHeight: 34
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
        width: Math.min(220 * Ui.textScale, Math.max(0, root.maximumWidth - 12))
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
                Layout.preferredWidth: Math.max(32, implicitWidth)
            }
        }
    }

    Rectangle {
        id: seekPopup
        objectName: "toolbarSeekPopup"
        visible: false
        anchors.top: barCard.bottom
        anchors.topMargin: 4
        anchors.horizontalCenter: parent.horizontalCenter
        width: Math.min(320 * Ui.textScale, Math.max(0, root.maximumWidth - 12))
        height: seekContent.implicitHeight + 20
        radius: 8
        color: Ui.dark ? "#262626" : "#FFFFFF"
        border.width: 1
        border.color: Ui.cardBorder
        onVisibleChanged: if (!visible) seekSlider.pendingSeek = false
        ColumnLayout {
            id: seekContent
            anchors.fill: parent
            anchors.margins: 10
            spacing: 4
            Label {
                Layout.fillWidth: true
                text: root.formatTime(seekSlider.pendingSeek ? seekSlider.value : root.backend.position)
                    + " / " + root.formatTime(root.backend.duration)
                font.pixelSize: Ui.captionSize
                color: Ui.textColor
                elide: Text.ElideRight
            }
            Slider {
                id: seekSlider
                objectName: "toolbarSeekSlider"
                Layout.fillWidth: true
                from: 0
                to: Math.max(1, root.backend.duration)
                property bool pendingSeek: false
                Binding on value { value: root.backend.position; when: !seekSlider.pressed && !seekSlider.pendingSeek }
                Accessible.name: I18n.toolbar.seekTitle
                onMoved: {
                    pendingSeek = true
                    if (!pressed && root.presentationAllowed && seekPopup.visible) {
                        const target = value
                        pendingSeek = false
                        root.backend.seek(target)
                    }
                }
                onPressedChanged: {
                    if (!pressed && pendingSeek && root.presentationAllowed && seekPopup.visible) {
                        const target = value
                        pendingSeek = false
                        root.backend.seek(target)
                    }
                }
            }
        }
    }
}
