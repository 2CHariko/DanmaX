import QtQuick
import QtQuick.Controls.FluentWinUI3
import QtQuick.Layouts
import "../theme"
import "../i18n"

ColumnLayout {
    id: pane
    required property QtObject metadata
    property bool ownerVisible: true
    readonly property bool showingResults: metadata.searchKeyword.length > 0
    readonly property alias searchField: keyword
    signal detailsRequested(var anime)
    signal revealRequested(var item)
    Layout.fillWidth: true
    spacing: 12

    function reset() {
        metadata.clearSearch()
        keyword.text = ""
    }
    onOwnerVisibleChanged: if (!ownerVisible) reset()

    Item {
        Layout.fillWidth: true
        implicitHeight: keyword.implicitHeight
        SearchField {
            id: keyword
            objectName: "homeAnimeSearch"
            anchors.horizontalCenter: parent.horizontalCenter
            width: Math.min(560, parent.width)
            live: false
            suggestionModel: []
            // SearchField exposes its native TextField through public contentItem.
            Binding { target: keyword.contentItem; property: "placeholderText"; value: I18n.home.searchPlaceholder }
            Binding { target: keyword.contentItem; property: "maximumLength"; value: 512 }
            Accessible.name: I18n.home.searchAccessible
            onSearchTriggered: pane.metadata.searchBangumi(text)
            onSearchButtonPressed: pane.metadata.searchBangumi(text)
            onClearButtonPressed: pane.reset()
            onTextChanged: if (text.trim().length === 0) pane.metadata.clearSearch()
            Keys.onDownPressed: event => {
                if (results.count > 0) {
                    pane.revealRequested(results.itemAt(0))
                    event.accepted = true
                } else event.accepted = false
            }
            Keys.onEscapePressed: event => {
                if (text.length > 0) { pane.reset(); event.accepted = true }
                else event.accepted = false
            }
        }
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
                text: pane.metadata.searching ? I18n.home.searchLoading
                    : pane.metadata.searchError || (pane.metadata.searchResults.length === 0 ? I18n.home.searchEmpty
                    : I18n.format(I18n.home.searchCount, pane.metadata.searchResults.length))
                textFormat: Text.PlainText
                wrapMode: Text.WordWrap
                color: Ui.secondaryText
                Accessible.role: Accessible.AlertMessage
            }
            Button {
                objectName: "retryAnimeSearch"
                text: I18n.home.searchRetry
                visible: pane.metadata.searchError.length > 0
                onClicked: pane.metadata.searchBangumi(pane.metadata.searchKeyword)
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
                    Image {
                        Layout.preferredWidth: 48
                        Layout.preferredHeight: 64
                        source: modelData.cover
                        visible: source.toString().length > 0
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
