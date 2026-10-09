import QtQuick
import QtQuick.Controls.FluentWinUI3
import QtQuick.Layouts
import "../theme"
import "../i18n"

ColumnLayout {
    id: pane
    required property QtObject library
    signal openSettings()
    spacing: 8
    Label {
        visible: pane.library.servers.length === 0
        text: I18n.online.noServerConfigured
        wrapMode: Text.WordWrap
        Layout.fillWidth: true
    }
    Label { visible: pane.library.activeServer.length > 0; text: I18n.online.activeServerPrefix + pane.library.activeServer; wrapMode: Text.WrapAnywhere; Layout.fillWidth: true }
    Button { visible: pane.library.servers.length === 0; text: I18n.online.openSettingsBtn; onClicked: pane.openSettings() }
    ColumnLayout {
        visible: pane.library.servers.length > 0
        Layout.fillWidth: true
        spacing: 12
    RowLayout {
        Layout.fillWidth: true
        TextField {
            id: keyword
            objectName: "animeKeyword"
            Layout.fillWidth: true
            placeholderText: I18n.online.searchPlaceholder
            Accessible.name: I18n.online.searchAccessible
            maximumLength: 512
            onAccepted: pane.library.searchAnime(text)
        }
        Button {
            text: I18n.online.searchBtn
            enabled: pane.library.servers.length > 0 && keyword.text.trim().length > 0
            onClicked: pane.library.searchAnime(keyword.text)
        }
    }
    Label { visible: pane.library.animes.length > 0; text: I18n.online.animeLabel; font.weight: Font.DemiBold }
    AppComboBox {
        id: animeBox
        visible: pane.library.animes.length > 0
        objectName: "animeChoice"
        Layout.fillWidth: true
        model: pane.library.animes
        textRole: "title"; valueRole: "id"
        displayText: currentIndex >= 0 ? currentText : I18n.online.animePlaceholder
        Accessible.name: I18n.online.animeAccessible
        onActivated: pane.library.selectAnime(currentValue)
        function sync() { currentIndex = indexOfValue(pane.library.selectedAnime) }
        onModelChanged: sync()
        Component.onCompleted: sync()
    }
    Label { visible: pane.library.selectedAnime.length > 0; text: I18n.online.episodeLabel; font.weight: Font.DemiBold }
    AppComboBox {
        id: episodeBox
        visible: pane.library.selectedAnime.length > 0
        objectName: "episodeChoice"
        Layout.fillWidth: true
        model: pane.library.episodes
        textRole: "title"; valueRole: "id"
        displayText: currentIndex >= 0 ? currentText : I18n.online.episodePlaceholder
        Accessible.name: I18n.online.episodeAccessible
        onActivated: pane.library.selectEpisode(currentValue)
        function sync() { currentIndex = indexOfValue(pane.library.selectedEpisode) }
        onModelChanged: sync()
        Component.onCompleted: sync()
    }
    Connections {
        target: pane.library
        function onChanged() { animeBox.sync(); episodeBox.sync() }
    }
    Flow {
        visible: pane.library.selectedEpisode.length > 0
        Layout.fillWidth: true
        spacing: 8
        Button {
            text: pane.library.selectedCached ? I18n.online.loadCachedBtn : I18n.online.downloadAndLoadBtn
            enabled: !pane.library.busy && pane.library.selectedEpisode.length > 0
            onClicked: pane.library.downloadEpisode(false)
        }
        Button {
            text: I18n.online.redownloadBtn
            enabled: !pane.library.busy && pane.library.selectedEpisode.length > 0
            onClicked: pane.library.downloadEpisode(true)
        }
    }
    }
    Button { text: I18n.online.cancelBtn; visible: pane.library.busy; onClicked: pane.library.cancel() }
    ProgressBar {
        visible: pane.library.busy
        Layout.fillWidth: true
        indeterminate: pane.library.progress < 0
        from: 0; to: 100; value: Math.max(0, pane.library.progress)
        Accessible.name: I18n.online.progressAccessible
    }
    Label { text: pane.library.status; wrapMode: Text.WordWrap; Layout.fillWidth: true; Accessible.name: I18n.online.statusPrefix + text }
    Label { visible: pane.library.error.length > 0; text: pane.library.error; wrapMode: Text.WordWrap; Layout.fillWidth: true; Accessible.name: I18n.online.errorPrefix + text }
    Label { visible: pane.library.servers.length > 0; text: I18n.online.readyToPlayHint; color: Ui.secondaryText; wrapMode: Text.WordWrap; Layout.fillWidth: true }
}
