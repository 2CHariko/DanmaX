import QtQuick
import QtQuick.Window
import QtQuick.Controls.FluentWinUI3
import QtQuick.Layouts
import QtQuick.Effects
import "../theme"

Item {
    id: feedback
    objectName: "videoDropOverlay"
    required property QtObject backend
    required property Item backgroundSource
    readonly property bool dragActive: engaged && enabled && visible
    readonly property bool blurSupported: !Ui.highContrast
        && GraphicsInfo.api !== GraphicsInfo.Software
        && GraphicsInfo.api !== GraphicsInfo.Unknown
        && GraphicsInfo.api !== GraphicsInfo.Null
    readonly property bool blurLoaded: backdropLoader.item !== null
    property bool engaged: false
    property string fileName: ""
    function reset() { engaged = false; fileName = "" }
    onEnabledChanged: { if (!enabled) reset() }
    onVisibleChanged: { if (!visible) reset() }

    // Destroy the texture and shader as soon as drag feedback ends. The detector
    // and sharp prompt are never children of the captured source.
    Loader {
        id: backdropLoader
        objectName: "videoDropBlurLoader"
        anchors.fill: parent
        active: feedback.dragActive && feedback.blurSupported
        sourceComponent: Item {
            clip: true
            ShaderEffectSource {
                id: snapshot
                objectName: "videoDropSnapshot"
                anchors.fill: parent
                sourceItem: feedback.backgroundSource
                hideSource: true
                live: false
                visible: false
                readonly property real captureScale: Math.min(Screen.devicePixelRatio,
                    Ui.dragSnapshotMaxDimension / Math.max(1, width, height))
                textureSize: Qt.size(Math.max(1, Math.ceil(width * captureScale)),
                                     Math.max(1, Math.ceil(height * captureScale)))
                Component.onCompleted: scheduleUpdate()
                onWidthChanged: scheduleUpdate()
                onHeightChanged: scheduleUpdate()
                onTextureSizeChanged: scheduleUpdate()
            }
            MultiEffect {
                objectName: "videoDropBlurEffect"
                anchors.fill: parent
                source: snapshot
                autoPaddingEnabled: false
                blurEnabled: true
                blurMax: Ui.dragBlurMax
                blur: 1.0
                saturation: Ui.dragBackgroundSaturation
            }
        }
    }
    Rectangle {
        anchors.fill: parent
        visible: feedback.dragActive
        color: feedback.blurSupported ? Ui.dragScrim : Ui.dragFallbackSurface
    }
    Rectangle {
        anchors.fill: parent
        anchors.margins: 8
        visible: feedback.dragActive
        color: "transparent"
        border.width: 2
        border.color: Ui.accentColor
        radius: Ui.cardRadius
    }
    ColumnLayout {
        id: prompt
        anchors.centerIn: parent
        width: Math.max(0, Math.min(Ui.dragPromptMaxWidth, parent.width - 2 * Ui.pagePadding))
        spacing: 16
        visible: feedback.dragActive
        opacity: feedback.dragActive ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: Ui.motionEnabled ? Ui.dragFeedbackDuration : 0 } }
        Image {
            source: Ui.icon("play")
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: 48
            Layout.preferredHeight: 48
            fillMode: Image.PreserveAspectFit
        }
        Label {
            Layout.fillWidth: true
            text: "松开以播放视频"
            font.pixelSize: Ui.titleSize
            color: Ui.textColor
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
        }
        Label {
            objectName: "videoDropFileName"
            Layout.fillWidth: true
            text: feedback.fileName
            textFormat: Text.PlainText
            font.pixelSize: Ui.bodySize
            color: Ui.textColor
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WrapAnywhere
            maximumLineCount: 2
            elide: Text.ElideMiddle
        }
        Label {
            Layout.fillWidth: true
            text: "使用 " + feedback.backend.videoOpen.playerName
                + (feedback.backend.videoOpen.synchronizes ? " 打开并自动匹配弹幕" : " 打开视频（仅打开）")
            font.pixelSize: Ui.bodySize
            color: Ui.secondaryText
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
        }
    }
    DropArea {
        id: detector
        objectName: "videoDropArea"
        anchors.fill: parent
        onEntered: drag => {
            const valid = drag.hasUrls && (drag.supportedActions & (Qt.CopyAction | Qt.LinkAction)) !== 0
                && feedback.backend.videoOpen.accepts(drag.urls)
            drag.accepted = valid
            if (!valid) { feedback.reset(); return }
            feedback.fileName = feedback.backend.videoOpen.displayName(drag.urls)
            feedback.engaged = true
        }
        onExited: feedback.reset()
        onDropped: drop => {
            const valid = feedback.enabled && drop.hasUrls
                && (drop.supportedActions & (Qt.CopyAction | Qt.LinkAction)) !== 0
                && feedback.backend.videoOpen.accepts(drop.urls)
            feedback.reset()
            if (!valid) { drop.accepted = false; return }
            drop.accept((drop.supportedActions & Qt.CopyAction) ? Qt.CopyAction : Qt.LinkAction)
            feedback.backend.videoOpen.open(drop.urls)
        }
    }
    Shortcut {
        sequence: "Escape"
        enabled: feedback.dragActive
        onActivated: feedback.reset()
    }
}
