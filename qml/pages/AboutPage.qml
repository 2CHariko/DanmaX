import QtQuick
import QtQuick.Controls.FluentWinUI3
import QtQuick.Layouts
import "../components"
import "../theme"
import "../i18n"

PageFrame {
    id: page
    title: I18n.about.title

    ColumnLayout {
        Layout.fillWidth: true
        spacing: Ui.sectionGap

        // 1. 顶部应用品牌信息（全视口真正水平居中）
        ColumnLayout {
            Layout.fillWidth: true
            Layout.topMargin: 12
            Layout.bottomMargin: 16
            spacing: 8

            Image {
                source: "qrc:/branding/DanmaX.png"
                Layout.preferredWidth: 80
                Layout.preferredHeight: 80
                Layout.alignment: Qt.AlignHCenter
                fillMode: Image.PreserveAspectFit
                mipmap: true
            }

            Label {
                text: I18n.nav.appTitle
                font.pixelSize: Ui.titleSize
                font.weight: Font.DemiBold
                color: Ui.textColor
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
            }

            Label {
                text: I18n.format(I18n.about.versionFormat, Qt.application.version)
                color: Ui.secondaryText
                font.pixelSize: Ui.captionSize
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
            }

            Label {
                text: I18n.about.appDescription
                color: Ui.secondaryText
                font.pixelSize: Ui.captionSize
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
            }
        }

        // 2. 反馈分组与交互卡片（自适应铺满主内容区域）
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 8

            Label {
                text: I18n.about.feedbackSection
                font.pixelSize: Ui.sectionHeaderSize
                font.weight: Font.DemiBold
                color: Ui.textColor
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: Ui.cardGap

                ActionCard {
                    objectName: "reportBugCard"
                    iconSource: Ui.icon("logs")
                    title: I18n.about.reportBugTitle
                    description: I18n.about.reportBugDesc
                    onActionTriggered: Qt.openUrlExternally("https://github.com/2CHariko/DanmaX/issues")
                }

                ActionCard {
                    objectName: "suggestFeatureCard"
                    iconSource: Ui.icon("appearance")
                    title: I18n.about.suggestFeatureTitle
                    description: I18n.about.suggestFeatureDesc
                    onActionTriggered: Qt.openUrlExternally("https://github.com/2CHariko/DanmaX/issues/new")
                }

                ActionCard {
                    objectName: "discussionsCard"
                    iconSource: Ui.icon("cloud")
                    title: I18n.about.discussionsTitle
                    description: I18n.about.discussionsDesc
                    onActionTriggered: Qt.openUrlExternally("https://github.com/2CHariko/DanmaX/discussions")
                }
            }
        }

        // 3. 其他链接横向导航
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 12

            Label {
                text: I18n.about.otherLinksSection
                font.pixelSize: Ui.sectionHeaderSize
                font.weight: Font.DemiBold
                color: Ui.textColor
            }

            Flow {
                Layout.fillWidth: true
                spacing: 24

                Repeater {
                    model: [
                        { label: I18n.about.githubRepo, url: "https://github.com/2CHariko/DanmaX" },
                        { label: I18n.about.faq, url: "https://github.com/2CHariko/DanmaX#readme" },
                        { label: I18n.about.contributionGuide, url: "https://github.com/2CHariko/DanmaX/blob/main/CONTRIBUTING.md" },
                        { label: I18n.about.license, url: "https://github.com/2CHariko/DanmaX/blob/main/LICENSE" }
                    ]
                    delegate: Item {
                        id: linkItem
                        required property var modelData
                        implicitWidth: linkText.implicitWidth
                        implicitHeight: linkText.implicitHeight

                        Label {
                            id: linkText
                            text: linkItem.modelData.label
                            font.pixelSize: Ui.bodySize
                            color: linkArea.containsMouse ? (Ui.dark ? Qt.lighter(Ui.accentColor, 1.2) : Qt.darker(Ui.accentColor, 1.2)) : Ui.accentColor
                            font.underline: linkArea.containsMouse
                        }

                        MouseArea {
                            id: linkArea
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            Accessible.role: Accessible.Link
                            Accessible.name: I18n.format(I18n.about.openLinkAccessible, linkItem.modelData.label)
                            onClicked: Qt.openUrlExternally(linkItem.modelData.url)
                        }
                    }
                }
            }
        }
    }
}
