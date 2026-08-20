// 章节导航面板（迭代5：由 ChapterService.sidebarPanel 提供，经图标栏切换加载）。
// 数据来自 chapterService；点击章节跳转编辑器对应行（经 Main.qml 的 focusLine 转发）。
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import FluentUI
import Translex

Item {
    id: root

    // 视觉 token 实例（普通组件；delegate/内联组件内经页面属性中转）
    DesignTokens {
        id: tokens
    }

    property var chapterModel: ListModel {}

    function refresh() {
        chapterModel.clear()
        const titles = chapterService.chapterTitles()
        for (let i = 0; i < titles.length; ++i) {
            chapterModel.append({
                index: i,
                title: titles[i],
                startLine: chapterService.chapterStartLine(i)
            })
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        FluText {
            text: qsTr("章节导航")
            font.pixelSize: tokens.fontHeading   // 面板标题（ui-improvement-plan P1）
            font.bold: true
            leftPadding: 12
            topPadding: 10
            bottomPadding: 6
            Layout.fillWidth: true
        }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: root.chapterModel
            // 列表动画（ui-improvement-plan P0）；受限模式禁用（性能铁律，见 large-file.md）
            add: Transition {
                enabled: !documentModel.limitedMode
                ParallelAnimation {
                    NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 200 }
                    NumberAnimation { property: "y"; from: 10; to: 0; duration: 200; easing.type: Easing.OutCubic }
                }
            }
            move: Transition {
                enabled: !documentModel.limitedMode
                NumberAnimation { property: "y"; duration: 150; easing.type: Easing.OutCubic }
            }
            displaced: Transition {
                enabled: !documentModel.limitedMode
                NumberAnimation { property: "y"; duration: 150; easing.type: Easing.OutCubic }
            }
            delegate: FluItemDelegate {
                width: list.width
                height: 32
                text: model.title
                onClicked: root.focusLine(model.startLine)
            }
        }

        FluText {
            text: qsTr("共 %1 个章节").arg(chapterModel.count)
            font.pixelSize: tokens.fontCaption
            color: FluTheme.fontSecondaryColor
            leftPadding: 12
            bottomPadding: 8
            Layout.fillWidth: true
        }
    }

    Connections {
        target: chapterService
        function onChaptersChanged() { root.refresh() }
    }

    Component.onCompleted: refresh()
}