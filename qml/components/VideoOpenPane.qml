import QtQuick
import QtQuick.Controls.FluentWinUI3
import QtQuick.Layouts
import QtQuick.Dialogs
import "../theme"

SettingsRow {
    id: pane
    required property QtObject backend
    title: "使用 PotPlayer 打开视频"
    description: backend.videoOpen.pending ? backend.videoOpen.status
        : backend.videoOpen.active ? backend.automatic.status
        : "拖入一个本地视频，自动打开播放器并匹配弹幕。也可使用打开按钮。"
    Button {
        objectName: "openVideoButton"
        text: "打开视频"
        Accessible.name: "选择视频并使用 PotPlayer 播放"
        onClicked: videoDialog.open()
    }
    Button {
        objectName: "confirmCurrentVideoButton"
        visible: pane.backend.videoOpen.needsConfirmation
        text: "使用当前视频"
        Accessible.description: "确认 PotPlayer 当前播放的就是所选视频"
        onClicked: pane.backend.videoOpen.confirmCurrent()
    }
    Button {
        objectName: "cancelVideoOpenButton"
        visible: pane.backend.videoOpen.pending || pane.backend.videoOpen.needsPlayer
        text: "取消"
        onClicked: pane.backend.videoOpen.cancel()
    }
    FileDialog {
        id: videoDialog
        objectName: "videoFileDialog"
        title: "选择视频"
        fileMode: FileDialog.OpenFile
        nameFilters: ["视频 (*.mp4 *.mkv *.avi *.mov *.wmv *.flv *.webm *.m4v *.ts *.m2ts)"]
        onAccepted: pane.backend.videoOpen.open([selectedFile])
    }
}
