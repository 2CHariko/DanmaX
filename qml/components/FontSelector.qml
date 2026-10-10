import QtQuick
import QtQuick.Controls.FluentWinUI3
import QtQuick.Layouts
import "../theme"
import "../i18n"

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
        renderFontFamily: true
        font.family: selector.family
        Layout.preferredWidth: 240
        model: selector.families
        editable: true
        currentIndex: selector.families.indexOf(selector.family)
        Accessible.name: I18n.fontSelector.accessibleName
        Accessible.description: I18n.fontSelector.accessibleDesc
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
        visible: !selector.listed
        text: I18n.fontSelector.notFoundHint
        wrapMode: Text.WordWrap
        }
}
