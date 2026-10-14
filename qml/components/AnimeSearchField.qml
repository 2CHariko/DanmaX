import QtQuick
import QtQuick.Controls.FluentWinUI3
import "../i18n"

SearchField {
    id: keyword
    required property QtObject metadata
    signal firstResultRequested()
    signal submitted()
    objectName: "homeAnimeSearch"
    live: false
    suggestionModel: []
    height: Math.max(28, contentItem.contentHeight + topPadding + bottomPadding)
    Binding { target: keyword.contentItem; property: "placeholderText"; value: I18n.home.searchPlaceholder }
    Binding { target: keyword.contentItem; property: "maximumLength"; value: 512 }
    Accessible.name: I18n.home.searchAccessible

    // The official SearchField editor is a template TextField with no placeholder visual.
    Label {
        objectName: "homeAnimeSearchHint"
        parent: keyword.contentItem
        x: parent.leftPadding
        y: parent.topPadding
        width: Math.max(0, parent.width - parent.leftPadding - parent.rightPadding)
        height: Math.max(0, parent.height - parent.topPadding - parent.bottomPadding)
        text: I18n.home.searchPlaceholder
        font: keyword.contentItem.font
        color: keyword.palette.placeholderText
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        visible: keyword.contentItem.text.length === 0 && keyword.contentItem.preeditText.length === 0
        Accessible.ignored: true
    }

    function reset() {
        metadata.clearSearch()
        text = ""
    }
    function submit() {
        metadata.searchBangumi(text)
        submitted()
    }
    onVisibleChanged: if (!visible) reset()
    onSearchTriggered: submit()
    onSearchButtonPressed: submit()
    onClearButtonPressed: reset()
    onTextChanged: if (text.trim().length === 0) metadata.clearSearch()
    Keys.onDownPressed: event => {
        if (metadata.searchResults.length > 0) {
            firstResultRequested()
            event.accepted = true
        } else event.accepted = false
    }
    Keys.onEscapePressed: event => {
        if (text.length > 0) { reset(); event.accepted = true }
        else event.accepted = false
    }
}
