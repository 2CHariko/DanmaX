import QtQuick
import "../theme"
import "../components"
import QtQuick.Controls.FluentWinUI3
import QtQuick.Layouts
import QtQuick.Dialogs

PageFrame {
    id: page
    required property QtObject backend
    property int sourceTab: 0
    property bool independent: false
    readonly property bool effectiveManual: backend.running ? backend.manualMode : independent
    readonly property string blockedReason: backend.loading ? "正在载入弹幕，请稍候。" : backend.total <= 0 ? "先选择并载入弹幕，再开始播放。" : !effectiveManual && backend.settings.values.targetSession.length === 0 ? "请选择播放器，或切换为独立播放。" : ""
    signal openSettings()
    title: "播放"
    description: "选择弹幕来源，连接播放器，然后开始播放。"
    function formatTime(value) {
        const seconds = Math.max(0, Math.floor(value || 0))
        const minutes = Math.floor(seconds / 60)
        return (seconds >= 3600 ? Math.floor(minutes / 60) + ":" + String(minutes % 60).padStart(2, "0") : String(minutes).padStart(2, "0")) + ":" + String(seconds % 60).padStart(2, "0")
    }
    SectionCard {
        title: "弹幕来源"
        TabBar {
            id: sourceTabs
            objectName: "danmakuSourceTabs"
            currentIndex: page.sourceTab
            Layout.fillWidth: true
            Accessible.name: "弹幕来源"
            TabButton { text: "本地 XML"; width: implicitWidth; icon.color: palette.buttonText }
            TabButton { text: "在线搜索"; width: implicitWidth; icon.color: palette.buttonText }
            TabButton { text: "已缓存"; width: implicitWidth; icon.color: palette.buttonText; onClicked: page.backend.library.refreshCache() }
        }
        ColumnLayout {
            visible: sourceTabs.currentIndex === 0
            Layout.fillWidth: true
            spacing: 8
            Label { text: "XML 弹幕文件" }
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
                placeholderText: "输入或选择 Bilibili XML 文件路径"
                Accessible.name: "弹幕文件路径"
                onAccepted: if (!page.backend.loading) page.backend.loadFile(text)
            }
            Flow {
                Layout.preferredWidth: 216
                Layout.fillWidth: parent.columns === 1
                spacing: 8
                Button { text: "选择文件"; onClicked: filePicker.open() }
                Button { text: page.backend.loading ? "取消载入" : "载入弹幕"; enabled: page.backend.loading || pathInput.text.trim().length > 0; onClicked: page.backend.loading ? page.backend.cancelLoad() : page.backend.loadFile(pathInput.text) }
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
            Label { visible: text.length > 0; text: page.backend.sourceTitle; font.weight: Font.DemiBold; Layout.fillWidth: true; wrapMode: Text.WrapAnywhere; Accessible.name: "当前弹幕：" + text }
            Label { text: "已载入 " + page.backend.total + " 条弹幕"; color: Ui.secondaryText }
        }
    }
    SectionCard {
        title: "播放方式"
        Flow {
            Layout.fillWidth: true
            spacing: 16
            RadioButton { objectName: "syncMode"; text: "跟随播放器"; checked: !page.effectiveManual; enabled: !page.backend.running; onClicked: page.independent = false }
            RadioButton { objectName: "manualMode"; text: "独立播放"; checked: page.effectiveManual; enabled: !page.backend.running; onClicked: page.independent = true }
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
                displayText: currentIndex >= 0 ? currentText : "等待媒体会话，请先在播放器中播放视频"
                Accessible.name: "目标播放器"
                onActivated: page.backend.selectSession(currentValue)
                function syncSelection() { currentIndex = indexOfValue(page.backend.settings.values.targetSession) }
                onModelChanged: syncSelection()
                Component.onCompleted: syncSelection()
                Connections { target: page.backend.settings; function onChanged() { sessionBox.syncSelection() } }
            }
            Label { text: "自动刷新媒体会话，跟随播放器的暂停和跳转。"; font.pixelSize: Ui.captionSize; color: Ui.secondaryText; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            Disclosure {
                objectName: "sessionDisclosure"
                title: "手动指定应用 ID"
                TextField {
                    objectName: "sessionIdInput"
                    Layout.fillWidth: true
                    placeholderText: "播放器应用 ID"
                    text: page.backend.settings.values.targetSession
                    Accessible.name: "播放器应用 ID"
                    onEditingFinished: page.backend.selectSession(text)
                }
            }
        }
        Label { visible: page.effectiveManual; text: "使用独立时间轴，可在下方暂停或调整进度。"; color: Ui.secondaryText; wrapMode: Text.WordWrap; Layout.fillWidth: true }
    }
    SectionCard {
        title: "播放控制"
        Label { text: page.backend.status; wrapMode: Text.WordWrap; Layout.fillWidth: true; Accessible.name: "播放状态：" + text }
        Label { visible: !page.backend.running && text.length > 0; text: page.blockedReason; color: Ui.secondaryText; wrapMode: Text.WordWrap; Layout.fillWidth: true }
        Flow {
            Layout.fillWidth: true
            spacing: 8
            Button { objectName: "startPlayback"; visible: !page.backend.running; text: "开始播放"; highlighted: true; enabled: page.blockedReason.length === 0; onClicked: page.backend.start(page.independent) }
            Button { objectName: "pausePlayback"; visible: page.backend.running && page.backend.manualMode; text: page.backend.playing ? "暂停" : "继续"; onClicked: page.backend.togglePause() }
            Button { objectName: "stopPlayback"; visible: page.backend.running || page.backend.total > 0 || page.backend.loading || page.backend.library.busy; text: "停止并卸载"; Accessible.description: "停止后需重新载入弹幕"; onClicked: page.backend.stop() }
        }
        ColumnLayout {
            visible: page.backend.running
            Layout.fillWidth: true
            spacing: 8
            Label { text: page.backend.mediaTitle; visible: text.length > 0; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            Label { text: page.formatTime(page.backend.position) + " / " + page.formatTime(page.backend.duration) + (page.backend.manualMode ? "（预计）" : " · 由播放器控制"); wrapMode: Text.WordWrap; Layout.fillWidth: true }
            Slider { visible: page.backend.manualMode; Layout.fillWidth: true; from: 0; to: Math.max(1, page.backend.duration); value: page.backend.position; Accessible.name: "独立播放进度"; onMoved: page.backend.seek(value) }
            ProgressBar { visible: !page.backend.manualMode; Layout.fillWidth: true; from: 0; to: Math.max(1, page.backend.duration); value: page.backend.position; Accessible.name: "播放器进度" }
            Disclosure {
                title: "运行详情"
                Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: "在屏 " + (page.backend.metrics.active || 0) + " · 丢弃 " + (page.backend.metrics.dropped || 0) + " · 内存 " + Number(page.backend.metrics.memoryMiB || 0).toFixed(1) + " MiB"; color: Ui.secondaryText }
            }
        }
    }
    FileDialog { id: filePicker; title: "选择弹幕 XML"; nameFilters: ["弹幕文件 (*.xml)"]; onAccepted: page.backend.loadFile(selectedFile.toString()) }
}
