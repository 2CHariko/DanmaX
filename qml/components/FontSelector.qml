import QtQuick
import QtQuick.Controls.FluentWinUI3
import QtQuick.Layouts
import "../theme"

ColumnLayout {
    id: selector
    required property var families
    required property string family
    signal familySelected(string name)
    spacing: 8
    Component.onCompleted: fontBox.editText = family
    onFamilyChanged: fontBox.editText = family
    onFamiliesChanged: Qt.callLater(function() {
        if (!fontBox.activeFocus && !fontBox.popup.visible)
            fontBox.editText = selector.family
    })

    function commit() {
        const name = fontBox.editText.trim()
        if (name.length > 0 && name !== family)
            familySelected(name)
        if (name.length === 0)
            fontBox.editText = family
    }
    readonly property bool listed: families.some(function(name) {
        return name.toLowerCase() === selector.family.toLowerCase()
    })
    AppComboBox {
        id: fontBox
        objectName: "fontFamilyCombo"
        Layout.preferredWidth: 240
        model: selector.families
        editable: true
        currentIndex: selector.families.indexOf(selector.family)
        Accessible.name: "弹幕字体"
        Accessible.description: "选择系统字体，或输入名称并按 Enter 确认。"
        onActivated: selector.familySelected(currentText)
        onAccepted: selector.commit()
    }
    Connections {
        target: fontBox.contentItem
        function onEditingFinished() { selector.commit() }
    }
    Label {
        Layout.preferredWidth: 240
        Layout.maximumWidth: 240
        wrapMode: Text.WrapAnywhere
        text: "弹幕预览 Aa 123"
        font.family: selector.family
        font.weight: Font.DemiBold
        font.pixelSize: Ui.subtitleSize
        Accessible.name: "弹幕字体预览"
    }
    Label {
        Layout.preferredWidth: 240
        Layout.maximumWidth: 240
        visible: !selector.listed
        text: "系统列表中未找到此名称；可能使用字体别名或回退字体。"
        wrapMode: Text.WordWrap
        font.pixelSize: Ui.captionSize
    }
}
