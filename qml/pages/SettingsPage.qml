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
    function focusOnlineSettings() { reveal(serverRows.count ? serverRows.itemAt(0).input : addServerButton) }
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

    SettingsSection {
        title: I18n.settings.onlineSection
        description: I18n.settings.onlineDesc
        Layout.fillWidth: true
        ListModel { id: serverModel }
        QtObject {
            id: serverEditor
            property bool committing: false
            property string snapshot: ""
            function reload() {
                const encoded = JSON.stringify(page.values.danmakuServers)
                if (committing || encoded === snapshot) return
                snapshot = encoded
                serverModel.clear()
                for (const address of page.values.danmakuServers)
                    serverModel.append({address: address, saved: address, issue: ""})
            }
            function save() {
                const addresses = []
                for (let i = 0; i < serverModel.count; ++i)
                    if (serverModel.get(i).saved.length) addresses.push(serverModel.get(i).saved)
                const previous = JSON.stringify(page.values.danmakuServers)
                committing = true
                const ok = page.backend.settings.setDanmakuServers(addresses)
                snapshot = JSON.stringify(page.values.danmakuServers)
                committing = false
                return ok || previous !== snapshot
            }
            function edit(index, text) {
                const previous = serverModel.get(index).saved
                serverModel.setProperty(index, "address", text)
                if (!text.trim().length) {
                    serverModel.setProperty(index, "issue", I18n.settings.serverEmptyIssue)
                    return
                }
                serverModel.setProperty(index, "saved", text)
                if (!save()) {
                    serverModel.setProperty(index, "saved", previous)
                    serverModel.setProperty(index, "issue", I18n.settings.serverSaveIssue)
                    return
                }
                let savedIndex = -1
                for (let i = 0; i <= index; ++i)
                    if (serverModel.get(i).saved.length) ++savedIndex
                const normalized = page.values.danmakuServers[savedIndex]
                serverModel.setProperty(index, "saved", normalized)
                serverModel.setProperty(index, "address", normalized)
                serverModel.setProperty(index, "issue", "")
            }
            function move(index, destination) {
                serverModel.move(index, destination, 1)
                save()
            }
            function remove(index) {
                const next = Math.min(index, serverModel.count - 2)
                serverModel.remove(index)
                save()
                if (next >= 0) serverRows.itemAt(next).input.forceActiveFocus()
                else addServerButton.forceActiveFocus()
            }
        }
        Component.onCompleted: serverEditor.reload()
        Connections { target: page.backend.settings; function onChanged() { serverEditor.reload() } }
        Repeater {
            id: serverRows
            model: serverModel
            SettingsSection {
                id: serverRow
                card: true
                required property int index
                required property string address
                required property string issue
                property alias input: serverInput
                Layout.fillWidth: true
                TextField {
                    id: serverInput
                    objectName: serverRow.index === 0 ? "danmakuServerInput" : "danmakuServerInput" + serverRow.index
                    Layout.fillWidth: true
                    text: serverRow.address
                    maximumLength: 4096
                    placeholderText: I18n.settings.serverPlaceholder
                    Accessible.name: I18n.format(I18n.settings.serverAccessibleFormat, serverRow.index + 1)
                    onEditingFinished: serverEditor.edit(serverRow.index, text)
                }
                Flow {
                    Layout.fillWidth: true; spacing: 8
                    Button {
                        text: I18n.settings.moveUpBtn; objectName: "serverUp" + serverRow.index
                        enabled: serverRow.index > 0
                        Accessible.name: I18n.format(I18n.settings.moveUpAccessibleFormat, serverRow.index + 1)
                        onClicked: serverEditor.move(serverRow.index, serverRow.index - 1)
                    }
                    Button {
                        text: I18n.settings.moveDownBtn; objectName: "serverDown" + serverRow.index
                        enabled: serverRow.index < serverModel.count - 1
                        Accessible.name: I18n.format(I18n.settings.moveDownAccessibleFormat, serverRow.index + 1)
                        onClicked: serverEditor.move(serverRow.index, serverRow.index + 1)
                    }
                    Button {
                        text: I18n.settings.removeBtn; objectName: "serverRemove" + serverRow.index
                        Accessible.name: I18n.format(I18n.settings.removeAccessibleFormat, serverRow.index + 1)
                        onClicked: serverEditor.remove(serverRow.index)
                    }
                }
                Label { visible: text.length > 0; text: serverRow.issue; Layout.fillWidth: true; wrapMode: Text.WordWrap }
            }
        }
        Button {
            id: addServerButton
            objectName: "addDanmakuServer"
            text: I18n.settings.addServerBtn
            onClicked: {
                serverModel.append({address: "", saved: "", issue: ""})
                page.reveal(serverRows.itemAt(serverModel.count - 1).input)
            }
        }
        Label { text: I18n.settings.onlineBottomHint; Layout.fillWidth: true; wrapMode: Text.WordWrap }
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
