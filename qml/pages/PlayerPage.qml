import QtQuick
import "../theme"
import QtQuick.Controls.FluentWinUI3
import QtQuick.Layouts
import QtQuick.Dialogs
ScrollView {
    id: page
    required property QtObject backend
    contentWidth: availableWidth
    ColumnLayout {
        width: page.availableWidth
        spacing: 16
        Label { text: "播放"; font.pixelSize: Ui.titleSize; font.weight: Font.DemiBold }
        Label { text: page.backend.status; wrapMode: Text.WordWrap; Layout.fillWidth: true; Accessible.name: "播放状态：" + text }
        Label { text: "弹幕文件"; font.weight: Font.DemiBold }
        RowLayout {
            Layout.fillWidth: true
            TextField {
                id: pathInput
                Layout.fillWidth: true
                text: page.backend.filePath
                placeholderText: "选择 Bilibili XML 弹幕文件"
                Accessible.name: "弹幕文件路径"
                onAccepted: page.backend.loadFile(text)
            }
            Button { text: "浏览"; onClicked: filePicker.open() }
            Button { text: page.backend.loading ? "取消" : "加载"; onClicked: page.backend.loading ? page.backend.cancelLoad() : page.backend.loadFile(pathInput.text) }
        }
        ProgressBar { visible: page.backend.loading; Layout.fillWidth: true; indeterminate: page.backend.loadProgress < 0; from: 0; to: 100; value: Math.max(0,page.backend.loadProgress) }
        Label { text: "已载入 " + page.backend.total + " 条弹幕" }
        Label { text: "播放器"; font.weight: Font.DemiBold; Layout.topMargin: 8 }
        ComboBox {
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
        TextField {
            Layout.fillWidth: true
            placeholderText: "也可输入播放器应用 ID"
            text: page.backend.settings.values.targetSession
            Accessible.name: "播放器应用 ID"
            onEditingFinished: page.backend.selectSession(text)
        }
        Label { text: "媒体会话自动刷新。同步模式跟随外部播放器的暂停和跳转。"; wrapMode: Text.WordWrap; Layout.fillWidth: true; font.pixelSize: Ui.captionSize }
        Flow {
            Layout.fillWidth: true
            spacing: 8
            Button { text: "同步播放"; enabled: !page.backend.loading && page.backend.total > 0 && page.backend.settings.values.targetSession.length > 0; onClicked: page.backend.start(false) }
            Button { text: "独立播放"; enabled: !page.backend.loading && page.backend.total > 0; onClicked: page.backend.start(true) }
            Button { text: page.backend.playing ? "暂停" : "继续"; enabled: page.backend.running && page.backend.manualMode; onClicked: page.backend.togglePause() }
            Button { text: "停止"; enabled: page.backend.running || page.backend.total > 0; Accessible.description: "停止播放并卸载弹幕，再次播放需要重新加载"; onClicked: page.backend.stop() }
        }
        Label { text: page.backend.mediaTitle; visible: text.length > 0; wrapMode: Text.WordWrap; Layout.fillWidth: true }
        Label { text: page.backend.position.toFixed(1) + " / " + page.backend.duration.toFixed(1) + (page.backend.manualMode ? " 秒（预计，播放至弹幕退场）" : " 秒") }
        Slider {
            Layout.fillWidth: true
            from: 0; to: Math.max(1, page.backend.duration)
            value: page.backend.position
            enabled: page.backend.manualMode && page.backend.total > 0
            Accessible.name: "独立播放进度"
            onMoved: page.backend.seek(value)
        }
        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: "在屏 " + (page.backend.metrics.active || 0) + " · 丢弃 " + (page.backend.metrics.dropped || 0)
                + " · 内存 " + Number(page.backend.metrics.memoryMiB || 0).toFixed(1) + " MiB"
        }
    }
    FileDialog { id: filePicker; title: "选择弹幕 XML"; nameFilters: ["弹幕文件 (*.xml)"]; onAccepted: page.backend.loadFile(selectedFile.toString()) }
}
