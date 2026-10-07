import QtQuick
import "../theme"
import QtQuick.Controls.FluentWinUI3
import QtQuick.Layouts
import "../components"
PageFrame {
    id: page
    required property QtObject backend
    readonly property var values: backend.settings.values
    title: "设置"
    description: "修改后即时生效并保存。"
    function focusOnlineSettings() { reveal(serverInput) }
    function put(key, value) { backend.settings.setValue(key, value) }

        SettingsSection {
            title: "应用外观"; Layout.fillWidth: true
            SettingsRow {
                title: "应用主题"
                AppComboBox { model: ["跟随系统", "浅色", "深色"]; currentIndex: ["system","light","dark"].indexOf(page.values.theme); Accessible.name: "应用主题"; onActivated: page.put("theme", ["system","light","dark"][currentIndex]) }
            }
        }
        SettingsSection {
            title: "弹幕样式"; Layout.fillWidth: true
            SettingsRow {
                title: "弹幕字体"
                description: "选择系统字体，或输入名称定位并确认；缺失的字符由 Qt 回退。"
                FontSelector { families: page.backend.fontFamilies; family: page.values.fontFamily; onFamilySelected: function(name) { page.put("fontFamily", name) } }
            }
            SettingsRow {
                title: "字号"
                description: "普通弹幕的基准字号；小字和大字按文件中的比例缩放。"
                SpinBox { from: 10; to: 72; value: page.values.fontSize; editable: true; Accessible.name: "弹幕字号"; onValueModified: page.put("fontSize", value) }
            }
            SettingsRow {
                title: "描边宽度"
                SpinBox { from: 0; to: 6; value: page.values.strokeWidth; editable: true; Accessible.name: "描边宽度"; onValueModified: page.put("strokeWidth", value) }
            }
            SettingsRow {
                title: "不透明度"
                SpinBox { from: 5; to: 100; value: Math.round(page.values.opacity*100); editable: true; Accessible.name: "弹幕不透明度百分比"; onValueModified: page.put("opacity", value/100) }
                Label { text: "%" }
            }
        }
        SettingsSection {
            title: "弹幕播放"; Layout.fillWidth: true
            Repeater {
                model: [
                    {key:"speed",title:"滚动速度（逻辑像素 / 秒）",min:30,max:1500},
                    {key:"maxActive",title:"最大在屏弹幕",min:50,max:5000},
                    {key:"maxTracks",title:"最大轨道数",min:1,max:60}]
                SettingsRow {
                    required property var modelData
                    title: modelData.title
                    SpinBox { from: modelData.min; to: modelData.max; value: page.values[modelData.key]; editable: true; Accessible.name: modelData.title; onValueModified: page.put(modelData.key,value) }
                }
            }
            SettingsRow { title: "固定弹幕时长（秒）"; SpinBox { from:1;to:30;value:page.values.fixedSeconds;editable:true;Accessible.name:"固定弹幕时长";onValueModified:page.put("fixedSeconds",value) } }
            SettingsRow { title: "轨道额外行距（%）"; SpinBox { from:0;to:200;value:Math.round(page.values.lineSpacing*100);editable:true;Accessible.name:"轨道额外行距";onValueModified:page.put("lineSpacing",value/100) } }
            SettingsRow { title: "允许弹幕重叠"; description: "优先使用空闲轨道；轨道满时，开启则允许重叠，关闭则丢弃新弹幕。"; Switch { checked:page.values.overlap;Accessible.name:"允许弹幕重叠";onToggled:page.put("overlap",checked) } }
        }
        SettingsSection {
            title: "同步与窗口"; Layout.fillWidth: true
            SettingsRow { title: "时间偏移（0.1 秒）"; description:"正值提前显示弹幕。"; SpinBox { from:-1200;to:1200;value:Math.round(page.values.timeOffset*10);editable:true;Accessible.name:"时间偏移十分之一秒";onValueModified:page.put("timeOffset",value/10) } }
            SettingsRow { title: "显示器"; AppComboBox { model:page.backend.screens;currentIndex:Math.min(page.values.screenIndex,count-1);implicitWidth:240;Accessible.name:"弹幕显示器";onActivated:page.put("screenIndex",currentIndex) } }
            SettingsRow { title: "置顶策略"; AppComboBox { model:["不置顶","置顶","周期保持置顶","兼容保持置顶"];currentIndex:page.values.onTop;Accessible.name:"置顶策略";onActivated:page.put("onTop",currentIndex) } }
            SettingsRow { title: "仅播放器在前台时显示"; description:"无法关联媒体应用与前台进程时请关闭此项。"; Switch { checked:page.values.foregroundOnly;Accessible.name:"仅播放器在前台显示";onToggled:page.put("foregroundOnly",checked) } }
        }
        SettingsSection {
            title: "在线弹幕"; Layout.fillWidth: true
            SettingsRow {
                title: "兼容服务地址"
                description: "填写允许匿名访问的弹弹play兼容服务基础地址，支持 HTTP/HTTPS 和路径前缀。"
                TextField {
                    id: serverInput
                    objectName: "danmakuServerInput"
                    Layout.fillWidth: true
                    implicitWidth: 280
                    text: page.values.danmakuServer
                    placeholderText: "https://服务器/路径前缀"
                    Accessible.name: "在线弹幕兼容服务地址"
                    onEditingFinished: {
                        page.backend.settings.setValue("danmakuServer", text)
                        text = Qt.binding(() => page.values.danmakuServer)
                    }
                }
            }
            Label { text: "缓存长期保留，可在播放页离线选择、重新下载或删除；本版本不提供在线账号登录。"; Layout.fillWidth: true; wrapMode: Text.WordWrap }
        }
        SettingsSection {
            title: "高级设置"
        Disclosure {
            title: "文字缓存"; Layout.fillWidth: true
            SettingsRow {
                title: "预算模式"
                description: "自动按当前弹幕需求增长，持续低负载后收缩；手动使用固定预算。"
                AppComboBox { model: ["自动", "手动"]; currentIndex: page.values.textureBudgetAuto ? 0 : 1; Accessible.name: "文字缓存预算模式"; onActivated: page.put("textureBudgetAuto", currentIndex === 0) }
            }
            SettingsRow {
                title: page.values.textureBudgetAuto ? "容量上限（MiB）" : "固定预算（MiB）"
                description: "只限制文字图片内容，不提前分配；超限弹幕仍以文字显示。"
                SpinBox { from: 32; to: 1024; stepSize: 32; value: page.values.textureBudgetMiB; editable: true; Accessible.name: "文字缓存容量 MiB"; onValueModified: page.put("textureBudgetMiB", value) }
            }
        }
        Disclosure {
            title: "诊断"; Layout.fillWidth: true
            SettingsRow { title: "显示调试信息"; Switch { checked:page.values.debug;Accessible.name:"显示调试信息";onToggled:page.put("debug",checked) } }
            SettingsRow { title: "调试信息位置"; AppComboBox { model:["左上","右上","左下","右下"];currentIndex:["top_left","top_right","bottom_left","bottom_right"].indexOf(page.values.debugPosition);Accessible.name:"调试位置";onActivated:page.put("debugPosition",["top_left","top_right","bottom_left","bottom_right"][currentIndex]) } }
            SettingsRow { title: "日志级别"; AppComboBox { model:["DEBUG","INFO","WARNING","ERROR"];currentIndex:model.indexOf(page.values.logLevel);Accessible.name:"日志级别";onActivated:page.put("logLevel",currentText) } }
            SettingsRow { title: "写入日志文件"; description:"单文件 2 MiB，保留一份轮转备份。"; Switch { checked:page.values.logToFile;Accessible.name:"写入日志文件";onToggled:page.put("logToFile",checked) } }
        }
        }
        SettingsSection {
            title: "配置与关于"; Layout.fillWidth: true
            Flow { Layout.fillWidth:true;spacing:8
                Button { text:"恢复默认设置";onClicked:resetDialog.open() }
            }
            Label { text:"配置使用带中文说明的 settings.ini。手动编辑前请退出程序；旧配置不导入。";wrapMode:Text.WordWrap;Layout.fillWidth:true }
            Label { text:"Local Danmaku 0.2 · C++20 / Qt 6.11\n便携版的配置和日志保存在程序旁；开发运行使用指定数据目录。";wrapMode:Text.WordWrap;Layout.fillWidth:true }
        }
    Dialog { id:resetDialog;title:"恢复默认设置？";modal:true;anchors.centerIn:parent;standardButtons:Dialog.Ok|Dialog.Cancel;Label{text:"将覆盖当前设置，原弹幕文件不会删除。"} onAccepted:page.backend.settings.reset() }
}
