import QtQuick
import "../theme"
import QtQuick.Controls.FluentWinUI3
import QtQuick.Layouts
import "../components"
import "../i18n"

PageFrame {
    id: page
    required property QtObject backend
    property QtObject backdrop: null
    readonly property var values: backend.settings.values
    title: I18n.settings.title
    description: I18n.settings.description
    function focusOnlineSettings() {
        serverEditor.reload()
        serverDialog.open()
        if (serverRows.count > 0 && serverRows.itemAt(0)) {
            serverRows.itemAt(0).input.forceActiveFocus()
        } else if (addServerButton) {
            addServerButton.forceActiveFocus()
        }
    }
    function put(key, value) { backend.settings.setValue(key, value) }

    SettingsSection {
        title: I18n.settings.appearanceSection; Layout.fillWidth: true
        SettingsRow {
            title: I18n.settings.themeTitle
            AppComboBox { model: I18n.settings.themeOptions; currentIndex: ["system","light","dark"].indexOf(page.values.theme); implicitWidth: 160; Accessible.name: I18n.settings.themeAccessible; onActivated: page.put("theme", ["system","light","dark"][currentIndex]) }
        }
        SettingsRow {
            title: I18n.settings.backdropTitle
            description: page.backdrop && page.backdrop.fallbackReason.length > 0 ? page.backdrop.fallbackReason : I18n.settings.backdropDefaultDesc
            Switch { objectName: "micaSwitch"; checked: page.values.micaEnabled; Accessible.name: I18n.settings.backdropAccessible; onToggled: page.put("micaEnabled", checked) }
        }
    }

    SettingsSection {
        title: I18n.settings.styleSection; Layout.fillWidth: true
        SettingsRow {
            title: I18n.settings.fontFamilyTitle
            description: I18n.settings.fontFamilyDesc
            FontSelector { families: page.backend.fontFamilies; family: page.values.fontFamily; onFamilySelected: function(name) { page.put("fontFamily", name) } }
        }
        SettingsRow {
            title: I18n.settings.fontSizeTitle
            description: I18n.settings.fontSizeDesc
            SpinBox { from: 10; to: 72; value: page.values.fontSize; editable: true; Accessible.name: I18n.settings.fontSizeAccessible; onValueModified: page.put("fontSize", value) }
        }
        SettingsRow {
            title: I18n.settings.strokeWidthTitle
            SpinBox { from: 0; to: 6; value: page.values.strokeWidth; editable: true; Accessible.name: I18n.settings.strokeWidthAccessible; onValueModified: page.put("strokeWidth", value) }
        }
        SettingsRow {
            title: I18n.settings.opacityTitle
            SpinBox { from: 5; to: 100; value: Math.round(page.values.opacity * 100); editable: true; Accessible.name: I18n.settings.opacityAccessible; onValueModified: page.put("opacity", value / 100) }
            Label { text: I18n.common.percentSuffix }
        }
    }

    SettingsSection {
        title: I18n.settings.playbackSection; Layout.fillWidth: true
        Repeater {
            model: [
                {key: "speed", title: I18n.settings.speedTitle, acc: I18n.settings.speedAccessible, min: 30, max: 1500},
                {key: "maxActive", title: I18n.settings.maxActiveTitle, acc: I18n.settings.maxActiveAccessible, min: 50, max: 5000},
                {key: "maxTracks", title: I18n.settings.maxTracksTitle, acc: I18n.settings.maxTracksAccessible, min: 1, max: 60}
            ]
            SettingsRow {
                required property var modelData
                title: modelData.title
                SpinBox { from: modelData.min; to: modelData.max; value: page.values[modelData.key]; editable: true; Accessible.name: modelData.acc; onValueModified: page.put(modelData.key, value) }
            }
        }
        SettingsRow { title: I18n.settings.fixedSecondsTitle; SpinBox { from: 1; to: 30; value: page.values.fixedSeconds; editable: true; Accessible.name: I18n.settings.fixedSecondsAccessible; onValueModified: page.put("fixedSeconds", value) } }
        SettingsRow { title: I18n.settings.lineSpacingTitle; SpinBox { from: 0; to: 200; value: Math.round(page.values.lineSpacing * 100); editable: true; Accessible.name: I18n.settings.lineSpacingAccessible; onValueModified: page.put("lineSpacing", value / 100) } }
        SettingsRow { title: I18n.settings.overlapTitle; description: I18n.settings.overlapDesc; Switch { checked: page.values.overlap; Accessible.name: I18n.settings.overlapAccessible; onToggled: page.put("overlap", checked) } }
    }

    SettingsSection {
        title: I18n.settings.syncSection; Layout.fillWidth: true
        SettingsRow { title: I18n.settings.timeOffsetTitle; description: I18n.settings.timeOffsetDesc; SpinBox { from: -1200; to: 1200; value: Math.round(page.values.timeOffset * 10); editable: true; Accessible.name: I18n.settings.timeOffsetAccessible; onValueModified: page.put("timeOffset", value / 10) } }
        SettingsRow { title: I18n.settings.screenTitle; AppComboBox { model: page.backend.screens; currentIndex: Math.min(page.values.screenIndex, count - 1); implicitWidth: 240; Accessible.name: I18n.settings.screenAccessible; onActivated: page.put("screenIndex", currentIndex) } }
        SettingsRow { title: I18n.settings.onTopTitle; AppComboBox { model: I18n.settings.onTopOptions; currentIndex: page.values.onTop; implicitWidth: 160; Accessible.name: I18n.settings.onTopAccessible; onActivated: page.put("onTop", currentIndex) } }
        SettingsRow { title: I18n.settings.foregroundOnlyTitle; description: I18n.settings.foregroundOnlyDesc; Switch { checked: page.values.foregroundOnly; Accessible.name: I18n.settings.foregroundOnlyAccessible; onToggled: page.put("foregroundOnly", checked) } }
    }

    ListModel { id: serverModel }
    QtObject {
        id: serverEditor
        property bool committing: false
        property string initialSnapshot: ""
        property int revision: 0

        function currentEncoded() {
            const list = []
            for (let i = 0; i < serverModel.count; ++i)
                list.push(serverModel.get(i).address.trim())
            return JSON.stringify(list)
        }

        readonly property bool isDirty: {
            revision
            return currentEncoded() !== initialSnapshot
        }

        function reload() {
            initialSnapshot = JSON.stringify(page.values.danmakuServers)
            serverModel.clear()
            for (const address of page.values.danmakuServers)
                serverModel.append({address: address, saved: address, issue: ""})
            revision++
        }

        function syncLiveInputs() {
            for (let i = 0; i < serverRows.count; ++i) {
                const row = serverRows.itemAt(i)
                if (row && row.input && i < serverModel.count) {
                    const text = row.input.text
                    if (serverModel.get(i).address !== text)
                        serverModel.setProperty(i, "address", text)
                }
            }
        }
        function canonicalAddress(raw) {
            let s = raw.trim().toLowerCase()
            while (s.endsWith("/")) s = s.slice(0, -1)
            return s
        }

        function validateAddress(raw) {
            const trimmed = raw.trim()
            if (!trimmed.length)
                return I18n.settings.serverEmptyIssue
            if (trimmed.length > 4096)
                return I18n.settings.serverSaveIssue
            const lower = trimmed.toLowerCase()
            if (!lower.startsWith("http://") && !lower.startsWith("https://"))
                return I18n.settings.serverInvalidProtocolIssue
            if (trimmed.includes("?") || trimmed.includes("#") || trimmed.includes(" ") || trimmed.includes("@"))
                return I18n.settings.serverInvalidHostIssue
            const afterScheme = trimmed.substring(trimmed.indexOf("://") + 3)
            const slashPos = afterScheme.indexOf("/")
            const hostPort = (slashPos >= 0 ? afterScheme.substring(0, slashPos) : afterScheme).trim()
            if (!hostPort.length)
                return I18n.settings.serverInvalidHostIssue
            return ""
        }

        function save() {
            syncLiveInputs()
            const seen = []
            let firstFailedIndex = -1

            for (let i = 0; i < serverModel.count; ++i) {
                const text = serverModel.get(i).address.trim()
                const err = validateAddress(text)
                if (err.length > 0) {
                    serverModel.setProperty(i, "issue", err)
                    if (firstFailedIndex < 0) firstFailedIndex = i
                    continue
                }
                const canon = canonicalAddress(text)
                if (seen.indexOf(canon) >= 0) {
                    serverModel.setProperty(i, "issue", I18n.settings.serverDuplicateIssue)
                    if (firstFailedIndex < 0) firstFailedIndex = i
                    continue
                }
                seen.push(canon)
                serverModel.setProperty(i, "issue", "")
            }

            if (firstFailedIndex >= 0) {
                if (serverRows.itemAt(firstFailedIndex))
                    serverRows.itemAt(firstFailedIndex).input.forceActiveFocus()
                return false
            }

            const addresses = []
            for (let i = 0; i < serverModel.count; ++i) {
                const addr = serverModel.get(i).address.trim()
                if (addr.length) addresses.push(addr)
            }
            committing = true
            const ok = page.backend.settings.setDanmakuServers(addresses)
            committing = false
            if (!ok) {
                for (let i = 0; i < serverModel.count; ++i)
                    serverModel.setProperty(i, "issue", I18n.settings.serverSaveIssue)
                return false
            }
            initialSnapshot = JSON.stringify(page.values.danmakuServers)
            reload()
            return true
        }
        function edit(index, text) {
            if (index < 0 || index >= serverModel.count) return
            serverModel.setProperty(index, "address", text)
            if (!text.trim().length) {
                serverModel.setProperty(index, "issue", I18n.settings.serverEmptyIssue)
            } else {
                serverModel.setProperty(index, "issue", "")
            }
            revision++
        }

        function move(index, destination) {
            syncLiveInputs()
            serverModel.move(index, destination, 1)
            revision++
        }

        function remove(index) {
            if (index < 0 || index >= serverModel.count) return
            syncLiveInputs()
            const next = Math.min(index, serverModel.count - 2)
            serverModel.remove(index)
            revision++
            if (next >= 0 && serverRows.itemAt(next)) serverRows.itemAt(next).input.forceActiveFocus()
            else addServerButton.forceActiveFocus()
        }

        function add() {
            syncLiveInputs()
            serverModel.append({address: "", saved: "", issue: ""})
            revision++
            Qt.callLater(() => {
                if (serverRows.count > 0 && serverRows.itemAt(serverModel.count - 1)) {
                    serverRows.itemAt(serverModel.count - 1).input.forceActiveFocus()
                }
            })
        }
    }
    Component.onCompleted: serverEditor.reload()
    Connections { target: page.backend.settings; function onChanged() { serverEditor.reload() } }
    Connections {
        target: page
        function onVisibleChanged() {
            if (!page.visible && serverDialog.visible)
                serverDialog.close()
        }
    }

    SettingsSection {
        title: I18n.settings.onlineSection
        Layout.fillWidth: true

        SettingsRow {
            title: I18n.settings.onlineServersTitle
            description: page.values.danmakuServers && page.values.danmakuServers.length > 0
                         ? I18n.format(I18n.settings.onlineServersDescFormat, page.values.danmakuServers.length, page.values.danmakuServers[0])
                         : I18n.settings.onlineServersEmptyDesc

            Button {
                id: manageServersBtn
                objectName: "manageDanmakuServers"
                text: I18n.settings.manageServersBtn
                Accessible.name: I18n.settings.manageServersBtn
                onClicked: page.focusOnlineSettings()
            }
        }
    }

    Dialog {
        id: serverDialog
        objectName: "danmakuServerDialog"
        parent: page
        anchors.centerIn: parent
        modal: true
        width: Math.min(page.width - 32, 540)
        title: I18n.settings.serverDialogTitle
        closePolicy: Popup.CloseOnEscape
        standardButtons: Dialog.NoButton
        onRejected: serverEditor.reload()
        onClosed: if (!serverDialog.visible) serverEditor.reload()

        footer: DialogButtonBox {
            alignment: Qt.AlignRight
            Button {
                id: saveServerBtn
                objectName: "saveServersBtn"
                text: I18n.settings.saveBtn
                enabled: serverEditor.isDirty
                highlighted: serverEditor.isDirty
                onClicked: {
                    if (serverEditor.save()) {
                        serverDialog.close()
                    }
                }
            }
            Button {
                id: cancelServerBtn
                objectName: "cancelServersBtn"
                text: I18n.settings.cancelBtn
                onClicked: {
                    serverEditor.reload()
                    serverDialog.close()
                }
            }
        }
        contentItem: ColumnLayout {
            width: serverDialog.availableWidth
            implicitWidth: Math.max(380, Math.min(page.width - 64, 508))
            spacing: 12

            Label {
                text: I18n.settings.onlineDesc
                color: Ui.secondaryText
                font.pixelSize: Ui.captionSize
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            ScrollView {
                id: serverScroll
                Layout.fillWidth: true
                Layout.maximumHeight: 280
                contentWidth: availableWidth
                clip: true
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

                ColumnLayout {
                    width: serverScroll.availableWidth
                    spacing: 8

                    Control {
                        visible: serverModel.count === 0
                        Layout.fillWidth: true
                        width: parent.width
                        leftPadding: 16
                        rightPadding: 16
                        topPadding: 16
                        bottomPadding: 16

                        background: Rectangle {
                            radius: Ui.cardRadius
                            color: Ui.dark ? Qt.rgba(1, 1, 1, 0.03) : Qt.rgba(0, 0, 0, 0.02)
                            border.width: 1
                            border.color: Ui.cardBorder
                        }

                        contentItem: ColumnLayout {
                            spacing: 4
                            Label {
                                text: I18n.settings.serverEmptyPlaceholderTitle
                                color: Ui.textColor
                                font.weight: Font.DemiBold
                                horizontalAlignment: Text.AlignHCenter
                                Layout.fillWidth: true
                            }
                            Label {
                                text: I18n.settings.serverEmptyPlaceholderDesc
                                color: Ui.secondaryText
                                font.pixelSize: Ui.captionSize
                                horizontalAlignment: Text.AlignHCenter
                                Layout.fillWidth: true
                            }
                        }
                    }

                    Repeater {
                        id: serverRows
                        model: serverModel
                        delegate: Control {
                            id: serverRow
                            required property int index
                            required property string address
                            required property string issue
                            property alias input: serverInput

                            Layout.fillWidth: true
                            width: parent.width
                            leftPadding: 12
                            rightPadding: 12
                            topPadding: 10
                            bottomPadding: 10

                            background: Rectangle {
                                radius: Ui.cardRadius
                                color: Ui.dark ? Qt.rgba(1, 1, 1, 0.04) : Qt.rgba(0, 0, 0, 0.02)
                                border.width: 1
                                border.color: serverRow.issue.length > 0 ? (Ui.dark ? "#FF99A4" : "#C42B1C") : Ui.cardBorder
                            }

                            contentItem: ColumnLayout {
                                spacing: 6

                                RowLayout {
                                    Layout.fillWidth: true
                                    width: parent.width
                                    spacing: 8

                                    TextField {
                                        id: serverInput
                                        objectName: serverRow.index === 0 ? "danmakuServerInput" : "danmakuServerInput" + serverRow.index
                                        Layout.fillWidth: true
                                        text: serverRow.address
                                        maximumLength: 4096
                                        placeholderText: I18n.settings.serverPlaceholder
                                        Accessible.name: I18n.format(I18n.settings.serverAccessibleFormat, serverRow.index + 1)
                                        onTextEdited: serverEditor.edit(serverRow.index, text)
                                        onEditingFinished: serverEditor.edit(serverRow.index, text)
                                    }

                                    RowLayout {
                                        spacing: 4

                                        Button {
                                            id: upBtn
                                            objectName: "serverUp" + serverRow.index
                                            enabled: serverRow.index > 0
                                            icon.source: Ui.icon("chevron-up")
                                            icon.width: 22
                                            icon.height: 22
                                            leftPadding: 4
                                            rightPadding: 4
                                            topPadding: 4
                                            bottomPadding: 4
                                            Layout.preferredWidth: 32
                                            Layout.preferredHeight: 32
                                            display: AbstractButton.IconOnly
                                            ToolTip.visible: hovered
                                            ToolTip.text: I18n.format(I18n.settings.moveUpAccessibleFormat, serverRow.index + 1)
                                            Accessible.name: I18n.format(I18n.settings.moveUpAccessibleFormat, serverRow.index + 1)
                                            onClicked: serverEditor.move(serverRow.index, serverRow.index - 1)
                                        }

                                        Button {
                                            id: downBtn
                                            objectName: "serverDown" + serverRow.index
                                            enabled: serverRow.index < serverModel.count - 1
                                            icon.source: Ui.icon("chevron-down")
                                            icon.width: 22
                                            icon.height: 22
                                            leftPadding: 4
                                            rightPadding: 4
                                            topPadding: 4
                                            bottomPadding: 4
                                            Layout.preferredWidth: 32
                                            Layout.preferredHeight: 32
                                            display: AbstractButton.IconOnly
                                            ToolTip.visible: hovered
                                            ToolTip.text: I18n.format(I18n.settings.moveDownAccessibleFormat, serverRow.index + 1)
                                            Accessible.name: I18n.format(I18n.settings.moveDownAccessibleFormat, serverRow.index + 1)
                                            onClicked: serverEditor.move(serverRow.index, serverRow.index + 1)
                                        }

                                        Button {
                                            id: removeBtn
                                            objectName: "serverRemove" + serverRow.index
                                            icon.source: Ui.icon("delete")
                                            icon.width: 20
                                            icon.height: 20
                                            leftPadding: 4
                                            rightPadding: 4
                                            topPadding: 4
                                            bottomPadding: 4
                                            Layout.preferredWidth: 32
                                            Layout.preferredHeight: 32
                                            display: AbstractButton.IconOnly
                                            ToolTip.visible: hovered
                                            ToolTip.text: I18n.format(I18n.settings.removeAccessibleFormat, serverRow.index + 1)
                                            Accessible.name: I18n.format(I18n.settings.removeAccessibleFormat, serverRow.index + 1)
                                            onClicked: serverEditor.remove(serverRow.index)
                                        }
                                    }
                                }

                                Label {
                                    visible: text.length > 0
                                    text: serverRow.issue
                                    color: Ui.dark ? "#FF99A4" : "#C42B1C"
                                    font.pixelSize: Ui.captionSize
                                    Layout.fillWidth: true
                                    wrapMode: Text.WordWrap
                                }
                            }
                        }
                    }

                }
            }

            Button {
                id: addServerButton
                objectName: "addDanmakuServer"
                text: I18n.settings.addServerBtn
                Layout.topMargin: 2
                Layout.leftMargin: 2
                onClicked: serverEditor.add()
            }

            Label {
                text: I18n.settings.onlineBottomHint
                color: Ui.secondaryText
                font.pixelSize: Ui.captionSize
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }
        }
    }

    SettingsSection {
        objectName: "cacheGroup"
        title: I18n.settings.cacheSection; Layout.fillWidth: true
        SettingsRow {
            title: I18n.settings.budgetModeTitle
            description: I18n.settings.budgetModeDesc
            AppComboBox { model: I18n.settings.budgetModeOptions; currentIndex: page.values.textureBudgetAuto ? 0 : 1; implicitWidth: 160; Accessible.name: I18n.settings.budgetModeAccessible; onActivated: page.put("textureBudgetAuto", currentIndex === 0) }
        }
        SettingsRow {
            title: page.values.textureBudgetAuto ? I18n.settings.limitTitleAuto : I18n.settings.limitTitleManual
            description: I18n.settings.limitDesc
            SpinBox { from: 32; to: 1024; stepSize: 32; value: page.values.textureBudgetMiB; editable: true; Accessible.name: I18n.settings.budgetMiBAccessible; onValueModified: page.put("textureBudgetMiB", value) }
        }
    }

    SettingsSection {
        objectName: "diagnosticsGroup"
        title: I18n.settings.diagnosticsSection; Layout.fillWidth: true
        SettingsRow { title: I18n.settings.debugTitle; Switch { objectName: "diagnosticsDebugSwitch"; checked: page.values.debug; Accessible.name: I18n.settings.debugAccessible; onToggled: page.put("debug", checked) } }
        SettingsRow { title: I18n.settings.debugPosTitle; AppComboBox { model: I18n.settings.debugPosOptions; currentIndex: ["top_left","top_right","bottom_left","bottom_right"].indexOf(page.values.debugPosition); implicitWidth: 160; Accessible.name: I18n.settings.debugPosAccessible; onActivated: page.put("debugPosition", ["top_left","top_right","bottom_left","bottom_right"][currentIndex]) } }
        SettingsRow { title: I18n.settings.logLevelTitle; AppComboBox { model: I18n.settings.logLevelOptions; currentIndex: I18n.settings.logLevelOptions.indexOf(page.values.logLevel); implicitWidth: 160; Accessible.name: I18n.settings.logLevelAccessible; onActivated: page.put("logLevel", currentText) } }
        SettingsRow { title: I18n.settings.logToFileTitle; description: I18n.settings.logToFileDesc; Switch { checked: page.values.logToFile; Accessible.name: I18n.settings.logToFileAccessible; onToggled: page.put("logToFile", checked) } }
    }

    SettingsSection {
        title: I18n.settings.resetSection
        Layout.fillWidth: true
        SettingsRow {
            title: I18n.settings.resetDefaultsBtn
            description: I18n.settings.resetDefaultsDesc
            Button {
                text: I18n.settings.resetBtn
                Accessible.name: I18n.settings.resetDefaultsBtn
                onClicked: resetDialog.open()
            }
        }
    }

    Dialog {
        id: resetDialog
        title: I18n.settings.resetDialogTitle
        modal: true
        anchors.centerIn: Overlay.overlay
        standardButtons: Dialog.Ok | Dialog.Cancel
        Label { text: I18n.settings.resetDialogContent }
        onAccepted: page.backend.settings.reset()
    }
}
