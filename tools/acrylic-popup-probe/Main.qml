import QtQuick
import QtQuick.Controls.FluentWinUI3

ApplicationWindow {
    id: root
    objectName: "probeWindow"
    visible: true
    flags: Qt.Window | Qt.WindowStaysOnTopHint
    width: 840; height: 680
    title: "Qt 下拉对齐 / Acrylic 最小验证"
    color: Application.styleHints.colorScheme === Qt.Dark ? "#202020" : "#F3F3F3"
    property bool pattern: true
    property bool warmPattern: false
    function openChoice(which) {
        const choice = which === 0 ? shortChoice : longChoice
        choice.forceActiveFocus()
        choice.popup.open()
    }
    function closeChoices() { shortChoice.popup.close(); longChoice.popup.close(); menu.close() }
    function openMenu() { menu.popup(menuButton, 0, menuButton.height) }
    function theme(dark) { Application.styleHints.colorScheme = dark ? Qt.Dark : Qt.Light }
    Label { x: 24; y: 24; text: "独立验证：选中项对齐 + Popup.Window + DWM Acrylic"; font.pixelSize: 20 }
    Label { x: 24; y: 64; text: "条纹用于检查真实模糊；此窗口不修改 DanmaX 设置。" }
    Button { x: 24; y: 100; text: "切换浅深主题"; onClicked: root.theme(Application.styleHints.colorScheme !== Qt.Dark) }
    Switch { x: 220; y: 100; text: "请求 Acrylic"; checked: bridge.requested; onToggled: bridge.requested = checked }
    Switch { x: 420; y: 100; text: "条纹背景"; checked: root.pattern; onToggled: root.pattern = checked }
    Label { x: 24; y: 156; text: "API 接受数仅为接口证据；是否真实模糊须看桌面合成截图。" }
    Item {
        x: 0; y: 204; width: root.width; height: 320; visible: root.pattern
        Repeater {
            model: Math.ceil(root.width / 8)
            Rectangle { required property int index; x: index * 8; width: 8; height: 320; color: root.warmPattern ? (index % 2 ? "#CC2222" : "#FFBB22") : (index % 2 ? "#111111" : "#EEEEEE") }
        }
    }
    ProbeComboBox {
        id: shortChoice; objectName: "shortChoice"
        x: 120; y: 300; width: 280
        model: ["1 - 无上下文", "2 - 即时上下文", "3 - 即时上下文名称和类型", "4 - 新控件的完整上下文", "5 - 旧控件和新控件的完整上下文"]
        currentIndex: 2
    }
    ProbeComboBox {
        id: longChoice; objectName: "longChoice"
        x: 480; y: 300; width: 280
        model: 120
        textRole: ""
        displayText: "第 " + (currentIndex + 1) + " 项"
        currentIndex: 59
    }
    Button { id: menuButton; objectName: "menuButton"; x: 120; y: 550; text: "弹出菜单"; onClicked: root.openMenu() }
    Menu {
        id: menu; objectName: "probeMenu"
        popupType: Popup.Window
        width: 240
        topInset: 0; bottomInset: 0; leftInset: 0; rightInset: 0
        background: ProbeSurface {}
        enter: Transition {}
        exit: Transition {}
        MenuItem { objectName: "visibilityItem"; text: "显示弹幕"; checkable: true; checked: true }
        MenuItem { text: "搜索弹幕" }
    }
    TextField { objectName: "outsideInput"; x: 420; y: 550; width: 340; placeholderText: "点击这里检查关闭与焦点返回" }
}
