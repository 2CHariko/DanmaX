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
    SettingsSection {
        title: I18n.player.sourceTabsAccessible
        TabBar {
            id: sourceTabs
            objectName: "danmakuSourceTabs"
            onCurrentIndexChanged: if(currentIndex === 2) page.backend.library.refreshCache()
            currentIndex: page.sourceTab
            Layout.fillWidth: true
            Accessible.name: I18n.player.sourceTabsAccessible
            // Qt 6.11's text-only TabButton label uses icon.color, whose
            // inherited default is transparent. Use the public palette role.
            TabButton { objectName: "sourceLocalTab"; text: I18n.player.sourceLocalTab; icon.color: palette.buttonText }
            TabButton { text: I18n.player.sourceOnlineTab; icon.color: palette.buttonText }
            TabButton { text: I18n.player.sourceCachedTab; icon.color: palette.buttonText }
        }
        ColumnLayout {
            visible: sourceTabs.currentIndex === 0
            Layout.fillWidth: true
            spacing: 8
            Label { text: I18n.player.localSectionTitle }
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
                Button { text: I18n.player.browseFile; onClicked: filePicker.open() }
                Button { text: page.backend.loading ? I18n.player.cancelLoad : I18n.player.loadDanmaku; enabled: page.backend.loading || pathInput.text.trim().length > 0; onClicked: page.backend.loading ? page.backend.cancelLoad() : page.backend.loadFile(pathInput.text) }
            }
            }
            ProgressBar { visible: page.backend.loading; Layout.fillWidth: true; indeterminate: page.backend.loadProgress < 0; from: 0; to: 100; value: Math.max(0, page.backend.loadProgress) }
        }
        OnlineDanmakuPane { visible: sourceTabs.currentIndex === 1; Layout.fillWidth: true; library: page.backend.library; onOpenSettings: page.openSettings() }
        CachedDanmakuPane { visible: sourceTabs.currentIndex === 2; Layout.fillWidth: true; library: page.backend.library }
        ColumnLayout {
            visible: page.backend.sourceTitle.length > 0 || page.backend.total > 0
            Layout.fillWidth: true
            spacing: 4
            Label { visible: text.length > 0; text: page.backend.sourceTitle; font.weight: Font.DemiBold; Layout.fillWidth: true; wrapMode: Text.WrapAnywhere; Accessible.name: I18n.player.currentDanmakuPrefix + text }
            Label { text: I18n.format(I18n.player.loadedCountFormat, page.backend.total); color: Ui.secondaryText }
        }
    }
    SettingsSection {
        title: I18n.player.modeSectionTitle
        Flow {
            Layout.fillWidth: true
            spacing: 16
            RadioButton { objectName: "syncMode"; text: I18n.player.syncMode; checked: !page.effectiveManual; enabled: !page.backend.running; onClicked: page.independent = false }
            RadioButton { objectName: "manualMode"; text: I18n.player.manualMode; checked: page.effectiveManual; enabled: !page.backend.running; onClicked: page.independent = true }
        }
        ColumnLayout {
            visible: !page.effectiveManual
            Layout.fillWidth: true
            spacing: 12
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
            Label { text: I18n.player.sessionHint; color: Ui.secondaryText; wrapMode: Text.WordWrap; Layout.fillWidth: true }
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
                Label { text: I18n.player.sessionIdHint; color: Ui.secondaryText; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            }
        }
        Label { visible: page.effectiveManual; text: I18n.player.manualHint; color: Ui.secondaryText; wrapMode: Text.WordWrap; Layout.fillWidth: true }
    }
    SettingsSection {
        title: I18n.player.controlSectionTitle
        Label { text: page.backend.status; wrapMode: Text.WordWrap; Layout.fillWidth: true; Accessible.name: I18n.player.statusAccessiblePrefix + text }
        Label { visible: !page.backend.running && text.length > 0; text: page.blockedReason; color: Ui.secondaryText; wrapMode: Text.WordWrap; Layout.fillWidth: true }
        Flow {
            Layout.fillWidth: true
            spacing: 8
            Button { objectName: "startPlayback"; visible: !page.backend.running; text: I18n.player.startPlayback; highlighted: true; enabled: page.blockedReason.length === 0; onClicked: page.backend.start(page.independent) }
            Button { objectName: "pausePlayback"; visible: page.backend.running && page.backend.manualMode; text: page.backend.playing ? I18n.player.pausePlayback : I18n.player.resumePlayback; onClicked: page.backend.togglePause() }
            Button { objectName: "stopPlayback"; visible: page.backend.running || page.backend.total > 0 || page.backend.loading || page.backend.library.busy; text: I18n.player.stopPlayback; Accessible.description: I18n.player.stopPlaybackDesc; onClicked: page.backend.stop() }
        }
        ColumnLayout {
            visible: page.backend.running
            Layout.fillWidth: true
            spacing: 8
            Label { text: page.backend.mediaTitle; visible: text.length > 0; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            Label { text: page.formatTime(page.backend.position) + " / " + page.formatTime(page.backend.duration) + (page.backend.manualMode ? I18n.player.timeManualSuffix : I18n.player.timeSyncSuffix); wrapMode: Text.WordWrap; Layout.fillWidth: true }
            Slider { visible: page.backend.manualMode; Layout.fillWidth: true; from: 0; to: Math.max(1, page.backend.duration); value: page.backend.position; Accessible.name: I18n.player.manualSeekAccessible; onMoved: page.backend.seek(value) }
            ProgressBar { indeterminate: false; visible: !page.backend.manualMode; Layout.fillWidth: true; from: 0; to: Math.max(1, page.backend.duration); value: page.backend.position; Accessible.name: I18n.player.playerProgressAccessible }
            Disclosure {
                title: I18n.player.runtimeDetailsTitle
                Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: I18n.format(I18n.player.runtimeDetailsFormat, page.backend.metrics.active || 0, page.backend.metrics.dropped || 0, Number(page.backend.metrics.memoryMiB || 0).toFixed(1)); color: Ui.secondaryText }
            }
        }
    }
    FileDialog { id: filePicker; title: I18n.player.browseDialogTitle; nameFilters: [I18n.player.browseFileFilter]; onAccepted: page.backend.loadFile(selectedFile.toString()) }
}
