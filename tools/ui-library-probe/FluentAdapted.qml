import QtQuick
import QtQuick.Window
import QtQuick.Layouts
import QtQuick.Controls.Basic as Basic
import FluentUI

Window {
    id: root
    property bool currentDark: FluTheme.dark
    function toggleTheme() { FluTheme.darkMode = FluTheme.dark ? FluThemeType.Light : FluThemeType.Dark }
    function scaleText() {
        FluTextStyle.Body = Qt.font({family: FluTextStyle.family, pixelSize: 21})
    }
    width: 900; height: 700; visible: true
    title: "QML FluentUI — 适配验证"
    color: FluTheme.windowBackgroundColor
    Component.onCompleted: FluTheme.darkMode = probeDark ? FluThemeType.Dark : FluThemeType.Light
    Flickable {
        id: scroll; objectName: "pageScroll"
        anchors.fill: parent; anchors.margins: 32
        clip: true; contentHeight: column.height
        Basic.ScrollBar.vertical: FluScrollBar { objectName: "pageScrollBar" }
        ColumnLayout {
            id: column; width: parent.width - 24; spacing: 4
            FluText { text: "个性化  ›  开始"; font.pixelSize: 28; Layout.bottomMargin: 20 }
            FluFrame {
                Layout.fillWidth: true; implicitHeight: 72; padding: 16
                contentItem: RowLayout { FluText { text: "已固定"; Layout.fillWidth: true } FluToggleSwitch { checked: true } }
            }
            ProbeExpander {
                objectName: "expander"; Layout.fillWidth: true
                headerText: "最近使用"; headerHeight: 68; contentHeight: 110
                Column { anchors.margins: 16; anchors.fill: parent; spacing: 12
                    FluCheckBox { objectName: "innerCheck"; text: "显示最近添加的应用"; checked: true }
                    FluCheckBox { text: "显示最近使用的文件" }
                }
            }
            FluFrame {
                Layout.fillWidth: true; implicitHeight: 72; padding: 16
                contentItem: RowLayout { FluText { text: "开始菜单大小"; Layout.fillWidth: true } ProbeComboBox { model: ["自动（默认）", "小", "大"]; currentIndex: 1; implicitWidth: 150 } }
            }
            FluFrame {
                Layout.fillWidth: true; implicitHeight: 72; padding: 16
                contentItem: RowLayout { FluText { text: "长列表（40 项）"; Layout.fillWidth: true } ProbeComboBox { objectName: "longCombo"; model: Array.from({length:40}, (_,i) => "选项 " + (i+1)); currentIndex:39; implicitWidth:150 } }
            }
            Repeater { model: 12
                FluFrame { required property int index; Layout.fillWidth: true; implicitHeight: 72; padding: 16
                    contentItem: RowLayout { FluText { text: "设置项目 " + (index+1); Layout.fillWidth: true } FluToggleSwitch {} }
                }
            }
        }
    }
}



