import QtQuick
import QtQuick.Window
import QtQuick.Layouts
import QtQuick.Controls.Basic as Basic
import RinUI as R

Window {
    width: 900; height: 700; visible: true
    title: "RinUI — 控件验证"
    color: probeDark ? "#202020" : "#f3f3f3"
    Flickable {
        id: scroll; objectName: "pageScroll"
        anchors.fill: parent; anchors.margins: 32
        clip: true; contentHeight: column.height
        Basic.ScrollBar.vertical: R.ScrollBar { objectName: "pageScrollBar" }
        ColumnLayout {
            id: column; width: parent.width - 24; spacing: 4
            R.Text { text: "个性化  ›  开始"; font.pixelSize: 28; Layout.bottomMargin: 20 }
            R.SettingCard {
                Layout.fillWidth: true; implicitHeight: 72
                title: "已固定"; description: "你已固定到开始菜单的自定义应用"
                icon.name: "ic_fluent_pin_20_regular"
                R.Switch { checked: true }
            }
            R.SettingExpander {
                objectName: "expander"; Layout.fillWidth: true
                title: "最近使用"; description: "最近添加的应用和打开的项目"
                icon.name: "ic_fluent_history_20_regular"
                R.SettingItem { Layout.fillWidth: true; implicitHeight: 52; R.CheckBox { text: "显示最近添加的应用"; checked: true } }
                R.SettingItem { Layout.fillWidth: true; implicitHeight: 52; R.CheckBox { text: "显示最近使用的文件" } }
            }
            R.SettingCard {
                Layout.fillWidth: true; implicitHeight: 72
                title: "开始菜单大小"; icon.name: "ic_fluent_resize_20_regular"
                R.ComboBox { model: ["自动（默认）", "小", "大"]; currentIndex: 1; implicitWidth: 150 }
            }
            R.SettingCard {
                Layout.fillWidth: true; implicitHeight: 72
                title: "长列表"; description: "40 个选项，当前项在末尾"
                R.ComboBox { objectName: "longCombo"; model: Array.from({length: 40}, (_, i) => "选项 " + (i + 1)); currentIndex: 39; implicitWidth: 150 }
            }
            Repeater {
                model: 12
                R.SettingCard { required property int index; Layout.fillWidth: true; implicitHeight: 72; title: "设置项目 " + (index + 1); description: "用于验证页面滚动与卡片边框"; R.Switch { } }
            }
        }
    }
}
