import QtQuick
import QtQuick.Controls.FluentWinUI3
import QtQuick.Layouts
import "../theme"
import "../i18n"

ColumnLayout {
    id: railRoot
    property string title: ""
    property var model: []
    property var weekdayTabs: []
    property int selectedWeekday: 0
    signal cardClicked(var anime)
    signal weekdaySelected(int day)

    spacing: 8
    Layout.fillWidth: true

    // 轨道顶部标题与控制栏
    RowLayout {
        Layout.fillWidth: true
        spacing: 12

        Label {
            text: railRoot.title
            font.pixelSize: Ui.subtitleSize
            font.weight: Font.DemiBold
            color: Ui.textColor
        }

        Item { Layout.fillWidth: true }

        RowLayout {
            spacing: 4
            Button {
                flat: true
                icon.source: Ui.icon("chevron-left")
                Accessible.name: I18n.home.railScrollLeft
                enabled: !cardList.atXBeginning
                onClicked: cardList.positionViewAtIndex(Math.max(0, cardList.indexAt(cardList.contentX, 1) - 3), ListView.Beginning)
            }
            Button {
                flat: true
                icon.source: Ui.icon("chevron-right")
                Accessible.name: I18n.home.railScrollRight
                enabled: !cardList.atXEnd
                onClicked: cardList.contentX = Math.min(Math.max(0, cardList.contentWidth - cardList.width), cardList.contentX + cardList.width * 0.8)
            }
        }
    }

        Flow {
            Layout.fillWidth: true
            Layout.preferredHeight: childrenRect.height
            visible: Boolean(railRoot.weekdayTabs && railRoot.weekdayTabs.length > 0)
            spacing: 4

            Repeater {
                model: railRoot.weekdayTabs
                delegate: Button {
                    id: tabBtn
                    required property string modelData
                    required property int index

                    readonly property bool active: (index + 1) === railRoot.selectedWeekday

                    objectName: "weekday" + (index + 1)
                    text: modelData
                    highlighted: active
                    Accessible.selected: active
                    onClicked: railRoot.weekdaySelected(index + 1)
                }
            }
        }


    // 横向卡片列表（支持视图复用与触控板横向平滑惯性滚动）
    Item {
        Layout.fillWidth: true
        Layout.preferredHeight: Math.round(218 * Ui.textScale)

        ListView {
            id: cardList
            anchors.fill: parent
            orientation: ListView.Horizontal
            spacing: 12
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            flickDeceleration: 2000
            model: railRoot.model

            delegate: AnimeCard {
                required property var modelData
                objectName: "animeCard"
                animeData: modelData
                onClicked: if (animeData) railRoot.cardClicked(animeData)
            }

            ScrollBar.horizontal: ScrollBar {
                id: hbar
                policy: ScrollBar.AsNeeded
            }

            // 滚轮横向平滑滚动支持
            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.NoButton
                onWheel: wheel => {
                    const delta = wheel.angleDelta.x || wheel.angleDelta.y
                    const next = Math.max(0, Math.min(Math.max(0, cardList.contentWidth - cardList.width), cardList.contentX - delta))
                    wheel.accepted = delta !== 0 && next !== cardList.contentX
                    if (wheel.accepted) cardList.contentX = next
                }
            }
        }

        // 空状态提示
        Label {
            anchors.centerIn: parent
            visible: Boolean(!railRoot.model || railRoot.model.length === 0)
            text: I18n.home.emptyCalendar
            color: Ui.secondaryText
            font.pixelSize: Ui.bodySize
        }
    }
}
