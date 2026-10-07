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
    property real popupRoom: Ui.fontPopupHeight
    property bool popupBelow: true
    property var listScrollBar: null
    function placePopup() {
        const window = fontBox.Window.window
        if (!window)
            return
        const origin = fontBox.mapToItem(window.contentItem, 0, 0)
        const below = Math.max(0, window.height - origin.y - fontBox.height - fontBox.popup.bottomMargin)
        const above = Math.max(0, origin.y - fontBox.popup.topMargin)
        const desired = Math.min(Ui.fontPopupHeight,
            fontBox.popup.contentItem.implicitHeight + fontBox.popup.topPadding + fontBox.popup.bottomPadding)
        popupBelow = below >= desired || below >= above
        popupRoom = popupBelow ? below : above
    }

    Component {
        id: fontScrollBar
        ScrollBar {
            objectName: "fontFamilyScrollBar"
            policy: size < 1 ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
            active: true
            palette.mid: fontBox.palette.text
            palette.dark: fontBox.palette.accent
            Accessible.name: "字体列表滚动条"
        }
    }
    Binding {
        target: fontBox.popup
        property: "height"
        value: Math.min(Ui.fontPopupHeight, selector.popupRoom,
            fontBox.popup.contentItem.implicitHeight + fontBox.popup.topPadding + fontBox.popup.bottomPadding)
    }
    Binding {
        target: fontBox.popup
        property: "y"
        value: selector.popupBelow ? fontBox.height : -fontBox.popup.height
    }
    Connections {
        target: fontBox.popup
        function onAboutToShow() { selector.placePopup() }
        function onOpened() {
            if (fontBox.currentIndex >= 0)
                fontBox.popup.contentItem.positionViewAtIndex(fontBox.currentIndex, ListView.Contain)
        }
    }
    Connections {
        target: fontBox.Window.window
        function onHeightChanged() { if (fontBox.popup.visible) selector.placePopup() }
        function onWidthChanged() { if (fontBox.popup.visible) selector.placePopup() }
    }

    ComboBox {
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
        delegate: ItemDelegate {
            required property string modelData
            required property int index
            width: ListView.view.width - (selector.listScrollBar && selector.listScrollBar.visible
                ? selector.listScrollBar.width : 0)
            text: modelData
            highlighted: fontBox.highlightedIndex === index
            hoverEnabled: fontBox.hoverEnabled
        }
        Component.onCompleted: {
            const list = popup.contentItem
            selector.listScrollBar = fontScrollBar.createObject(list)
            list.ScrollBar.vertical = selector.listScrollBar
        }
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
