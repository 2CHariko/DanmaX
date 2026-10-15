import QtQuick
import "../theme"

Image {
    id: imageRoot
    objectName: "cachedCoverImage"
    required property QtObject cacheService
    property url remoteSource: ""
    property real imageOpacity: 1
    property real revealProgress: 1
    property bool sawPlaceholder: false
    readonly property bool coverLoading: Boolean(request && request.loading) || status === Image.Loading
    // Load on first display, then keep the bounded request until this item is destroyed.
    // StackLayout hides pages without destroying them; navigation must retain the image.
    property bool requested: false
    readonly property QtObject request: cacheService ? cacheService.createRequest(imageRoot) : null
    Binding { target: imageRoot.request; property: "url"; value: imageRoot.visible || imageRoot.requested ? imageRoot.remoteSource : ""; when: imageRoot.request !== null }
    source: request ? request.source : ""
    asynchronous: true
    cache: true
    opacity: status === Image.Ready ? imageOpacity * revealProgress : 1
    CoverPlaceholder {
        id: placeholder
        objectName: "coverPlaceholder"
        anchors.fill: parent
        visible: imageRoot.status !== Image.Ready
        loading: imageRoot.coverLoading
        onRevealedChanged: if (revealed) imageRoot.sawPlaceholder = true
    }
    NumberAnimation { id: reveal; target: imageRoot; property: "revealProgress"; from: 0; to: 1; duration: Ui.contentTransitionDuration }
    onStatusChanged: {
        if (visible && status === Image.Ready && sawPlaceholder && Ui.motionEnabled) reveal.restart()
        else { reveal.complete(); revealProgress = 1 }
        if (status === Image.Ready) sawPlaceholder = false
    }
    onRemoteSourceChanged: { sawPlaceholder = false; reveal.complete(); revealProgress = 1 }
    Connections {
        target: Ui
        function onMotionEnabledChanged() { if (!Ui.motionEnabled) reveal.complete() }
    }
    onVisibleChanged: if (visible) {
        requested = true
        if (request) request.retry()
    } else reveal.complete()
    Component.onCompleted: if (visible) requested = true
}
