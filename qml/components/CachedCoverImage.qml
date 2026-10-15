import QtQuick

Image {
    id: imageRoot
    objectName: "cachedCoverImage"
    required property QtObject cacheService
    property url remoteSource: ""
    // Load on first display, then keep the bounded request until this item is destroyed.
    // StackLayout hides pages without destroying them; navigation must retain the image.
    property bool requested: false
    readonly property QtObject request: cacheService ? cacheService.createRequest(imageRoot) : null
    Binding { target: imageRoot.request; property: "url"; value: imageRoot.visible || imageRoot.requested ? imageRoot.remoteSource : ""; when: imageRoot.request !== null }
    source: request ? request.source : ""
    asynchronous: true
    cache: true
    onVisibleChanged: if (visible) {
        requested = true
        if (request) request.retry()
    }
    Component.onCompleted: if (visible) requested = true
}
