import QtQuick
import QtQuick.Controls.FluentWinUI3
import QtQuick.Layouts
import "../theme"
import "../i18n"

ColumnLayout {
    id: pane
    required property QtObject metadata
    readonly property bool showingResults: metadata.searchKeyword.length > 0
    signal detailsRequested(var anime)
    signal revealRequested(var item)
    Layout.fillWidth: true
    spacing: 12

    visible: showingResults
    function focusFirstResult() {
        if (results.count > 0) pane.revealRequested(results.itemAt(0))
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 8
        visible: pane.showingResults
        Label {
            Layout.fillWidth: true
            text: I18n.format(I18n.home.searchResultsTitle, pane.metadata.searchKeyword)
            textFormat: Text.PlainText
            font.pixelSize: Ui.subtitleSize
            font.weight: Font.DemiBold
            wrapMode: Text.WordWrap
            maximumLineCount: 2
            elide: Text.ElideRight
            Accessible.role: Accessible.Heading
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            BusyIndicator { running: pane.metadata.searching; visible: running }
            Label {
                objectName: "homeSearchStatus"
                Layout.fillWidth: true
                text: pane.metadata.searching ? (pane.metadata.searchStale ? I18n.home.updatingCache : I18n.home.searchLoading)
                    : pane.metadata.searchError || (pane.metadata.searchResults.length === 0 ? I18n.home.searchEmpty
                    : I18n.format(I18n.home.searchCount, pane.metadata.searchResults.length))
                textFormat: Text.PlainText
                wrapMode: Text.WordWrap
                color: Ui.secondaryText
                Accessible.role: Accessible.AlertMessage
            }
            Button {
                objectName: "retryAnimeSearch"
                text: pane.metadata.searchError.length > 0 ? I18n.home.searchRetry : I18n.common.refresh
                enabled: !pane.metadata.searching
                onClicked: pane.metadata.searchBangumi(pane.metadata.searchKeyword, true)
            }
        }
        Repeater {
            id: results
            model: pane.metadata.searchResults
            delegate: ItemDelegate {
                required property var modelData
                required property int index
                objectName: "animeSearchResult" + index
                Layout.fillWidth: true
                Accessible.name: modelData.title + " " + modelData.originalTitle + " " + modelData.airDate
                onClicked: pane.detailsRequested(modelData)
                contentItem: RowLayout {
                    spacing: 12
                    CachedCoverImage {
                        Layout.preferredWidth: 48
                        Layout.preferredHeight: 64
                        cacheService: pane.metadata.coverCache
                        remoteSource: modelData.cover
                        visible: remoteSource.toString().length > 0
                        sourceSize.width: 96
                        sourceSize.height: 128
                        fillMode: Image.PreserveAspectFit
                        asynchronous: true
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        spacing: 4
                        Label {
                            Layout.fillWidth: true
                            text: modelData.title
                            textFormat: Text.PlainText
                            font.weight: Font.DemiBold
                            wrapMode: Text.WordWrap
                            maximumLineCount: 2
                            elide: Text.ElideRight
                        }
                        Label {
                            Layout.fillWidth: true
                            text: modelData.originalTitle
                            visible: text.length > 0 && text !== modelData.title
                            textFormat: Text.PlainText
                            wrapMode: Text.WordWrap
                            maximumLineCount: 2
                            elide: Text.ElideRight
                            color: Ui.secondaryText
                        }
                        Label {
                            Layout.fillWidth: true
                            text: [modelData.airDate, modelData.score ? I18n.home.ratingPrefix + modelData.score : ""].filter(Boolean).join(" · ")
                            textFormat: Text.PlainText
                            wrapMode: Text.WordWrap
                            color: Ui.secondaryText
                            visible: text.length > 0
                        }
                    }
                }
            }
        }
        Label {
            Layout.fillWidth: true
            visible: !pane.metadata.searching && pane.metadata.searchError.length === 0 && pane.metadata.searchTotal > 20
            text: I18n.home.searchLimit
            wrapMode: Text.WordWrap
            color: Ui.secondaryText
        }
    }
}
