import QtQuick

Image {
    id: imageRoot
    objectName: "cachedCoverImage"
    required property QtObject cacheService
    property url remoteSource: ""
    readonly property QtObject request: cacheService ? cacheService.createRequest(imageRoot) : null
    Binding { target: imageRoot.request; property: "url"; value: imageRoot.visible ? imageRoot.remoteSource : ""; when: imageRoot.request !== null }
    source: request ? request.source : ""
    asynchronous: true
    cache: true
    onVisibleChanged: if (visible && request) request.retry()
}
